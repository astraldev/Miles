# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ekure Edem
#
# Settings for dmgbuild: see make-dmg.sh.

import os.path

application = defines["app"]
name = os.path.basename(application)

format = "ULFO"
files = [application]
symlinks = {"Applications": "/Applications"}
icon = os.path.join(application, "Contents", "Resources", "AppIcon.icns")

background = defines["background"]
window_rect = ((200, 160), (660, 400))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False

icon_size = 128
text_size = 13
arrange_by = None
icon_locations = {name: (165, 190), "Applications": (495, 190)}
