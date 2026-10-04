/*
 * Nautilus is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nautilus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; see the file COPYING.  If not,
 * see <http://www.gnu.org/licenses/>.
 *
 */
#define G_LOG_DOMAIN "nautilus-search"

/* Search provider backed by Spotlight. Takes the place localsearch has on other systems. */

#include <config.h>
#include "nautilus-search-engine-spotlight.h"

#include "nautilus-query.h"
#include "nautilus-search-hit.h"
#include "nautilus-search-provider.h"

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <CoreServices/CoreServices.h>

#define BATCH_SIZE 100
/* A broad query can match most of the disk. Spotlight stops gathering here. */
#define MAX_RESULTS 10000
/* Limit for one-folder searches: gathering cannot be interrupted, and this takes about a second. */
#define MAX_GATHERED_RESULTS 20000
/* Where macOS mounts the user's data. Spotlight reports its folders as seen from the root. */
#define DATA_VOLUME_PATH "/System/Volumes/Data"
/* The other volumes mounted there are internal to the system. */
#define SYSTEM_VOLUMES_PATH "/System/Volumes/"

typedef struct
{
    NautilusSearchEngineSpotlight *engine;
    GCancellable *cancellable;

    NautilusQuery *query;
    /* Folder being searched, or NULL when searching everywhere. */
    GFile *location;
    char *query_string;
    gboolean recursive;
    gboolean show_hidden;
    gboolean search_content;
    /* Whether the type filter asks for folders and nothing else. */
    gboolean only_folders;

    /* The following data is only used by the search thread. */
    /* Path of @location with links resolved and no trailing separator. */
    char *location_path;
    /* The same folder as seen from the root, when it is on the data volume. */
    const char *location_alias;
    MDQueryRef md_query;

    GMutex idle_mutex;
    /* The following data is shared between threads: lock the mutex. */
    GQueue *idle_queue;
    guint processing_id;
    gboolean finished;
} SearchThreadData;

struct _NautilusSearchEngineSpotlight
{
    GObject parent_instance;

    NautilusQuery *query;

    SearchThreadData *active_search;
};

static void nautilus_search_provider_init (NautilusSearchProviderInterface *iface);

G_DEFINE_TYPE_WITH_CODE (NautilusSearchEngineSpotlight,
                         nautilus_search_engine_spotlight,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (NAUTILUS_TYPE_SEARCH_PROVIDER,
                                                nautilus_search_provider_init))

static void
finalize (GObject *object)
{
    NautilusSearchEngineSpotlight *self = NAUTILUS_SEARCH_ENGINE_SPOTLIGHT (object);

    g_clear_object (&self->query);

    G_OBJECT_CLASS (nautilus_search_engine_spotlight_parent_class)->finalize (object);
}

static gboolean
query_wants_only_folders (NautilusQuery *query)
{
    g_autoptr (GPtrArray) types = nautilus_query_get_mime_types (query);

    return types->len == 1 && g_str_equal (g_ptr_array_index (types, 0), "public.folder");
}

static SearchThreadData *
search_thread_data_new (NautilusSearchEngineSpotlight *engine,
                        NautilusQuery                 *query,
                        GFile                         *location,
                        char                          *query_string)
{
    SearchThreadData *data;

    data = g_new0 (SearchThreadData, 1);

    data->engine = g_object_ref (engine);
    data->cancellable = g_cancellable_new ();
    data->query = g_object_ref (query);
    g_set_object (&data->location, location);
    data->query_string = query_string;
    data->recursive = nautilus_query_recursive (query);
    data->show_hidden = nautilus_query_get_show_hidden_files (query);
    data->search_content = nautilus_query_get_search_content (query);
    data->only_folders = query_wants_only_folders (query);

    g_mutex_init (&data->idle_mutex);
    data->idle_queue = g_queue_new ();

    return data;
}

