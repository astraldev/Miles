# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ekure Edem

glycin_shim = static_library('glycin-shim',
  'glycin-shim.c',
  dependencies: [
    dependency('gtk4'),
    dependency('gdk-pixbuf-2.0'),
  ],
)

glycin = declare_dependency(
  link_with: glycin_shim,
  include_directories: include_directories('.'),
  dependencies: dependency('gdk-pixbuf-2.0'),
)
glycin_gtk4 = glycin
