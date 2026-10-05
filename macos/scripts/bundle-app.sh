#!/bin/sh
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ekure Edem
#
# Contents/Resources is laid out as the prefix is.

set -eu

prefix=$1
name=$2
build_dir=$3
source_dir=$(cd "$(dirname "$0")/../.." && pwd)
brew_prefix=$(brew --prefix)

app=$build_dir/$name.app
resources=$app/Contents/Resources
libs=$resources/lib
share=$resources/share
licences=$resources/licenses
notices=$licences/THIRD-PARTY-NOTICES.md

# A line for each program and library: the file, where it came from, its path to the libraries.
queue=$(mktemp)
references=$(mktemp)
errors=$(mktemp)
trap 'rm -f "$queue" "$references" "$errors"' EXIT

add_file () {
    cp "$2" "$1"
    chmod u+w "$1"
    printf '%s\t%s\t%s\n' "$1" "$2" "${3:-}" >> "$queue"
}

copy_licences () {
    mkdir -p "$licences/$2"
    find "$1" -maxdepth 1 -type f \
         \( -iname 'LICEN[CS]E*' -o -iname 'COPYING*' -o -iname 'COPYRIGHT*' -o -iname 'NOTICE*' \
            -o -iname '*GPL*' -o -iname 'MIT*' -o -iname 'BSD*' -o -iname 'AUTHORS*' \) \
         -exec cp {} "$licences/$2/" \;
}

add_notice () {
    printf '| %s | %s | %s | %s |\n' "$1" "$2" "$3" "$4" >> "$notices"
}

# A field of a Homebrew formula, which may go on over several lines.
formula_field () {
    awk -v field="$1" '
        !found && $1 == field && /^  [a-z]/ { found = 1; sub(/^  [a-z]+ /, "") }
        found {
            sub(/ *#.*/, "")
            printf "%s", $0
            depth += gsub(/[\[{(]/, "&") - gsub(/[\]})]/, "&")
            if (depth <= 0 && $0 !~ /,$/) exit
        }
    ' "$2" | tr -d '"' | tr -s ' '
}

copy_brew_licences () {
    case $1 in
        "$brew_prefix"/Cellar/*) ;;
        *) return ;;
    esac

    package=${1#"$brew_prefix"/Cellar/}
    version=${package#*/}
    package=${package%%/*}
    version=${version%%/*}
    formula=$brew_prefix/Cellar/$package/$version/.brew/$package.rb

    if [ -d "$licences/$package-$version" ]; then
        return
    fi

    copy_licences "$brew_prefix/Cellar/$package/$version" "$package-$version"
    if [ -f "$formula" ]; then
        cp "$formula" "$licences/$package-$version/"
        add_notice "$package" "$version" "$(formula_field license "$formula")" "$(formula_field url "$formula")"
    else
        add_notice "$package" "$version" "see its folder" "https://formulae.brew.sh/formula/$package"
    fi
}

wrap_field () {
    sed -n "s/^$2 = //p" "$source_dir/subprojects/$1.wrap" | head -1
}

script_field () {
    sed -n "s/^$2=//p" "$source_dir/macos/scripts/$1" | head -1
}

find_library () {
    case $1 in
        @*)
            for dir in "$2" "$prefix/lib" "$prefix/lib/gvfs" "$brew_prefix/lib"; do
                if [ -e "$dir/${1#@*/}" ]; then
                    echo "$dir/${1#@*/}"
                    return
                fi
            done
            ;;
        *)
            echo "$1"
            ;;
    esac
}

echo "Copying the app's files"

rm -rf "$app"
cp -R "$prefix/Applications/$name.app" "$app"
mkdir -p "$resources/bin" "$resources/libexec" "$libs/gio/modules" "$libs/gdk-pixbuf-2.0" \
         "$share/dbus-1/services" "$share/glib-2.0/schemas" "$share/icons" "$licences"