static void
search_thread_data_free (SearchThreadData *data)
{
    GPtrArray *hits;

    g_clear_pointer (&data->md_query, CFRelease);
    g_free (data->location_path);
    g_free (data->query_string);
    g_clear_object (&data->location);
    g_object_unref (data->query);
    g_object_unref (data->cancellable);
    g_object_unref (data->engine);
    g_mutex_clear (&data->idle_mutex);

    while ((hits = g_queue_pop_head (data->idle_queue)))
    {
        g_ptr_array_unref (hits);
    }
    g_queue_free (data->idle_queue);

    g_free (data);
}

/* Runs in the main thread, once the search thread is done with @data. */
static gboolean
search_thread_done (SearchThreadData *data)
{
    NautilusSearchEngineSpotlight *engine = data->engine;

    if (g_cancellable_is_cancelled (data->cancellable))
    {
        g_debug ("Spotlight engine finished and cancelled");
    }
    else
    {
        g_debug ("Spotlight engine finished");
    }

    engine->active_search = NULL;
    nautilus_search_provider_finished (NAUTILUS_SEARCH_PROVIDER (engine),
                                       NAUTILUS_SEARCH_PROVIDER_STATUS_NORMAL);

    search_thread_data_free (data);

    return G_SOURCE_REMOVE;
}

/* Runs in the main thread and delivers one batch per main loop iteration. */
static gboolean
search_thread_process_idle (gpointer user_data)
{
    SearchThreadData *data = user_data;
    GPtrArray *hits;
    gboolean finished;
    gboolean cancelled;

    g_mutex_lock (&data->idle_mutex);

    hits = g_queue_pop_head (data->idle_queue);
    finished = data->finished;
    cancelled = g_cancellable_is_cancelled (data->cancellable);

    /* Done when the queue is empty, or at once if the search was cancelled and has finished. */
    if (hits == NULL || (finished && cancelled))
    {
        data->processing_id = 0;
        g_mutex_unlock (&data->idle_mutex);

        g_clear_pointer (&hits, g_ptr_array_unref);

        if (finished)
        {
            search_thread_done (data);
        }

        return G_SOURCE_REMOVE;
    }

    g_mutex_unlock (&data->idle_mutex);

    if (!cancelled)
    {
        g_debug ("Spotlight engine add hits");
        nautilus_search_provider_hits_added (NAUTILUS_SEARCH_PROVIDER (data->engine),
                                             g_steal_pointer (&hits));
    }

    g_clear_pointer (&hits, g_ptr_array_unref);

    return G_SOURCE_CONTINUE;
}

static void
send_batch_in_idle (SearchThreadData  *data,
                    GPtrArray        **hits)
{
    if (*hits == NULL)
    {
        return;
    }

    g_mutex_lock (&data->idle_mutex);

    g_queue_push_tail (data->idle_queue, g_steal_pointer (hits));
    if (data->processing_id == 0)
    {
        data->processing_id = g_idle_add (search_thread_process_idle, data);
    }

    g_mutex_unlock (&data->idle_mutex);
}

/* The search thread must not use @data after calling this. */
static void
finish_search_thread (SearchThreadData *data)
{
    gboolean idle_pending;

    g_mutex_lock (&data->idle_mutex);
    data->finished = TRUE;
    idle_pending = data->processing_id != 0;
    g_mutex_unlock (&data->idle_mutex);

    /* If an idle is still delivering batches, it finishes the search. */
    if (!idle_pending)
    {
        g_idle_add (G_SOURCE_FUNC (search_thread_done), data);
    }
}

static char *
string_from_cfstring (CFStringRef string)
{
    CFIndex size;
    char *buffer;

    size = CFStringGetMaximumSizeForEncoding (CFStringGetLength (string),
                                              kCFStringEncodingUTF8) + 1;
    buffer = g_malloc (size);

    if (!CFStringGetCString (string, buffer, size, kCFStringEncodingUTF8))
    {
        g_free (buffer);
        return NULL;
    }

    return buffer;
}

/* Dates come from the query's value lists: reading them per item costs about 0.5 ms each. */
static GDateTime *
date_time_from_result (MDQueryRef  md_query,
                       CFStringRef name,
                       CFIndex     idx)
{
    CFTypeRef value = MDQueryGetAttributeValueOfResultAtIndex (md_query, name, idx);

    if (value == NULL || CFGetTypeID (value) != CFDateGetTypeID ())
    {
        return NULL;
    }

    return g_date_time_new_from_unix_local ((gint64) (CFDateGetAbsoluteTime (value) +
                                                      kCFAbsoluteTimeIntervalSince1970));
}

