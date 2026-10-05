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

#include <config.h>
#include "nautilus-mac-app-data.h"

#include <CoreFoundation/CoreFoundation.h>

/*
 * Where an app keeps its settings and data in ~/Library, under its identifier.
 */
static const struct
{
    const char *folder;
    const char *suffix;
} by_identifier[] =
{
    { "Application Support", "" },
    { "Caches", "" },
    { "Containers", "" },
    { "Cookies", ".binarycookies" },
    { "HTTPStorages", "" },
    { "Logs", "" },
    { "Preferences", ".plist" },
    { "Saved Application State", ".savedState" },
    { "WebKit", "" },
};

/*
 * Some apps use their name there.
 */
static const char *by_name[] = { "Application Support", "Caches", "Logs" };

static char *
get_identifier (const char *app_path)
{
    CFURLRef url = CFURLCreateFromFileSystemRepresentation (NULL, (const UInt8 *) app_path,
                                                            strlen (app_path), true);
    CFDictionaryRef info = CFBundleCopyInfoDictionaryInDirectory (url);
    char buffer[256];
    char *identifier = NULL;

    if (info != NULL)
    {
        CFTypeRef value = CFDictionaryGetValue (info, kCFBundleIdentifierKey);

        if (value != NULL && CFGetTypeID (value) == CFStringGetTypeID () &&
            CFStringGetCString (value, buffer, sizeof (buffer), kCFStringEncodingUTF8))
        {
            identifier = g_strdup (buffer);
        }

        CFRelease (info);
    }

    CFRelease (url);

    return identifier;
}

/*
 * It becomes a file name, and comes from the app: it must not lead out of its folder.
 */
static gboolean
is_identifier (const char *identifier)
{
    for (const char *c = identifier; *c != '\0'; c++)
    {
        if (!g_ascii_isalnum (*c) && strchr ("-_.", *c) == NULL)
        {
            return FALSE;
        }
    }

    return identifier[0] != '.' && strchr (identifier, '.') != NULL &&
           strstr (identifier, "..") == NULL;
}

static GList *
add_if_exists (GList      *data,
               const char *folder,
               const char *name,
               const char *suffix)
{
    g_autofree char *file_name = g_strconcat (name, suffix, NULL);
    g_autofree char *path = g_build_filename (g_get_home_dir (), "Library", folder, file_name, NULL);

    if (g_file_test (path, G_FILE_TEST_EXISTS))
    {
        data = g_list_prepend (data, g_file_new_for_path (path));
    }

    return data;
}

GList *
nautilus_mac_app_get_data (GFile *app)
{
    g_autofree char *path = g_file_get_path (app);
    g_autofree char *identifier = path != NULL ? get_identifier (path) : NULL;
    g_autofree char *name = path != NULL ? g_path_get_basename (path) : NULL;
    GList *data = NULL;

    /*
     * A name that is not an identifier could be the folder of another app.
     */
    if (identifier != NULL && is_identifier (identifier))
    {
        for (guint i = 0; i < G_N_ELEMENTS (by_identifier); i++)
        {
            data = add_if_exists (data, by_identifier[i].folder, identifier, by_identifier[i].suffix);
        }
    }

    if (name != NULL && name[0] != '.' && g_str_has_suffix (name, ".app"))
    {
        name[strlen (name) - strlen (".app")] = '\0';

        for (guint i = 0; i < G_N_ELEMENTS (by_name); i++)
        {
            data = add_if_exists (data, by_name[i], name, "");
        }
    }

    return g_list_reverse (data);
}
