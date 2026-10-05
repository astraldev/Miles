#!/bin/sh
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ekure Edem

set -eu

YARU_URL=https://github.com/ubuntu/yaru.git
YARU_COMMIT=7f18973e05607c609c4b972f0dd9bff36fa2a13e

# One per macOS accent colour, see src/macos/nautilus-mac-appearance.c.
YARU_COLOURS="bark blue magenta purple red viridian yellow"

if [ $# -ne 1 ] || [ -z "$1" ]; then
    echo "usage: $0 <icons folder>" >&2
    exit 1
fi

icons_root=${MESON_INSTALL_DESTDIR_PREFIX:+$MESON_INSTALL_DESTDIR_PREFIX/}$1
stamp=$icons_root/.yaru-commit
installed="$YARU_COMMIT $YARU_COLOURS dark"

if [ "$(cat "$stamp" 2>/dev/null)" = "$installed" ]; then
    echo "Yaru icon theme is up to date"
    exit 0
fi

src_dir=${MESON_BUILD_ROOT:-${TMPDIR:-/tmp}}/yaru/src
icons_dir=$src_dir/icons

if [ "$(git -C "$src_dir" rev-parse HEAD 2>/dev/null)" != "$YARU_COMMIT" ]; then
    echo "Fetching the Yaru icon theme ($YARU_COMMIT)"
    rm -rf "$src_dir"
    mkdir -p "$src_dir"
    git -C "$src_dir" init --quiet
    git -C "$src_dir" remote add origin "$YARU_URL"
    git -C "$src_dir" sparse-checkout set icons
    git -C "$src_dir" fetch --quiet --depth 1 --filter=blob:none origin "$YARU_COMMIT"
    git -C "$src_dir" checkout --quiet FETCH_HEAD
fi

install_theme() {
    rm -rf "$icons_root/$1"
    cp -R "$icons_dir/$1" "$icons_root/$1"
    rm -rf "$icons_root/$1/cursors" "$icons_root/$1/cursor.theme"

    python3 "$icons_dir/src/generate-index-theme.py" "$1" \
        --source-dir "$icons_dir/$1" \
        --output-dir "$icons_root/$1" \
        --output-name index.theme \
        --inherits "$2"
}

mkdir -p "$icons_root"
rm -f "$stamp"

# Yaru lacks some generic file icons. Adwaita has them.
install_theme Yaru Adwaita
install_theme Yaru-dark Yaru

for colour in $YARU_COLOURS; do
    install_theme "Yaru-$colour" Yaru
    install_theme "Yaru-$colour-dark" "Yaru-$colour"
done

cp "$icons_dir/LICENSE_CCBYSA" "$icons_dir/COPYING" "$icons_dir/AUTHORS" "$icons_root/Yaru/"

# Last: GTK ignores a cache that is older than its folder.
if command -v gtk4-update-icon-cache >/dev/null 2>&1; then
    for theme_dir in "$icons_root"/Yaru*; do
        gtk4-update-icon-cache -q -f -t "$theme_dir"
    done
fi

echo "$installed" > "$stamp"
echo "Installed the Yaru icon theme in $icons_root"