/* Returns: (nullable): the part of @path below @folder (no trailing separator), or NULL. */
static const char *
path_below_folder (const char *path,
                   const char *folder)
{
    size_t length = strlen (folder);
    const char *relative;

    if (!g_str_has_prefix (path, folder) ||
        (path[length] != G_DIR_SEPARATOR && path[length] != '\0'))
    {
        return NULL;
    }

    relative = path + length;
    while (*relative == G_DIR_SEPARATOR)
    {
        relative++;
    }

    return relative;
}

/* Returns: (nullable): @path relative to the searched folder, or NULL if the file is not wanted. */
static const char *
get_wanted_path (SearchThreadData *data,
                 const char       *path)
{
    const char *relative = path;

    if (data->location_path != NULL)
    {
        relative = path_below_folder (path, data->location_path);
        if (relative == NULL && data->location_alias != NULL)
        {
            relative = path_below_folder (path, data->location_alias);
        }

        if (relative == NULL || *relative == '\0')
        {
            /* Outside of the searched folder, or the folder itself. */
            return NULL;
        }

        if (!data->recursive && strchr (relative, G_DIR_SEPARATOR) != NULL)
        {
            return NULL;
        }
    }
    else if (g_str_has_prefix (path, SYSTEM_VOLUMES_PATH) &&
             path_below_folder (path, DATA_VOLUME_PATH) == NULL)
    {
        return NULL;
    }

    if (!data->show_hidden &&
        (relative[0] == '.' ||
         strstr (relative, G_DIR_SEPARATOR_S ".") != NULL ||
         g_str_has_suffix (relative, "~") ||
         strstr (relative, "~" G_DIR_SEPARATOR_S) != NULL))
    {
        /* Hidden and backup files and folders, as the simple engine skips them. */
        return NULL;
    }

    return relative;
}

static NautilusSearchHit *
hit_from_result (SearchThreadData *data,
                 CFIndex           idx)
{
    MDItemRef item = (MDItemRef) MDQueryGetResultAtIndex (data->md_query, idx);
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    g_autofree char *basename = NULL;
    g_autoptr (GDateTime) mtime = NULL;
    g_autoptr (GDateTime) atime = NULL;
    g_autoptr (GDateTime) ctime = NULL;
    const char *relative;
    CFTypeRef path_value;
    NautilusSearchHit *hit;
    gdouble match;

    /* The path is not available from the query's value lists. */
    path_value = MDItemCopyAttribute (item, kMDItemPath);
    if (path_value == NULL)
    {
        return NULL;
    }

    if (CFGetTypeID (path_value) == CFStringGetTypeID ())
    {
        path = string_from_cfstring (path_value);
    }
    CFRelease (path_value);

    if (path == NULL)
    {
        return NULL;
    }

    relative = get_wanted_path (data, path);
    if (relative == NULL)
    {
        return NULL;
    }

    if (data->only_folders)
    {
        GStatBuf info;

        /* Spotlight counts some plain files as directories. Follow links: an app can be one. */
        if (g_stat (path, &info) != 0 || !S_ISDIR (info.st_mode))
        {
            return NULL;
        }
    }

    basename = g_path_get_basename (path);
    /* Negative when the name does not match the way NautilusQuery compares. */
    match = nautilus_query_matches_string (data->query, basename);
    if (match < 0 && !data->search_content)
    {
        /* Spotlight ignores accents ("é" finds "e"): keep it only if the contents may match. */
        return NULL;
    }

    if (data->location != NULL)
    {
        /* Name the file through the searched folder, not its resolved path, to avoid duplicates. */
        g_autoptr (GFile) file = g_file_resolve_relative_path (data->location, relative);

        uri = g_file_get_uri (file);
    }
    else
    {
        uri = g_filename_to_uri (path, NULL, NULL);
    }

    if (uri == NULL)
    {
        return NULL;
    }

    mtime = date_time_from_result (data->md_query, kMDItemFSContentChangeDate, idx);
    atime = date_time_from_result (data->md_query, kMDItemLastUsedDate, idx);
    ctime = date_time_from_result (data->md_query, kMDItemFSCreationDate, idx);

    if (mtime == NULL || atime == NULL || ctime == NULL)
    {
        GStatBuf info;

        /* Read the dates Spotlight lacks from the file: the ranking uses them. */
        if (g_lstat (path, &info) == 0)
        {
            if (mtime == NULL)
            {
                mtime = g_date_time_new_from_unix_local (info.st_mtime);
            }
            if (atime == NULL)
            {
                atime = g_date_time_new_from_unix_local (info.st_atime);
            }
            if (ctime == NULL)
            {
                ctime = g_date_time_new_from_unix_local (info.st_birthtime);
            }
        }
    }

    hit = nautilus_search_hit_new (uri);
    nautilus_search_hit_set_fts_rank (hit, MAX (match, 0));

    if (mtime != NULL)
    {
        nautilus_search_hit_set_modification_time (hit, mtime);
    }
    if (atime != NULL)
    {
        nautilus_search_hit_set_access_time (hit, atime);
    }
    if (ctime != NULL)
    {
        nautilus_search_hit_set_creation_time (hit, ctime);
    }

    return hit;
}

