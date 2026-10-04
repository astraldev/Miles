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

/* Folders the sidebar always lists on macOS, where nothing adds them as bookmarks. */

#include <config.h>
#include "nautilus-mac-places.h"

#include "nautilus-file-utilities.h"

typedef struct
{
    /* A fixed path, or NULL to ask for @directory. */
    const char *path;
    GUserDirectory directory;
    /* An icon name, or NULL to use the icon of @directory. */
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

/* Returns: (nullable) (transfer full): path of the folder at @index, NULL if this Mac lacks it. */
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

/* Returns: (transfer full): the icon for the folder at @index. */
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

/* Returns: whether @location is one of the folders the sidebar always lists. */
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
