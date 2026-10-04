#!/bin/sh
#
# Bundles the Yaru icon theme into the app's own data folder on macOS.
# Meson runs it at install. By hand: install-yaru.sh <prefix>/share/nautilus/icons
# It copies the rendered icons, since Yaru's own build cannot install icons alone.

set -eu

YARU_URL=https://github.com/ubuntu/yaru.git
YARU_COMMIT=7f18973e05607c609c4b972f0dd9bff36fa2a13e

# Colour variants for the macOS accent colour, see src/mac/nautilus-mac-appearance.c.
YARU_VARIANTS="bark blue magenta purple red viridian yellow"

if [ $# -ne 1 ] || [ -z "$1" ]; then
    echo "usage: $0 <icons folder>" >&2
    exit 1
fi

if [ -n "${MESON_INSTALL_DESTDIR_PREFIX:-}" ]; then
    icons_root=$MESON_INSTALL_DESTDIR_PREFIX/$1
else
    icons_root=$1
fi

theme_dir=$icons_root/Yaru
stamp=$theme_dir/.yaru-commit
# The stamp also names the variants, so changing the list reinstalls.
installed="$YARU_COMMIT $YARU_VARIANTS"

if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$installed" ]; then
    echo "Yaru icon theme is up to date"
    exit 0
fi

# Keep the download next to the build, so it is only fetched once.
work_dir=${MESON_BUILD_ROOT:-${TMPDIR:-/tmp}}/yaru
src_dir=$work_dir/src

if [ "$(git -C "$src_dir" rev-parse HEAD 2>/dev/null)" != "$YARU_COMMIT" ]; then
    echo "Fetching the Yaru icon theme ($YARU_COMMIT)"
    rm -rf "$src_dir"
    mkdir -p "$src_dir"
    git -C "$src_dir" init --quiet
    git -C "$src_dir" remote add origin "$YARU_URL"
    # Only the icons are needed, and only at this one commit.
    git -C "$src_dir" sparse-checkout set icons
    git -C "$src_dir" fetch --quiet --depth 1 --filter=blob:none origin "$YARU_COMMIT"
    git -C "$src_dir" checkout --quiet FETCH_HEAD
fi

icons_dir=$src_dir/icons

rm -rf "$theme_dir"
mkdir -p "$icons_root"
cp -R "$icons_dir/Yaru" "$theme_dir"

# The cursors are for X11 and Wayland.
rm -rf "$theme_dir/cursors" "$theme_dir/cursor.theme"

# Yaru only has some of the generic file icons. Adwaita has the rest.
python3 "$icons_dir/src/generate-index-theme.py" Yaru \
    --source-dir "$icons_dir/Yaru" \
    --output-dir "$theme_dir" \
    --output-name index.theme \
    --inherits Adwaita

# The icons are CC BY-SA 4.0, the scripts GPL 3: ship the licences with them.
cp "$icons_dir/LICENSE_CCBYSA" "$icons_dir/COPYING" "$icons_dir/AUTHORS" "$theme_dir/"

for variant in $YARU_VARIANTS; do
    variant_dir=$icons_root/Yaru-$variant

    rm -rf "$variant_dir"
    cp -R "$icons_dir/Yaru-$variant" "$variant_dir"

    python3 "$icons_dir/src/generate-index-theme.py" "Yaru-$variant" \
        --source-dir "$icons_dir/Yaru-$variant" \
        --output-dir "$variant_dir" \
        --output-name index.theme \
        --inherits Yaru
done

if command -v gtk4-update-icon-cache >/dev/null 2>&1; then
    gtk4-update-icon-cache -q -f -t "$theme_dir"
    for variant in $YARU_VARIANTS; do
        gtk4-update-icon-cache -q -f -t "$icons_root/Yaru-$variant"
    done
fi

echo "$installed" > "$stamp"
echo "Installed the Yaru icon theme in $theme_dir"
