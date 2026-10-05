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

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_MAC_BONJOUR (nautilus_mac_bonjour_get_type ())

G_DECLARE_FINAL_TYPE (NautilusMacBonjour, nautilus_mac_bonjour, NAUTILUS, MAC_BONJOUR, GObject)

NautilusMacBonjour * nautilus_mac_bonjour_new          (void);
gboolean             nautilus_mac_bonjour_is_file_name (const char *name);

G_END_DECLS
