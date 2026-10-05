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

static void
on_finder_done (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
    g_autoptr (GTask) task = user_data;
    g_autofree char *message = NULL;
    GError *error = NULL;

    if (!g_subprocess_communicate_utf8_finish (G_SUBPROCESS (source), result, NULL, &message, &error))
    {
        g_task_return_error (task, error);
    }
    else if (g_subprocess_get_successful (G_SUBPROCESS (source)))
    {
        g_task_return_boolean (task, TRUE);
    }
    else
    {
        /*
         * -128 is what AppleScript gives when the user cancels.
         */
        gboolean cancelled = (strstr (message, "(-128)") != NULL);

        g_task_return_new_error (task, G_IO_ERROR,
                                 cancelled ? G_IO_ERROR_CANCELLED : G_IO_ERROR_FAILED,
                                 "%s", g_strstrip (message));
    }
}

/*
 * Finder asks for an administrator's password where the account may not move an app.
 */
void
nautilus_mac_app_trash_with_finder (GFile               *app,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    g_autofree char *path = g_file_get_path (app);
    GTask *task = g_task_new (app, NULL, callback, user_data);
    g_autoptr (GSubprocess) osascript = NULL;
    GError *error = NULL;

    osascript = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                  &error,
                                  "/usr/bin/osascript",
                                  "-e", "on run arguments",
                                  "-e", "tell application \"Finder\" to delete (POSIX file (item 1 of arguments) as alias)",
                                  "-e", "end run",
                                  path,
                                  NULL);
    if (osascript == NULL)
    {
        g_task_return_error (task, error);
        g_object_unref (task);

        return;
    }

    g_subprocess_communicate_utf8_async (osascript, NULL, NULL, on_finder_done, task);
}

gboolean
nautilus_mac_app_trash_with_finder_finish (GAsyncResult  *result,
                                           GError       **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}