app_version=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$app/Contents/Info.plist")
commit=$(git -C "$source_dir" rev-parse HEAD 2> /dev/null || echo "see the release")
sed -e "s/@NAME@/$name/g" -e "s/@VERSION@/$app_version/g" -e "s/@COMMIT@/$commit/g" \
    "$source_dir/macos/bundle/THIRD-PARTY-NOTICES.md.in" > "$notices"

copy_licences "$source_dir" nautilus
copy_licences "$build_dir/gvfs/src" gvfs
for subproject in dbus gnome-desktop libportal; do
    copy_licences "$source_dir/subprojects/$subproject" "$subproject"
done
# libgxdp names its licence, and has no copy of it.
mkdir -p "$licences/libgxdp"
cp "$source_dir/libnautilus-extension/LICENSE" "$licences/libgxdp/LGPL-2.1.txt"

add_notice "$name (Nautilus)" "$app_version" "GPL-3.0-or-later" "https://github.com/astraldev/Miles at $commit"
add_notice "gvfs" "$(script_field install-gvfs.sh GVFS_COMMIT)" \
           "LGPL-2.0-or-later, its trash backend GPL-3.0-only" \
           "$(script_field install-gvfs.sh GVFS_URL), with macos/patches"
add_notice "dbus" "$(wrap_field dbus revision)" "AFL-2.1 OR GPL-2.0-or-later" "$(wrap_field dbus url)"
add_notice "gnome-desktop" "$(wrap_field gnome-desktop revision)" "GPL-2.0-or-later AND LGPL-2.1-or-later" \
           "$(wrap_field gnome-desktop url), with subprojects/packagefiles"
add_notice "libgxdp" "$(wrap_field libgxdp revision)" "LGPL-2.1-or-later" "$(wrap_field libgxdp url)"
add_notice "libportal" "$(wrap_field libportal revision)" "LGPL-3.0-only" \
           "$(wrap_field libportal url), with subprojects/packagefiles"
add_notice "Yaru Icons" "$(script_field install-yaru.sh YARU_COMMIT)" "CC-BY-SA-4.0" \
           "$(script_field install-yaru.sh YARU_URL), see http://snwh.org/"

add_file "$app/Contents/MacOS/nautilus" "$prefix/bin/nautilus" "@executable_path/../Resources/lib"
add_file "$resources/bin/dbus-daemon" "$prefix/bin/dbus-daemon" "@executable_path/../lib"
for daemon in "$prefix"/libexec/gvfsd*; do
    add_file "$resources/libexec/$(basename "$daemon")" "$daemon" "@executable_path/../lib"
