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

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_MAC_APP_ICON (nautilus_mac_app_icon_get_type ())

G_DECLARE_FINAL_TYPE (NautilusMacAppIcon, nautilus_mac_app_icon, NAUTILUS, MAC_APP_ICON, GObject)

GIcon * nautilus_mac_app_icon_new (GFile *location);

G_END_DECLS
