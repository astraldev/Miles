/*
 * Copyright (C) 2026 Ekure Edem
 *
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

/*
 * GIO reads a folder file by file: 49 s for 800,000 files. In bulk, from several threads: 2 s.
 */

#define G_LOG_DOMAIN "nautilus-search"

#include <config.h>
#include "nautilus-search-engine-walker.h"

#include "nautilus-query.h"
#include "nautilus-search-hit.h"
#include "nautilus-ui-utilities.h"

#include <fcntl.h>
#include <sys/attr.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <unistd.h>

#define N_THREADS 4
#define BUFFER_SIZE (256 * 1024)
#define FLUSH_TIME_SPAN (250 * G_TIME_SPAN_MILLISECOND)

struct _NautilusSearchEngineWalker
{
    NautilusSearchProvider parent_instance;
};

G_DEFINE_FINAL_TYPE (NautilusSearchEngineWalker,
                     nautilus_search_engine_walker,
                     NAUTILUS_TYPE_SEARCH_PROVIDER)

typedef struct
{
    gpointer provider;
    NautilusQuery *query;
    GCancellable *cancellable;
    GPtrArray *date_range;
    gboolean recursive;

    GMutex lock;
    GCond changed;
    GQueue folders;
    guint n_folders_being_read;
    guint n_threads;
    gint64 last_flush_time;
} Walk;

typedef struct
{
    const char *name;
    uint32_t error;
    dev_t device;
    fsobj_type_t type;
    uint32_t flags;
    struct timespec times[3];
} Entry;

#define READ_FIELD(attribute, target) \
        if (returned.commonattr & (attribute)) \
        { \
            memcpy (&(target), field, sizeof (target)); \
            field += sizeof (target); \
        }

/*
 * The fields come in this order, and only those MacOS could give.
 */
static void
parse_entry (const char *field,
             Entry      *entry)
{
    attribute_set_t returned;
    attrreference_t name;

    field += sizeof (uint32_t);
    memcpy (&returned, field, sizeof (returned));
    field += sizeof (returned);

    READ_FIELD (ATTR_CMN_ERROR, entry->error);
    if (returned.commonattr & ATTR_CMN_NAME)
    {
        memcpy (&name, field, sizeof (name));
        entry->name = field + name.attr_dataoffset;
        field += sizeof (name);
    }
    READ_FIELD (ATTR_CMN_DEVID, entry->device);
    READ_FIELD (ATTR_CMN_OBJTYPE, entry->type);
    READ_FIELD (ATTR_CMN_CRTIME, entry->times[NAUTILUS_SEARCH_TIME_TYPE_CREATED]);
    READ_FIELD (ATTR_CMN_MODTIME, entry->times[NAUTILUS_SEARCH_TIME_TYPE_LAST_MODIFIED]);
    READ_FIELD (ATTR_CMN_ACCTIME, entry->times[NAUTILUS_SEARCH_TIME_TYPE_LAST_ACCESS]);
    READ_FIELD (ATTR_CMN_FLAGS, entry->flags);
}

static gboolean
should_descend (Walk        *walk,
                const char  *path,
                const Entry *entry,
                dev_t        folder_device)
{
    struct statfs file_system;

    /*
     * An app is one thing to the user, as a search in Finder has it.
     */
    if (g_str_has_suffix (entry->name, ".app"))
    {
        return FALSE;
    }

    if (entry->device == folder_device)
    {
        return TRUE;
    }

    if (statfs (path, &file_system) != 0)
    {
        return FALSE;
    }

    /*
     * /Users and others are on another volume without being where it is mounted.
     */
    if (!g_str_equal (file_system.f_mntonname, path))
    {
        return TRUE;
    }

    /*
     * The volumes MacOS keeps for itself repeat the files of the startup disk.
     */
    return !(file_system.f_flags & MNT_DONTBROWSE) &&
           (!nautilus_query_recursive_local_only (walk->query) ||
            (file_system.f_flags & MNT_LOCAL));
}

