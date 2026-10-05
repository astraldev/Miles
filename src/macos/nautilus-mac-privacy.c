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

#include <config.h>
#include "nautilus-mac-privacy.h"

#include <dirent.h>
#include <errno.h>

#define SETTINGS_URL "x-apple.systempreferences:com.apple.preference.security?"

static char *
get_folder_path (GFile *location)
{
    if (g_file_has_uri_scheme (location, "trash"))
    {
        return g_build_filename (g_get_home_dir (), ".Trash", NULL);
    }

    return g_file_get_path (location);
}

static int
get_open_error (GFile *location)
{
    g_autofree char *path = get_folder_path (location);
    DIR *dir;

    if (path == NULL)
    {
        return 0;
    }

    dir = opendir (path);
    if (dir != NULL)
    {
        closedir (dir);

        return 0;
    }

    return errno;
}

/* MacOS privacy gives EPERM, file permissions give EACCES. */
gboolean
nautilus_mac_location_is_blocked (GFile *location)
{
    return get_open_error (location) == EPERM;
}

gboolean
nautilus_mac_location_is_denied (GFile *location)
{
    return get_open_error (location) == EACCES;
}

/* The folders MacOS lists one by one. The rest need Full Disk Access. */
static gboolean
is_listed_folder (const char *path)
{
    const GUserDirectory directories[] =
    {
        G_USER_DIRECTORY_DESKTOP,
        G_USER_DIRECTORY_DOCUMENTS,
        G_USER_DIRECTORY_DOWNLOAD,
    };

    if (g_str_has_prefix (path, "/Volumes/"))
    {
        return TRUE;
    }

    for (guint i = 0; i < G_N_ELEMENTS (directories); i++)
    {
        const char *directory = g_get_user_special_dir (directories[i]);

        if (directory != NULL && g_str_has_prefix (path, directory) &&
            (path[strlen (directory)] == '\0' || path[strlen (directory)] == '/'))
        {
            return TRUE;
        }
    }

    return FALSE;
}

/* The folders MacOS asks about when an app opens them: looking inside is left to the user. */
gboolean
nautilus_mac_location_is_guarded (GFile *location)
{
    const GUserDirectory directories[] =
    {
        G_USER_DIRECTORY_DESKTOP,
        G_USER_DIRECTORY_DOCUMENTS,
        G_USER_DIRECTORY_DOWNLOAD,
    };
    const char *guarded[] = { ".Trash", "Library/Mobile Documents" };
    const char *guarded_parents[] =
    {
        "Library/CloudStorage/", "Library/Containers/", "Library/Group Containers/",
        "Library/Mobile Documents/",
    };
    const char *libraries[] = { ".photoslibrary", ".musiclibrary", ".tvlibrary" };
    const char *path = g_file_peek_path (location);
    const char *home = g_get_home_dir ();
    const char *in_home;

    if (path == NULL)
    {
        return FALSE;
    }

    for (guint i = 0; i < G_N_ELEMENTS (directories); i++)
    {
        if (g_strcmp0 (path, g_get_user_special_dir (directories[i])) == 0)
        {
            return TRUE;
        }
    }

    for (guint i = 0; i < G_N_ELEMENTS (libraries); i++)
    {
        if (g_str_has_suffix (path, libraries[i]))
        {
            return TRUE;
        }
    }

    if (g_str_has_prefix (path, "/Volumes/"))
    {
        return strchr (path + strlen ("/Volumes/"), '/') == NULL;
    }

    if (!g_str_has_prefix (path, home) || path[strlen (home)] != '/')
    {
        return FALSE;
    }

    in_home = path + strlen (home) + 1;

    for (guint i = 0; i < G_N_ELEMENTS (guarded); i++)
    {
        if (g_str_equal (in_home, guarded[i]))
        {
            return TRUE;
        }
    }

    for (guint i = 0; i < G_N_ELEMENTS (guarded_parents); i++)
    {
        if (g_str_has_prefix (in_home, guarded_parents[i]))
        {
            return strchr (in_home + strlen (guarded_parents[i]), '/') == NULL;
        }
    }

    return FALSE;
}

void
nautilus_mac_open_privacy_settings (GFile *location)
{
    g_autofree char *path = get_folder_path (location);
    const char *url = SETTINGS_URL "Privacy_AllFiles";
    g_autoptr (GError) error = NULL;

    if (path != NULL && is_listed_folder (path))
    {
        url = SETTINGS_URL "Privacy_FilesAndFolders";
    }

    if (!g_spawn_async (NULL, (char *[]) { "/usr/bin/open", (char *) url, NULL }, NULL,
                        G_SPAWN_DEFAULT, NULL, NULL, NULL, &error))
    {
        g_warning ("Could not open System Settings: %s", error->message);
    }
}
