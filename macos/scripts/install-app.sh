#!/bin/sh
#
# Makes the app around the installed nautilus, in <prefix>/Applications.
# Meson runs it at install: install-app.sh <app name>
# The app runs in place: it uses the libraries and data of the prefix it was built for.

set -eu

name=$1
prefix=$MESON_INSTALL_DESTDIR_PREFIX
app=$prefix/Applications/$name.app
icon_svg=$MESON_SOURCE_ROOT/macos/bundle/AppIcon.svg

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

cp "$prefix/bin/nautilus" "$app/Contents/MacOS/nautilus"
cp "$MESON_BUILD_ROOT/Info.plist" "$app/Contents/Info.plist"

if command -v rsvg-convert >/dev/null 2>&1; then
    iconset=$MESON_BUILD_ROOT/AppIcon.iconset

    rm -rf "$iconset"
    mkdir -p "$iconset"
    for size in 16 32 128 256 512; do
        rsvg-convert -w "$size" -h "$size" "$icon_svg" -o "$iconset/icon_${size}x${size}.png"
        rsvg-convert -w "$((size * 2))" -h "$((size * 2))" "$icon_svg" -o "$iconset/icon_${size}x${size}@2x.png"
    done
    iconutil -c icns "$iconset" -o "$app/Contents/Resources/AppIcon.icns"
else
    echo "rsvg-convert not found: $name.app has no icon" >&2
fi

# Signed for this Mac only. macOS ties the permissions it gives to the signature.
codesign --force --sign - "$app"

echo "Made $app"
