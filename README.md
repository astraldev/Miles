# Files for MacOS

GNOME's file manager, [Files](https://apps.gnome.org/Nautilus/) (also known as Nautilus), ported to MacOS.

This is not a GNOME project. For Files itself, see the [original README](https://gitlab.gnome.org/GNOME/nautilus/-/blob/main/README.md).

Based on Files 51.0.1. It runs from a source build on Apple silicon. There is no app to download yet.

## Credits

- **[Files](https://gitlab.gnome.org/GNOME/nautilus)** by the GNOME project and its contributors. Nearly all of the code here is theirs. GPL 2.0 or later, see [LICENSE](LICENSE).
- **[Yaru](https://github.com/ubuntu/yaru)** icon theme by the Ubuntu community. The icons are bundled with the app, under CC BY-SA 4.0. Their licence and authors files are installed beside them.
- **[gvfs](https://gitlab.gnome.org/GNOME/gvfs)** by the GNOME project, for network folders, the trash and recent files. **[D-Bus](https://www.freedesktop.org/wiki/Software/dbus/)**, which gvfs talks through.
- **[GTK](https://www.gtk.org/)** and **[libadwaita](https://gnome.pages.gitlab.gnome.org/libadwaita/)**, which draw the app.

## Changed Features

What works differently from Files on GNOME.

**Look**

- Icons are Yaru's. Their colour follows the accent colour of MacOS, and they follow light and dark appearance.
- Text is 13 pt.
- Icons in the grid grow by 1.35 per zoom step: 48, 64, 88, 120, 160.

**Sidebar**

- Applications, Documents, Downloads, Movies, Music and Pictures are always listed, in a section of their own.
- The startup disk ("Macintosh HD") has a row, ahead of the other drives.

**Apps**

- An app is one item, with the icon Finder shows for it. The icon follows "Icon & widget style" in System Settings.
- A double-click opens the app. "Show Package Contents" in the menu looks inside it.
- Applications only holds the apps you installed. The apps that come with MacOS are in another folder, and a banner leads there and back. Finder shows the two as one.

**Search**

- Files are found by name, by reading the folders in bulk. No index is used, neither GNOME's nor Spotlight.
- "Search Everywhere" looks through your home folder and the apps.
- An app counts as one item: search does not look inside it.

**Network**

- Servers on the local network are found through Bonjour.
- SFTP, WebDAV, FTP and AFP servers can be opened.

**Trash**

- The trash is the Trash of MacOS, the one Finder shows.
- "Restore" puts a file back where Finder would.

**Permissions**

- A folder MacOS keeps the app out of shows "No Permission" and a button that opens System Settings. It loads by itself once access is given.
- There is no "open as administrator". A folder your account may not read says so.

**Other**

- Preferences opens with Command-comma. The other shortcuts use Control, as on GNOME.
- Archives are extracted by Files itself, also on a double-click.
- The app starts its own D-Bus session bus for gvfs, and stops it on quit.

## Missing Features

What Files on GNOME has and this port does not.

- Search in the contents of files.
- Windows shares (SMB).
- Phones and cameras, Google Drive, OneDrive and other online accounts, NFS.
- Extensions, and with them "Open in Terminal".
- Thumbnails. Pictures and documents show the icon of their type.
- Saved passwords for servers: a server asks for its password every time.
- Opening a folder as administrator.
- Dropping files on a tab.
- An app bundle. It is started from a terminal, so MacOS gives the permissions to the terminal.

Not checked yet: starred files, and what shows when a drive is plugged in.

## Building

Needs Homebrew with GTK 4, libadwaita and the other libraries Files uses. Homebrew's Python has to come first on `PATH`.

```bash
PATH="/opt/homebrew/bin:$PATH" meson setup build --prefix="$PWD/.deps/prefix" \
  -Dextensions=false -Dintrospection=false -Ddocs=false \
  -Dselinux=disabled -Dcloudproviders=disabled -Dtests=none
ninja -C build install
```

The install step also builds gvfs and bundles the icons.

## More

- [port-progress.md](port-progress.md): what is done and decided, and why.
- [todo.md](todo.md): what is left.
