#!/bin/sh
#
# Makes the disk image people install the app from.
# "ninja dmg" runs it, after "ninja app": make-dmg.sh <app name> <version> <build folder>
# Needs dmgbuild: pip install dmgbuild

set -eu

name=$1
version=$2
build_dir=$3
dmg_dir=$(cd "$(dirname "$0")/../dmg" && pwd)

app=$build_dir/$name.app
dmg=$build_dir/$name-$version.dmg
background=$build_dir/dmg-background

if ! command -v dmgbuild > /dev/null 2>&1; then
    echo "dmgbuild not found: pip install dmgbuild" >&2
    exit 1
fi

# One picture for both kinds of screen.
rsvg-convert -w 660 -h 400 "$dmg_dir/background.svg" -o "$background.png"
rsvg-convert -w 1320 -h 800 "$dmg_dir/background.svg" -o "$background@2x.png"
tiffutil -cathidpicheck "$background.png" "$background@2x.png" -out "$background.tiff" 2> /dev/null

rm -f "$dmg"
dmgbuild -s "$dmg_dir/settings.py" -D app="$app" -D background="$background.tiff" "$name" "$dmg"

if [ -n "${CODESIGN_IDENTITY:-}" ]; then
    codesign --force --timestamp --sign "$CODESIGN_IDENTITY" "$dmg"
fi

echo "Made $dmg: $(du -h "$dmg" | cut -f1)"