/* Resolving links can block on a slow volume, so this runs in the search thread. */
static void
resolve_location (SearchThreadData *data)
{
    g_autofree char *path = NULL;
    size_t length;
    size_t volume_length = strlen (DATA_VOLUME_PATH);

    if (data->location == NULL)
    {
        return;
    }

    path = g_file_get_path (data->location);

    /* Spotlight reports paths with symbolic links resolved. */
    data->location_path = realpath (path, NULL);
    if (data->location_path == NULL)
    {
        data->location_path = g_steal_pointer (&path);
    }

    length = strlen (data->location_path);
    while (length > 0 && data->location_path[length - 1] == G_DIR_SEPARATOR)
    {
        data->location_path[--length] = '\0';
    }

    if (g_str_has_prefix (data->location_path, DATA_VOLUME_PATH) &&
        (data->location_path[volume_length] == G_DIR_SEPARATOR ||
         data->location_path[volume_length] == '\0'))
    {
        data->location_alias = data->location_path + volume_length;
    }
}

/* Returns: (nullable): the Spotlight query for @data, ready to execute. */
static MDQueryRef
create_md_query (SearchThreadData *data)
{
    const void *date_names[] =
    {
        kMDItemFSContentChangeDate,
        kMDItemLastUsedDate,
        kMDItemFSCreationDate,
    };
    CFStringRef query_string = NULL;
    CFArrayRef date_attributes = NULL;
    CFStringRef scope = NULL;
    CFArrayRef scopes = NULL;
    MDQueryRef md_query = NULL;

    query_string = CFStringCreateWithCString (kCFAllocatorDefault, data->query_string,
                                              kCFStringEncodingUTF8);
    date_attributes = CFArrayCreate (kCFAllocatorDefault, date_names,
                                     G_N_ELEMENTS (date_names),
                                     &kCFTypeArrayCallBacks);

    if (data->location_path == NULL)
    {
        scope = CFRetain (kMDQueryScopeComputer);
    }
    else
    {
        /* An empty path is the root folder, with its separator removed. */
        const char *path = data->location_path[0] != '\0' ?
                           data->location_path : G_DIR_SEPARATOR_S;

        scope = CFStringCreateWithCString (kCFAllocatorDefault, path, kCFStringEncodingUTF8);
    }

    if (scope != NULL)
    {
        scopes = CFArrayCreate (kCFAllocatorDefault, (const void **) &scope, 1,
                                &kCFTypeArrayCallBacks);
    }

    if (query_string != NULL && date_attributes != NULL && scopes != NULL)
    {
        md_query = MDQueryCreate (kCFAllocatorDefault, query_string, date_attributes, NULL);
    }

    if (md_query != NULL)
    {
        gboolean one_folder = data->location_path != NULL && !data->recursive;

        MDQuerySetSearchScope (md_query, scopes, 0);
        MDQuerySetMaxCount (md_query, one_folder ? MAX_GATHERED_RESULTS : MAX_RESULTS);
    }

    g_clear_pointer (&scopes, CFRelease);
    g_clear_pointer (&scope, CFRelease);
    g_clear_pointer (&date_attributes, CFRelease);
    g_clear_pointer (&query_string, CFRelease);

    return md_query;
}

