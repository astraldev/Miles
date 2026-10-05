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

/* Folders the sidebar always lists on MacOS, where nothing adds them as bookmarks. */

#include <config.h>
#include "nautilus-mac-places.h"

#include "nautilus-file-utilities.h"

#include <CoreFoundation/CoreFoundation.h>

typedef struct
{
    const char *path;
    GUserDirectory directory;
    const char *icon_name;
} Place;

static const Place places[NAUTILUS_MAC_N_PLACES] =
{
    { "/Applications", G_USER_N_DIRECTORIES, "view-app-grid-symbolic" },
    { NULL, G_USER_DIRECTORY_DOCUMENTS, NULL },
    { NULL, G_USER_DIRECTORY_DOWNLOAD, NULL },
    { NULL, G_USER_DIRECTORY_VIDEOS, NULL },
    { NULL, G_USER_DIRECTORY_MUSIC, NULL },
    { NULL, G_USER_DIRECTORY_PICTURES, NULL },
};

char *
nautilus_mac_place_get_path (guint index)
{
    const char *path;

    g_return_val_if_fail (index < NAUTILUS_MAC_N_PLACES, NULL);

    path = places[index].path;
    if (path == NULL)
    {
        path = g_get_user_special_dir (places[index].directory);
    }

    /* A folder that is not set up points at the home folder. */
    if (path == NULL ||
        g_strcmp0 (path, g_get_home_dir ()) == 0 ||
        !g_file_test (path, G_FILE_TEST_IS_DIR))
    {
        return NULL;
    }

    return g_strdup (path);
}

GIcon *
nautilus_mac_place_get_symbolic_icon (guint index)
{
    g_return_val_if_fail (index < NAUTILUS_MAC_N_PLACES, NULL);

    if (places[index].icon_name != NULL)
    {
        return g_themed_icon_new_with_default_fallbacks (places[index].icon_name);
    }

    return nautilus_special_directory_get_symbolic_icon (places[index].directory);
}

gboolean
nautilus_mac_location_is_place (GFile *location)
{
    for (guint i = 0; i < NAUTILUS_MAC_N_PLACES; i++)
    {
        g_autofree char *path = nautilus_mac_place_get_path (i);

        if (path != NULL)
        {
            g_autoptr (GFile) place = g_file_new_for_path (path);

            if (g_file_equal (location, place))
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

char *
nautilus_mac_get_startup_disk_name (void)
{
    CFURLRef root = CFURLCreateWithFileSystemPath (NULL, CFSTR ("/"), kCFURLPOSIXPathStyle, true);
    CFStringRef name = NULL;
    char buffer[256];
    char *result = NULL;

    if (CFURLCopyResourcePropertyForKey (root, kCFURLVolumeNameKey, &name, NULL) && name != NULL)
    {
        if (CFStringGetCString (name, buffer, sizeof (buffer), kCFStringEncodingUTF8))
        {
            result = g_strdup (buffer);
        }

        CFRelease (name);
    }

    CFRelease (root);

    return result;
}

/* Finder shows the apps of MacOS in Applications. On disk they are apart. */
GFile *
nautilus_mac_get_other_apps_location (GFile    *location,
                                      gboolean *is_system)
{
    const char *folders[] = { "/Applications", "/Applications/Utilities" };
    const char *path = g_file_peek_path (location);

    for (guint i = 0; i < G_N_ELEMENTS (folders); i++)
    {
        g_autofree char *system_folder = g_strconcat ("/System", folders[i], NULL);

        if (g_strcmp0 (path, folders[i]) == 0)
        {
            *is_system = TRUE;

            return g_file_new_for_path (system_folder);
        }
        if (g_strcmp0 (path, system_folder) == 0)
        {
            *is_system = FALSE;

            return g_file_new_for_path (folders[i]);
        }
    }

    return NULL;
}
