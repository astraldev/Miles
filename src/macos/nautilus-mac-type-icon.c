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
#include "nautilus-mac-type-icon.h"

/* GIO makes up an icon name from the type MacOS gives. For these the theme has
 * the icon under another name. */
static const struct
{
    const char *content_type;
    const char *icon_name;
} type_icons[] =
{
    { "com.apple.log", "text-x-log" },
    { "com.netscape.javascript-source", "text-x-javascript" },
    { "com.sun.java-source", "text-x-java" },
    { "public.c-header", "text-x-chdr" },
    { "public.c-plus-plus-header", "text-x-c++hdr" },
    { "public.c-plus-plus-source", "text-x-c++src" },
    { "public.c-source", "text-x-csrc" },
    { "public.objective-c-source", "text-x-csrc" },
    { "public.patch-file", "text-x-patch" },
    { "public.perl-script", "text-x-script" },
    { "public.php-script", "text-x-php" },
    { "public.plain-text", "text-plain" },
    { "public.python-script", "text-x-python" },
    { "public.ruby-script", "text-x-ruby" },
    { "public.shell-script", "application-x-shellscript" },
    { "public.swift-source", "text-x-script" },
    { "public.text", "text-plain" },
    { "public.utf8-plain-text", "text-plain" },
};

GIcon *
nautilus_mac_get_type_icon (const char *content_type)
{
    for (guint i = 0; i < G_N_ELEMENTS (type_icons); i++)
    {
        if (g_str_equal (content_type, type_icons[i].content_type))
        {
            const char *names[] = { type_icons[i].icon_name, "text-x-generic" };

            return g_themed_icon_new_from_names ((char **) names, G_N_ELEMENTS (names));
        }
    }

    return NULL;
}