static gpointer
search_thread_func (gpointer user_data)
{
    SearchThreadData *data = user_data;
    GPtrArray *hits = NULL;

    resolve_location (data);
    data->md_query = create_md_query (data);

    if (data->md_query == NULL)
    {
        g_debug ("Spotlight engine could not create the query");
    }
    else if (g_cancellable_is_cancelled (data->cancellable))
    {
        /* Stopped before the search began. */
    }
    else if (!MDQueryExecute (data->md_query, kMDQuerySynchronous))
    {
        /* MDQueryExecute () blocks until Spotlight has gathered the results. */
        g_debug ("Spotlight engine could not run the query");
    }
    else
    {
        CFIndex count = MDQueryGetResultCount (data->md_query);

        for (CFIndex i = 0; i < count && !g_cancellable_is_cancelled (data->cancellable); i++)
        {
            NautilusSearchHit *hit = hit_from_result (data, i);

            if (hit == NULL)
            {
                continue;
            }

            if (hits == NULL)
            {
                hits = g_ptr_array_new_with_free_func (g_object_unref);
            }
            g_ptr_array_add (hits, hit);

            if (hits->len >= BATCH_SIZE)
            {
                send_batch_in_idle (data, &hits);
            }
        }
    }

    send_batch_in_idle (data, &hits);

    /* Release the query here: it frees every result, too slow for the main thread. */
    g_clear_pointer (&data->md_query, CFRelease);

    finish_search_thread (data);

    return NULL;
}

/* Appends @text as a quoted Spotlight string. Modifiers: "c" ignores case, "d" ignores accents. */
static void
append_quoted (GString    *string,
               const char *text,
               gboolean    wildcard_before,
               gboolean    wildcard_after,
               const char *modifiers)
{
    g_string_append_c (string, '"');
    if (wildcard_before)
    {
        g_string_append_c (string, '*');
    }

    for (const char *c = text; *c != '\0'; c++)
    {
        if (*c == '\\' || *c == '"' || *c == '*')
        {
            g_string_append_c (string, '\\');
        }
        g_string_append_c (string, *c);
    }

    if (wildcard_after)
    {
        g_string_append_c (string, '*');
    }
    g_string_append_c (string, '"');
    g_string_append (string, modifiers);
}

/* A word with an accent is compared exactly, as NautilusQuery does: "é" must not find "e". */
static const char *
get_name_modifiers (const char *word)
{
    g_autofree char *normalized = g_utf8_normalize (word, -1, G_NORMALIZE_NFD);

    for (const char *c = normalized; c != NULL && *c != '\0'; c = g_utf8_next_char (c))
    {
        if (g_unichar_ismark (g_utf8_get_char (c)))
        {
            return "c";
        }
    }

    return "cd";
}

/* Every word must match the name or the text. Returns: whether @text had any word. */
static gboolean
add_text_clauses (GStrvBuilder *clauses,
                  const char   *text,
                  gboolean      search_content,
                  gboolean      require_content)
{
    /* Split on spaces only, as NautilusQuery does for its own matching. */
    g_auto (GStrv) words = g_strsplit (text, " ", -1);
    g_autoptr (GString) content_clause = g_string_new (NULL);

    for (guint i = 0; words[i] != NULL; i++)
    {
        g_autoptr (GString) clause = NULL;

        if (words[i][0] == '\0')
        {
            continue;
        }

        clause = g_string_new ("(kMDItemFSName == ");
        append_quoted (clause, words[i], TRUE, TRUE, get_name_modifiers (words[i]));

        if (search_content)
        {
            g_string_append (clause, " || kMDItemTextContent == ");
            append_quoted (clause, words[i], FALSE, TRUE, "cd");
        }

        g_string_append_c (clause, ')');
        g_strv_builder_add (clauses, clause->str);

        g_string_append (content_clause, content_clause->len == 0 ? "(" : " || ");
        g_string_append (content_clause, "kMDItemTextContent == ");
        append_quoted (content_clause, words[i], FALSE, TRUE, "cd");
    }

    if (content_clause->len == 0)
    {
        return FALSE;
    }

    if (require_content)
    {
        g_string_append_c (content_clause, ')');
        g_strv_builder_add (clauses, content_clause->str);
    }

    return TRUE;
}