static void
add_hit (Walk        *walk,
         const char  *path,
         const Entry *entry,
         double       rank)
{
    g_autoptr (GDateTime) modified = g_date_time_new_from_unix_utc (entry->times[NAUTILUS_SEARCH_TIME_TYPE_LAST_MODIFIED].tv_sec);
    g_autoptr (GDateTime) accessed = g_date_time_new_from_unix_utc (entry->times[NAUTILUS_SEARCH_TIME_TYPE_LAST_ACCESS].tv_sec);
    g_autoptr (GDateTime) created = g_date_time_new_from_unix_utc (entry->times[NAUTILUS_SEARCH_TIME_TYPE_CREATED].tv_sec);
    GDateTime *dates[] = { modified, accessed, created };
    g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
    NautilusSearchHit *hit;
    gint64 now;

    if (uri == NULL)
    {
        return;
    }

    if (nautilus_query_has_mime_types (walk->query))
    {
        gboolean is_folder = (entry->type == VDIR && !g_str_has_suffix (entry->name, ".app"));
        g_autofree char *content_type = is_folder ?
                                        g_strdup ("public.folder") :
                                        g_content_type_guess (entry->name, NULL, 0, NULL);

        if (!nautilus_query_matches_mime_type (walk->query, content_type))
        {
            return;
        }
    }

    if (walk->date_range != NULL &&
        !nautilus_date_time_is_between_dates (dates[nautilus_query_get_search_type (walk->query)],
                                              g_ptr_array_index (walk->date_range, 0),
                                              g_ptr_array_index (walk->date_range, 1)))
    {
        return;
    }

    hit = nautilus_search_hit_new (uri);
    nautilus_search_hit_set_fts_rank (hit, rank);
    nautilus_search_hit_set_modification_time (hit, modified);
    nautilus_search_hit_set_access_time (hit, accessed);
    nautilus_search_hit_set_creation_time (hit, created);

    /*
     * The provider takes hits from one thread at a time.
     */
    g_mutex_lock (&walk->lock);

    nautilus_search_provider_add_hit (walk->provider, hit);

    now = g_get_monotonic_time ();
    if (now - walk->last_flush_time >= FLUSH_TIME_SPAN)
    {
        walk->last_flush_time = now;
        nautilus_search_provider_flush_hits (walk->provider);
    }

    g_mutex_unlock (&walk->lock);
}

static void
handle_entry (Walk        *walk,
              const char  *folder,
              dev_t        folder_device,
              const Entry *entry)
{
    gboolean descends = (entry->type == VDIR && walk->recursive);
    g_autofree char *path = NULL;
    g_autofree char *valid_name = NULL;
    double rank;

    if (entry->name == NULL || entry->error != 0)
    {
        return;
    }

    if (!nautilus_query_get_show_hidden_files (walk->query) &&
        (entry->name[0] == '.' ||
         (entry->flags & UF_HIDDEN) ||
         g_str_has_suffix (entry->name, "~")))
    {
        return;
    }

    /*
     * A server can have names that are not UTF-8, which matching needs.
     */
    if (!g_utf8_validate (entry->name, -1, NULL))
    {
        valid_name = g_utf8_make_valid (entry->name, -1);
    }

    rank = nautilus_query_matches_string (walk->query, valid_name != NULL ? valid_name : entry->name);
    if (rank < 0 && !descends)
    {
        return;
    }

    path = g_build_filename (folder, entry->name, NULL);

    if (rank >= 0)
    {
        add_hit (walk, path, entry, rank);
    }

    if (descends && should_descend (walk, path, entry, folder_device))
    {
        g_mutex_lock (&walk->lock);
        g_queue_push_tail (&walk->folders, g_steal_pointer (&path));
        g_cond_signal (&walk->changed);
        g_mutex_unlock (&walk->lock);
    }
}

