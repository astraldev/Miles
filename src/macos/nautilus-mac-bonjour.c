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

/* Finds the file servers on the local network. gvfs does this with avahi, which MacOS lacks. */

#define G_LOG_DOMAIN "nautilus-mac"

#include <config.h>
#include "nautilus-mac-bonjour.h"

#include <dns_sd.h>
#include <glib-unix.h>
#include <SystemConfiguration/SystemConfiguration.h>

#define FILE_NAME_PREFIX "bonjour-"
#define RESTART_DELAY_SECONDS 10

static const struct
{
    const char *service_type;
    const char *scheme;
    guint16 default_port;
} service_types[] =
{
    { "_sftp-ssh._tcp", "sftp", 22 },
    { "_afpovertcp._tcp", "afp", 548 },
    { "_webdav._tcp", "dav", 80 },
    { "_webdavs._tcp", "davs", 443 },
    { "_ftp._tcp", "ftp", 21 },
};

typedef struct
{
    NautilusMacBonjour *bonjour;
    DNSServiceRef ref;
    guint source_id;
} Request;

typedef struct
{
    NautilusMacBonjour *bonjour;
    guint type_index;
    char *key;
    char *display_name;
    /* A server is announced once on each network interface it is seen on. */
    guint n_interfaces;
    Request resolve;
    GFileInfo *info;
} Server;

typedef struct
{
    NautilusMacBonjour *bonjour;
    guint type_index;
    Request browse;
} Browser;

struct _NautilusMacBonjour
{
    GObject parent_instance;

    Browser browsers[G_N_ELEMENTS (service_types)];
    GHashTable *servers;
    guint restart_id;
};

enum
{
    ADDED,
    REMOVED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_FINAL_TYPE (NautilusMacBonjour, nautilus_mac_bonjour, G_TYPE_OBJECT)

static void restart (NautilusMacBonjour *self);

static void
schedule_restart (NautilusMacBonjour *self)
{
    if (self->restart_id == 0)
    {
        self->restart_id = g_timeout_add_seconds_once (RESTART_DELAY_SECONDS,
                                                       (GSourceOnceFunc) restart, self);
    }
}

static gboolean
on_request_ready (int          fd,
                  GIOCondition condition,
                  gpointer     user_data)
{
    Request *request = user_data;

    /* Calls the callback of the request, which may end it. */
    if (DNSServiceProcessResult (request->ref) != kDNSServiceErr_NoError)
    {
        /* The Bonjour service went away: what it told is stale. */
        request->source_id = 0;
        schedule_restart (request->bonjour);

        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void
request_watch (Request *request)
{
    request->source_id = g_unix_fd_add (DNSServiceRefSockFD (request->ref), G_IO_IN,
                                        on_request_ready, request);
}

static void
request_clear (Request *request)
{
    g_clear_handle_id (&request->source_id, g_source_remove);
    g_clear_pointer (&request->ref, DNSServiceRefDeallocate);
}

static void
server_free (Server *server)
{
    request_clear (&server->resolve);
    g_clear_object (&server->info);
    g_free (server->display_name);
    g_free (server->key);
    g_free (server);
}

static GFileInfo *
server_info_new (Server     *server,
                 const char *uri)
{
    GFileInfo *info = g_file_info_new ();
    /* The name goes into a URI and is looked up again: keep it to plain characters. */
    g_autofree char *checksum = g_compute_checksum_for_string (G_CHECKSUM_SHA1, server->key, -1);
    g_autofree char *name = g_strconcat (FILE_NAME_PREFIX, checksum, NULL);
    g_autoptr (GIcon) icon = g_themed_icon_new ("network-server");
    g_autoptr (GIcon) symbolic_icon = g_themed_icon_new ("network-server-symbolic");

    g_file_info_set_name (info, name);
    g_file_info_set_display_name (info, server->display_name);
    g_file_info_set_icon (info, icon);
    g_file_info_set_symbolic_icon (info, symbolic_icon);
    g_file_info_set_content_type (info, "inode/directory");
    g_file_info_set_file_type (info, G_FILE_TYPE_SHORTCUT);
    g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_STANDARD_IS_VIRTUAL, TRUE);
    g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, FALSE);
    g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_RENAME, FALSE);
    g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE, FALSE);
    g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH, FALSE);
    g_file_info_set_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI, uri);

    return info;
}

static gboolean
is_this_mac (const char *host)
{
    CFStringRef local_name = SCDynamicStoreCopyLocalHostName (NULL);
    char own_name[256];
    gboolean is_own = FALSE;

    if (local_name == NULL)
    {
        return FALSE;
    }

    if (CFStringGetCString (local_name, own_name, sizeof (own_name), kCFStringEncodingUTF8))
    {
        g_autofree char *own_host = g_strconcat (own_name, ".local", NULL);

        is_own = (g_ascii_strcasecmp (host, own_host) == 0);
    }

    CFRelease (local_name);

    return is_own;
}