/* Spotlight filters by uniform type identifier. Returns: whether any type could be expressed. */
static gboolean
add_type_clause (GStrvBuilder *clauses,
                 GPtrArray    *mime_types)
{
    g_autoptr (GString) clause = g_string_new (NULL);

    for (guint i = 0; i < mime_types->len; i++)
    {
        const char *mime_type = g_ptr_array_index (mime_types, i);
        g_autofree char *content_type = NULL;

        if (strchr (mime_type, '/') != NULL)
        {
            content_type = g_content_type_from_mime_type (mime_type);
        }
        else
        {
            content_type = g_strdup (mime_type);
        }

        /* "dyn." identifiers are made up on the spot for unknown types. */
        if (content_type == NULL || g_str_has_prefix (content_type, "dyn."))
        {
            continue;
        }

        g_string_append (clause, clause->len == 0 ? "(" : " || ");
        g_string_append (clause, "kMDItemContentTypeTree == ");

        if (g_str_equal (content_type, "public.folder"))
        {
            /* GIO calls apps and packages folders too; Spotlight calls those directories. */
            append_quoted (clause, "public.directory", FALSE, FALSE, "");
        }
        else
        {
            append_quoted (clause, content_type, FALSE, FALSE, "");
        }
    }

    if (clause->len == 0)
    {
        return FALSE;
    }

    g_string_append_c (clause, ')');
    g_strv_builder_add (clauses, clause->str);

    return TRUE;
}

static void
add_date_clause (GStrvBuilder           *clauses,
                 GPtrArray              *date_range,
                 NautilusSearchTimeType  type)
{
    GDateTime *initial_date = g_ptr_array_index (date_range, 0);
    GDateTime *end_date = g_ptr_array_index (date_range, 1);
    /* As for other searches, the end date is inclusive: add a day to it. */
    g_autoptr (GDateTime) shifted_end_date = g_date_time_add_days (end_date, 1);
    g_autoptr (GDateTime) initial_utc = g_date_time_to_utc (initial_date);
    g_autoptr (GDateTime) end_utc = g_date_time_to_utc (shifted_end_date);
    g_autofree char *initial_format = g_date_time_format (initial_utc, "%Y-%m-%dT%H:%M:%SZ");
    g_autofree char *end_format = g_date_time_format (end_utc, "%Y-%m-%dT%H:%M:%SZ");
    g_autofree char *clause = NULL;
    const char *attribute;

    switch (type)
    {
        case NAUTILUS_SEARCH_TIME_TYPE_LAST_ACCESS:
        {
            /* Spotlight has no access time, only a last-opened date that few files have. */
            attribute = "kMDItemLastUsedDate";
        }
        break;

        case NAUTILUS_SEARCH_TIME_TYPE_CREATED:
        {
            attribute = "kMDItemFSCreationDate";
        }
        break;

        case NAUTILUS_SEARCH_TIME_TYPE_LAST_MODIFIED:
        default:
        {
            attribute = "kMDItemFSContentChangeDate";
        }
        break;
    }

    clause = g_strdup_printf ("(%s >= $time.iso(%s) && %s <= $time.iso(%s))",
                              attribute, initial_format,
                              attribute, end_format);
    g_strv_builder_add (clauses, clause);
}

