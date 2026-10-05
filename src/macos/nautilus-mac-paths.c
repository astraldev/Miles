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
#include "nautilus-mac-paths.h"

#include <CoreFoundation/CoreFoundation.h>
#include <glib/gi18n.h>

static char *
get_app_resources (void)
{
    CFBundleRef bundle = CFBundleGetMainBundle ();
    CFURLRef url = bundle != NULL ? CFBundleCopyResourcesDirectoryURL (bundle) : NULL;
    char path[PATH_MAX];
    gboolean found;

    if (url == NULL)
    {
        return NULL;
    }

    found = CFURLGetFileSystemRepresentation (url, true, (UInt8 *) path, sizeof (path));
    CFRelease (url);

    return found ? g_strdup (path) : NULL;
}

const char *
nautilus_mac_get_prefix (void)
{
    static const char *prefix = NULL;

    if (g_once_init_enter_pointer (&prefix))
    {
        g_autofree char *resources = get_app_resources ();
        g_autofree char *data_dir = g_build_filename (resources != NULL ? resources : "",
                                                      &NAUTILUS_DATADIR[strlen (NAUTILUS_PREFIX)],
                                                      NULL);
        gboolean is_carried = (resources != NULL && g_file_test (data_dir, G_FILE_TEST_IS_DIR));

        g_once_init_leave_pointer (&prefix,
                                   is_carried ? g_steal_pointer (&resources) : NAUTILUS_PREFIX);
    }

    return prefix;
}

char *
nautilus_mac_get_install_path (const char *built_in_path)
{
    g_return_val_if_fail (g_str_has_prefix (built_in_path, NAUTILUS_PREFIX), NULL);

    return g_build_filename (nautilus_mac_get_prefix (),
                             built_in_path + strlen (NAUTILUS_PREFIX),
                             NULL);
}

/*
 * Call first in main().
 */
void
nautilus_mac_paths_init (void)
{
    g_autofree char *nautilus_data_dir = nautilus_mac_get_install_path (NAUTILUS_DATADIR);
    g_autofree char *data_dir = g_path_get_dirname (nautilus_data_dir);
    g_autofree char *locale_dir = nautilus_mac_get_install_path (LOCALEDIR);
    g_autofree char *with_system = g_strconcat (data_dir, G_SEARCHPATH_SEPARATOR_S,
                                                NAUTILUS_MACOS_SYSTEM_DATA_DIRS, NULL);
    const char *prefix = nautilus_mac_get_prefix ();
    gboolean is_carried = !g_str_equal (prefix, NAUTILUS_PREFIX);

    /*
     * Started from Finder, nothing says where schemas and icons are.
     */
    g_setenv ("XDG_DATA_DIRS", is_carried ? data_dir : with_system, is_carried);

    bindtextdomain (GETTEXT_PACKAGE, locale_dir);

    if (is_carried)
    {
        g_autofree char *gio_modules = nautilus_mac_get_install_path (NAUTILUS_GIO_MODULE_DIR);
        g_autofree char *image_loaders = g_build_filename (prefix, "lib", "gdk-pixbuf-2.0",
                                                           "loaders.cache", NULL);

        /*
         * Not Homebrew's modules, on a Mac that has it.
         */
        g_setenv ("GIO_MODULE_DIR", gio_modules, TRUE);
        g_setenv ("GDK_PIXBUF_MODULE_FILE", image_loaders, TRUE);
        g_setenv ("GTK_EXE_PREFIX", prefix, TRUE);
        g_setenv ("GTK_DATA_PREFIX", prefix, TRUE);
    }
}