done
for module in "$prefix"/lib/gio/modules/*.dylib "$brew_prefix/opt/glib-networking/lib/gio/modules/libgiognutls.so"; do
    add_file "$libs/gio/modules/$(basename "$module")" "$module"
done
copy_brew_licences "$(realpath "$brew_prefix/opt/glib-networking")/"

# Empty, or Homebrew's image loaders are used on a Mac that has it.
: > "$libs/gdk-pixbuf-2.0/loaders.cache"

# tinysparql loads its parser from the folder it was built for, and has no setting for it.
add_file "$libs/libtracker-parser-libicu.so" "$brew_prefix/opt/tinysparql/lib/tinysparql-3.0/libtracker-parser-libicu.so"

cp -R "$prefix/share/nautilus" "$prefix/share/gvfs" "$prefix/share/locale" "$share/"
cp "$prefix"/share/dbus-1/services/org.gtk.vfs.*.service "$share/dbus-1/services/"
cp "$prefix"/share/glib-2.0/schemas/*.xml \
   "$brew_prefix"/opt/gtk4/share/glib-2.0/schemas/org.gtk.gtk4.Settings.*.xml \
   "$brew_prefix"/opt/gsettings-desktop-schemas/share/glib-2.0/schemas/*.xml \
   "$share/glib-2.0/schemas/"
glib-compile-schemas "$share/glib-2.0/schemas" 2>/dev/null

# The TLS library looks for these in Homebrew's folder: gvfs is told where they are.
cp "$brew_prefix/opt/ca-certificates/share/ca-certificates/cacert.pem" "$share/nautilus/certificates.pem"
copy_brew_licences "$(realpath "$brew_prefix/opt/ca-certificates")/"

# Yaru falls back to these for the icons it lacks.
for theme in adwaita-icon-theme/share/icons/Adwaita hicolor-icon-theme/share/icons/hicolor; do
    cp -RL "$brew_prefix/opt/$theme" "$share/icons/"
    copy_brew_licences "$(realpath "$brew_prefix/opt/${theme%%/*}")/"
done
rm -rf "$share/icons/Adwaita/cursors"
copy_brew_licences "$(realpath "$brew_prefix/opt/gsettings-desktop-schemas")/"

echo "Copying the libraries"

line_number=0
while line_number=$((line_number + 1)) && line=$(sed -n "${line_number}p" "$queue") && [ -n "$line" ]; do
    file=$(printf '%s' "$line" | cut -f1)
    original=$(printf '%s' "$line" | cut -f2)
    rpath=$(printf '%s' "$line" | cut -f3)
    own_name=$(otool -D "$original" | sed -n 2p)

    set --
    if [ -n "$own_name" ]; then
        set -- -id "@rpath/$(basename "$file")"
    fi
    if [ -n "$rpath" ]; then
        set -- "$@" -add_rpath "$rpath"
    fi

    otool -l "$original" | grep -A2 LC_RPATH | sed -n 's/^ *path \(.*\) (offset.*/\1/p' | sort -u > "$references"
    while IFS= read -r old_rpath; do
        set -- "$@" -delete_rpath "$old_rpath"
    done < "$references"

    otool -L "$original" | sed 1d | sed 's/^[[:space:]]*//; s/ (compatibility.*//' > "$references"
    while IFS= read -r reference; do
        case $reference in
            /usr/lib/*|/System/*) continue ;;
        esac
        if [ "$reference" = "$own_name" ]; then
            continue
        fi

        library=$(find_library "$reference" "$(dirname "$original")")
        if [ -z "$library" ] || [ ! -e "$library" ]; then
            echo "$original needs $reference, which was not found" >&2
            exit 1
        fi
        library=$(realpath "$library")
        library_name=$(basename "$(otool -D "$library" | sed -n 2p)")

        if [ ! -e "$libs/$library_name" ]; then
            add_file "$libs/$library_name" "$library"
            copy_brew_licences "$library"
        fi
        set -- "$@" -change "$reference" "@rpath/$library_name"
    done < "$references"

    if [ $# -gt 0 ] && ! install_name_tool "$@" "$file" 2> "$errors"; then
        cat "$errors" >&2
        exit 1
    fi
done

# The path is in the library: replaced by one of the same length, found next to the libraries.
parser_path=$(strings "$libs/libtinysparql-3.0.0.dylib" | grep '/tinysparql-3.0/%s$')
PARSER_PATH=$parser_path perl -pi -e \
    's/\Q$ENV{PARSER_PATH}\E/"\@rpath\/%s" . "\0" x (length($ENV{PARSER_PATH}) - 9)/e' \
    "$libs/libtinysparql-3.0.0.dylib"

echo "Signing"

# Changing a file breaks its signature. Without CODESIGN_IDENTITY the signature is ad hoc.
if [ -n "${CODESIGN_IDENTITY:-}" ]; then
    set -- --options runtime --timestamp --entitlements "$source_dir/macos/bundle/nautilus.entitlements" \
           --sign "$CODESIGN_IDENTITY"
else
    set -- --sign -
fi
cut -f1 "$queue" | while IFS= read -r file; do
    if ! codesign --force "$@" "$file" 2> "$errors"; then
        cat "$errors" >&2
        exit 1
    fi
done
codesign --force "$@" "$app"

echo "Made $app: $((line_number - 1)) programs and libraries, $(du -sh "$app" | cut -f1)"