static void
read_folder (Walk       *walk,
             const char *folder,
             char       *buffer)
{
    struct attrlist attributes =
    {
        .bitmapcount = ATTR_BIT_MAP_COUNT,
        .commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_ERROR | ATTR_CMN_NAME |
                      ATTR_CMN_DEVID | ATTR_CMN_OBJTYPE | ATTR_CMN_CRTIME |
                      ATTR_CMN_MODTIME | ATTR_CMN_ACCTIME | ATTR_CMN_FLAGS,
    };
    int fd = open (folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    struct stat folder_info;
    int n_entries;

    if (fd < 0)
    {
        return;
    }

    fstat (fd, &folder_info);

    while (!g_cancellable_is_cancelled (walk->cancellable) &&
           (n_entries = getattrlistbulk (fd, &attributes, buffer, BUFFER_SIZE, 0)) > 0)
    {
        const char *data = buffer;

        for (int i = 0; i < n_entries; i++)
        {
            Entry entry = { 0 };
            uint32_t length;

            memcpy (&length, data, sizeof (length));
            parse_entry (data, &entry);
            handle_entry (walk, folder, folder_info.st_dev, &entry);

            data += length;
        }
    }

    close (fd);
}

static gpointer
walk_thread (gpointer user_data)
{
    Walk *walk = user_data;
    g_autofree char *buffer = g_malloc (BUFFER_SIZE);
    char *folder;
    gboolean is_last;

    g_mutex_lock (&walk->lock);

    while (TRUE)
    {
        /*
         * A folder being read may still add its folders to the queue.
         */
        while (g_queue_is_empty (&walk->folders) && walk->n_folders_being_read > 0)
        {
            g_cond_wait (&walk->changed, &walk->lock);
        }

        folder = g_queue_pop_head (&walk->folders);
        if (folder == NULL)
        {
            break;
        }

        walk->n_folders_being_read++;
        g_mutex_unlock (&walk->lock);

        if (!g_cancellable_is_cancelled (walk->cancellable))
        {
            read_folder (walk, folder, buffer);
        }
        g_free (folder);

        g_mutex_lock (&walk->lock);
        walk->n_folders_being_read--;
        g_cond_broadcast (&walk->changed);
    }

    is_last = (--walk->n_threads == 0);
    g_mutex_unlock (&walk->lock);

    if (is_last)
    {
        g_idle_add_once ((GSourceOnceFunc) nautilus_search_provider_finished, walk->provider);

        g_clear_pointer (&walk->date_range, g_ptr_array_unref);
        g_cond_clear (&walk->changed);
        g_mutex_clear (&walk->lock);
        g_free (walk);
    }

    return NULL;
}

/*
 * Finishes the search of the provider itself.
 */
void
nautilus_search_engine_walker_search (gpointer  provider,
                                      GFile    *location,
                                      gboolean  recursive)
{
    Walk *walk = g_new0 (Walk, 1);

    walk->provider = provider;
    walk->query = nautilus_search_provider_get_query (provider);
    walk->cancellable = nautilus_search_provider_get_cancellable (provider);
    walk->date_range = nautilus_query_get_date_range (walk->query);
    walk->recursive = recursive;
    walk->n_threads = N_THREADS;
    walk->last_flush_time = g_get_monotonic_time ();
    g_mutex_init (&walk->lock);
    g_cond_init (&walk->changed);
    g_queue_init (&walk->folders);

    if (location != NULL)
    {
        g_queue_push_tail (&walk->folders, g_file_get_path (location));
    }
    else
    {
        g_queue_push_tail (&walk->folders, g_strdup (g_get_home_dir ()));
        g_queue_push_tail (&walk->folders, g_strdup ("/Applications"));
        g_queue_push_tail (&walk->folders, g_strdup ("/System/Applications"));
    }

    for (guint i = 0; i < N_THREADS; i++)
    {
        g_thread_unref (g_thread_new ("nautilus-search-walker", walk_thread, walk));
    }
}

static const char *
get_name (NautilusSearchProvider *provider)
{
    return "walker";
}

static gboolean
run_in_thread (NautilusSearchProvider *provider)
{
    return TRUE;
}

/*
 * A search in a folder comes through the simple engine. This one is for everywhere.
 */
static gboolean
should_search (NautilusSearchProvider *provider,
               NautilusQuery          *query)
{
    g_autoptr (GFile) location = nautilus_query_get_location (query);
    g_autofree char *text = nautilus_query_get_text (query);

    return location == NULL && text != NULL && *text != '\0';
}

static void
start_search (NautilusSearchProvider *provider)
{
    nautilus_search_engine_walker_search (provider, NULL, TRUE);
}

static void
nautilus_search_engine_walker_class_init (NautilusSearchEngineWalkerClass *class)
{
    NautilusSearchProviderClass *provider_class = NAUTILUS_SEARCH_PROVIDER_CLASS (class);

    provider_class->get_name = get_name;
    provider_class->run_in_thread = run_in_thread;
    provider_class->should_search = should_search;
    provider_class->start_search = start_search;
}

static void
nautilus_search_engine_walker_init (NautilusSearchEngineWalker *self)
{
}

NautilusSearchEngineWalker *
nautilus_search_engine_walker_new (void)
{
    return g_object_new (NAUTILUS_TYPE_SEARCH_ENGINE_WALKER, NULL);
}