static void DNSSD_API
on_resolved (DNSServiceRef         ref,
             DNSServiceFlags       flags,
             uint32_t              interface_index,
             DNSServiceErrorType   error,
             const char           *full_name,
             const char           *host_target,
             uint16_t              port,
             uint16_t              txt_length,
             const unsigned char  *txt,
             void                 *user_data)
{
    Server *server = user_data;
    g_autofree char *host = NULL;
    g_autofree char *path = NULL;
    g_autofree char *uri = NULL;
    guint16 host_port = g_ntohs (port);
    uint8_t path_length;
    const char *txt_path;
    GList added = { NULL, NULL, NULL };

    if (error != kDNSServiceErr_NoError || server->info != NULL)
    {
        return;
    }

    host = g_strdup (host_target);
    if (g_str_has_suffix (host, "."))
    {
        host[strlen (host) - 1] = '\0';
    }

    if (is_this_mac (host))
    {
        request_clear (&server->resolve);

        return;
    }

    /* WebDAV servers say which folder they share. */
    txt_path = TXTRecordGetValuePtr (txt_length, txt, "path", &path_length);
    if (txt_path != NULL && path_length > 0 && txt_path[0] == '/')
    {
        g_autofree char *announced_path = g_strndup (txt_path, path_length);

        /* It may be escaped already. */
        path = g_uri_escape_string (announced_path,
                                    G_URI_RESERVED_CHARS_ALLOWED_IN_PATH "%", TRUE);
    }

    uri = g_uri_join (G_URI_FLAGS_ENCODED_PATH,
                      service_types[server->type_index].scheme,
                      NULL,
                      host,
                      host_port == service_types[server->type_index].default_port ? -1 : host_port,
                      path != NULL ? path : "/",
                      NULL, NULL);

    g_debug ("Found the server %s at %s", server->display_name, uri);

    server->info = server_info_new (server, uri);
    added.data = server->info;
    g_signal_emit (server->bonjour, signals[ADDED], 0, &added);

    request_clear (&server->resolve);
}

static void DNSSD_API
on_browsed (DNSServiceRef        ref,
            DNSServiceFlags      flags,
            uint32_t             interface_index,
            DNSServiceErrorType  error,
            const char          *service_name,
            const char          *service_type,
            const char          *domain,
            void                *user_data)
{
    Browser *browser = user_data;
    NautilusMacBonjour *self = browser->bonjour;
    g_autofree char *key = NULL;
    Server *server;

    if (error != kDNSServiceErr_NoError)
    {
        return;
    }

    key = g_strconcat (service_name, ".", service_type, domain, NULL);
    server = g_hash_table_lookup (self->servers, key);

    if (flags & kDNSServiceFlagsAdd)
    {
        if (server != NULL)
        {
            server->n_interfaces++;

            return;
        }

        server = g_new0 (Server, 1);
        server->bonjour = self;
        server->resolve.bonjour = self;
        server->type_index = browser->type_index;
        server->key = g_steal_pointer (&key);
        server->display_name = g_strdup (service_name);
        server->n_interfaces = 1;
        g_hash_table_insert (self->servers, server->key, server);

        if (DNSServiceResolve (&server->resolve.ref, 0, interface_index,
                               service_name, service_type, domain,
                               on_resolved, server) == kDNSServiceErr_NoError)
        {
            request_watch (&server->resolve);
        }
    }
    else if (server != NULL && --server->n_interfaces == 0)
    {
        if (server->info != NULL)
        {
            GList removed = { server->info, NULL, NULL };

            g_debug ("The server %s is gone", server->display_name);
            g_signal_emit (self, signals[REMOVED], 0, &removed);
        }

        g_hash_table_remove (self->servers, key);
    }
}

static void
start_browsing (NautilusMacBonjour *self)
{
    for (guint i = 0; i < G_N_ELEMENTS (service_types); i++)
    {
        Browser *browser = &self->browsers[i];

        browser->bonjour = self;
        browser->type_index = i;
        browser->browse.bonjour = self;

        if (DNSServiceBrowse (&browser->browse.ref, 0, kDNSServiceInterfaceIndexAny,
                              service_types[i].service_type, NULL,
                              on_browsed, browser) == kDNSServiceErr_NoError)
        {
            request_watch (&browser->browse);
        }
        else
        {
            g_debug ("Could not look for %s servers", service_types[i].scheme);
            schedule_restart (self);
        }
    }
}

static void
stop_browsing (NautilusMacBonjour *self)
{
    for (guint i = 0; i < G_N_ELEMENTS (service_types); i++)
    {
        request_clear (&self->browsers[i].browse);
    }
}

static void
restart (NautilusMacBonjour *self)
{
    GHashTableIter iter;
    Server *server;
    g_autoptr (GList) removed = NULL;

    self->restart_id = 0;

    g_hash_table_iter_init (&iter, self->servers);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &server))
    {
        if (server->info != NULL)
        {
            removed = g_list_prepend (removed, server->info);
        }
    }

    if (removed != NULL)
    {
        g_signal_emit (self, signals[REMOVED], 0, removed);
    }

    g_hash_table_remove_all (self->servers);
    stop_browsing (self);
    start_browsing (self);
}

static void
nautilus_mac_bonjour_dispose (GObject *object)
{
    NautilusMacBonjour *self = NAUTILUS_MAC_BONJOUR (object);

    g_clear_handle_id (&self->restart_id, g_source_remove);
    stop_browsing (self);
    g_clear_pointer (&self->servers, g_hash_table_unref);

    G_OBJECT_CLASS (nautilus_mac_bonjour_parent_class)->dispose (object);
}

static void
nautilus_mac_bonjour_class_init (NautilusMacBonjourClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = nautilus_mac_bonjour_dispose;

    /* Both signals carry a GList of GFileInfo, as those of NautilusRecentServers do. */
    signals[ADDED] = g_signal_new ("added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                   0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
    signals[REMOVED] = g_signal_new ("removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                     0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void
nautilus_mac_bonjour_init (NautilusMacBonjour *self)
{
    self->servers = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           NULL, (GDestroyNotify) server_free);

    start_browsing (self);
}

NautilusMacBonjour *
nautilus_mac_bonjour_new (void)
{
    return g_object_new (NAUTILUS_TYPE_MAC_BONJOUR, NULL);
}

gboolean
nautilus_mac_bonjour_is_file_name (const char *name)
{
    return name != NULL && g_str_has_prefix (name, FILE_NAME_PREFIX);
}