/* Returns: (nullable) (transfer full): the Spotlight query, or NULL to leave it to the others. */
static char *
build_query_string (NautilusQuery *query,
                    gboolean       has_location)
{
    g_autoptr (GStrvBuilder) clauses = g_strv_builder_new ();
    g_autofree char *text = nautilus_query_get_text (query);
    g_autoptr (GPtrArray) mime_types = nautilus_query_get_mime_types (query);
    g_autoptr (GPtrArray) date_range = nautilus_query_get_date_range (query);
    gboolean search_content = nautilus_query_get_search_content (query);
    /* Only the children of one folder are wanted. */
    gboolean one_folder = has_location && !nautilus_query_recursive (query);
    g_auto (GStrv) strv = NULL;

    if (one_folder && (!search_content || text == NULL))
    {
        /* The simple engine lists one folder faster than Spotlight gathers its whole tree. */
        return NULL;
    }

    /* For one folder, only ask for content matches: the simple engine finds the names. */
    if (text != NULL &&
        !add_text_clauses (clauses, text, search_content, one_folder) &&
        one_folder)
    {
        return NULL;
    }

    if (mime_types != NULL && mime_types->len > 0 &&
        !add_type_clause (clauses, mime_types))
    {
        return NULL;
    }

    if (date_range != NULL)
    {
        NautilusSearchTimeType type = nautilus_query_get_search_type (query);

        if (type == NAUTILUS_SEARCH_TIME_TYPE_LAST_ACCESS && has_location)
        {
            /* Left to the simple engine, which reads the real access time. */
            return NULL;
        }

        add_date_clause (clauses, date_range, type);
    }

    if (!has_location)
    {
        /* System files would use up the result limit. */
        g_strv_builder_add (clauses, "kMDItemSupportFileType != \"MDSystemFile\"");
    }

    strv = g_strv_builder_end (clauses);
    if (strv[0] == NULL)
    {
        /* Every file. A bare wildcard on the name matches nothing. */
        return g_strdup ("kMDItemContentTypeTree == \"public.item\"");
    }

    return g_strjoinv (" && ", strv);
}

static gboolean
search_engine_spotlight_start (NautilusSearchProvider *provider,
                               NautilusQuery          *query)
{
    NautilusSearchEngineSpotlight *self = NAUTILUS_SEARCH_ENGINE_SPOTLIGHT (provider);
    g_autoptr (GFile) location = NULL;
    g_autoptr (GThread) thread = NULL;
    g_autofree char *query_string = NULL;

    g_set_object (&self->query, query);

    if (self->active_search != NULL)
    {
        return FALSE;
    }

    location = nautilus_query_get_location (self->query);
    if (location != NULL && !g_file_is_native (location))
    {
        /* Spotlight only knows about files that have a local path. */
        return FALSE;
    }

    query_string = build_query_string (self->query, location != NULL);
    if (query_string == NULL)
    {
        return FALSE;
    }

    g_debug ("Spotlight engine start");
    g_debug ("Spotlight query: %s", query_string);

    self->active_search = search_thread_data_new (self, self->query, location,
                                                  g_steal_pointer (&query_string));

    thread = g_thread_new ("nautilus-search-spotlight", search_thread_func,
                           self->active_search);

    return TRUE;
}

static void
nautilus_search_engine_spotlight_stop (NautilusSearchProvider *provider)
{
    NautilusSearchEngineSpotlight *self = NAUTILUS_SEARCH_ENGINE_SPOTLIGHT (provider);

    if (self->active_search != NULL)
    {
        g_debug ("Spotlight engine stop");
        /* The thread still reports that it finished, without any hits. */
        g_cancellable_cancel (self->active_search->cancellable);
    }
}

static void
nautilus_search_provider_init (NautilusSearchProviderInterface *iface)
{
    iface->start = search_engine_spotlight_start;
    iface->stop = nautilus_search_engine_spotlight_stop;
}

static void
nautilus_search_engine_spotlight_class_init (NautilusSearchEngineSpotlightClass *class)
{
    GObjectClass *gobject_class;

    gobject_class = G_OBJECT_CLASS (class);
    gobject_class->finalize = finalize;
}

static void
nautilus_search_engine_spotlight_init (NautilusSearchEngineSpotlight *engine)
{
}

NautilusSearchEngineSpotlight *
nautilus_search_engine_spotlight_new (void)
{
    return g_object_new (NAUTILUS_TYPE_SEARCH_ENGINE_SPOTLIGHT, NULL);
}
