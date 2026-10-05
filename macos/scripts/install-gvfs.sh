#!/bin/sh
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ekure Edem
#
# Builds gvfs into the prefix on macOS, for network locations.
# Meson runs it at install. By hand: install-gvfs.sh <prefix>

set -eu

GVFS_URL=https://gitlab.gnome.org/GNOME/gvfs.git
# 1.62.0
GVFS_COMMIT=046d5423ef100255f554eff84388f8ba765a2182

# Off: what needs Linux, or a library that macOS or Homebrew lacks.
GVFS_OPTIONS="
    -Dsystemduserunitdir=no -Dtmpfilesdir=no
    -Dadmin=false -Dafc=false -Dafp=true -Darchive=false -Dcdda=false
    -Ddnssd=false -Dgoa=false -Dgoogle=false -Dgphoto2=false -Dhttp=true -Dmtp=false
    -Dnfs=false -Donedrive=false -Dsftp=true -Dsmb=false -Dudisks2=false -Dwsdd=false
    -Dbluray=false -Dfuse=false -Dgcr=false -Dgcrypt=true -Dgudev=false -Dkeyring=false
    -Dlogind=false -Dlibusb=false -Dman=false"

if [ -n "${MESON_INSTALL_PREFIX:-}" ]; then
    prefix=$MESON_INSTALL_PREFIX
    installed_prefix=$MESON_INSTALL_DESTDIR_PREFIX
elif [ $# -eq 1 ] && [ -n "$1" ]; then
    prefix=$1
    installed_prefix=$1
else
    echo "usage: $0 <prefix>" >&2
    exit 1
fi

patches_dir=$(cd "$(dirname "$0")/../patches" && pwd)

stamp=$installed_prefix/share/nautilus/.gvfs-commit
# The stamp also names the options and the patches, so changing them rebuilds.
installed="$GVFS_COMMIT $(echo $GVFS_OPTIONS) $(cat "$patches_dir"/gvfs-* | cksum)"

if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$installed" ]; then
    echo "gvfs is up to date"
    exit 0
fi

work_dir=${MESON_BUILD_ROOT:-${TMPDIR:-/tmp}}/gvfs
src_dir=$work_dir/src
build_dir=$work_dir/build

if [ "$(git -C "$src_dir" rev-parse HEAD 2>/dev/null)" != "$GVFS_COMMIT" ]; then
    echo "Fetching gvfs ($GVFS_COMMIT)"
    rm -rf "$src_dir" "$build_dir"
    mkdir -p "$src_dir"
    git -C "$src_dir" init --quiet
    git -C "$src_dir" remote add origin "$GVFS_URL"
    git -C "$src_dir" fetch --quiet --depth 1 origin "$GVFS_COMMIT"
    git -C "$src_dir" checkout --quiet FETCH_HEAD
fi

git -C "$src_dir" reset --quiet --hard
git -C "$src_dir" clean --quiet -fd
for patch in "$patches_dir"/gvfs-*.patch; do
    git -C "$src_dir" apply "$patch"
done
cp "$patches_dir/gvfs-trashmac.h" "$src_dir/daemon/trashlib/trashmac.h"

# For the dbus that was just installed there.
export PKG_CONFIG_PATH="$installed_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

echo "Building gvfs"
rm -rf "$build_dir"
# shellcheck disable=SC2086
meson setup "$build_dir" "$src_dir" --prefix="$prefix" --buildtype=release $GVFS_OPTIONS
meson install -C "$build_dir"

# A backend for gvfs's own tests.
rm -f "$installed_prefix/share/gvfs/mounts/localtest.mount" \
      "$installed_prefix/libexec/gvfsd-localtest"

mkdir -p "$(dirname "$stamp")"
echo "$installed" > "$stamp"
echo "Installed gvfs in $installed_prefix"
