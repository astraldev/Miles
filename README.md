# Files for MacOS

GNOME's file manager, [Files](https://apps.gnome.org/Nautilus/) (also known as Nautilus), ported to MacOS.

This is not a GNOME project. For Files itself, see the [original README](https://gitlab.gnome.org/GNOME/nautilus/-/blob/main/README.md).

Based on Files 51.0.1. It runs from a source build on Apple silicon. There is no app to download yet.

## Credits

- **[Yaru](https://github.com/ubuntu/yaru)** icon theme by the Ubuntu community. The icons are bundled with the app, under CC BY-SA 4.0.

## Changed Features

What works differently from Files on GNOME.

- Icons are Yaru's, and follow the accent colour and the light or dark appearance of MacOS.
- The sidebar always lists Applications, Documents, Downloads, Movies, Music and Pictures, the startup disk, and the folders of iCloud Drive, Google Drive and OneDrive when their apps are set up.
- An app is one item with its own icon. A double-click opens it.
- Search finds files by name and uses no index. "Search Everywhere" covers your home folder and the apps.
- Servers on the local network are found through Bonjour.
- The trash is the Trash of MacOS.
- A folder MacOS keeps the app out of says so, with a button that opens System Settings.
- Preferences opens with Command-comma. The other shortcuts use Control, as on GNOME.

## Missing Features

What Files on GNOME has and this port does not.

- Search in the contents of files.
- Windows shares (SMB).
- Phones and cameras, online accounts, NFS.
- Extensions, and with them "Open in Terminal".
- Thumbnails. Pictures and documents show the icon of their type.
- Saved passwords for servers.
- Opening a folder as administrator.
- An app bundle. It is started from a terminal, so MacOS gives the permissions to the terminal.

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
