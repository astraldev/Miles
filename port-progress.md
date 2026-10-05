# Nautilus on mac: progress

What is done and decided. What is left is in `todo.md`.

Base: upstream Nautilus 51.0.1, branch `mac-development`. The port as it was on 49.6 is kept on `mac-gnome-49`.

Where mac-only files go:

- `src/macos/`: code
- `macos/bundle/`: what goes into `Files.app` (Info.plist, entitlements)
- `macos/scripts/`: scripts the build runs
- `macos/patches/`: patches for what the scripts build (gvfs)
- `macos/shims/`: stand-ins for libraries that are not built on mac (glycin)
- `subprojects/`: dependencies the build pulls (`*.wrap`) and their patches (`packagefiles/`)

To build: Homebrew's Python has to come first on `PATH` (blueprint-compiler needs PyGObject, the system Python lacks it):

    PATH="/opt/homebrew/bin:$PATH" meson setup build --prefix="$PWD/.deps/prefix" \
      -Dextensions=false -Dintrospection=false -Ddocs=false \
      -Dselinux=disabled -Dcloudproviders=disabled -Dtests=none

## Done

### Repo and build

- `upstream` is GNOME, `origin` is astraldev/nautilus-mac, default branch `mac-development`
- Builds and links on mac, with extensions, introspection, tests and docs off
- `meson setup` on a clean clone needs no manual steps: libportal, gnome-desktop, libgxdp and blueprint-compiler are pulled by the build as wraps pinned to commits, with patches in `subprojects/packagefiles/`
- `g_set_date_time` clash with GLib 2.90 (upstream fix cherry-picked)
- Unused desktop file launcher removed (upstream fix cherry-picked), and its includes
- `memrchr` replaced with `strrchr`
- autofs check uses `statfs` on mac
- Install step no longer needs `update-desktop-database`
- localsearch indexer disabled on mac (it made the app abort at startup)

### GNOME 51

- Merged upstream 51.0.1 (48 conflicts, 14 in files the port changed). Builds, starts, browses, searches
- glycin (Rust) replaced by a stand-in on gdk-pixbuf, `macos/shims/glycin/`. It loads the icons that come from a file: app icons, and the picture a user picks as a custom icon
- blueprint-compiler pulled as a wrap with a patch, nothing to install
- libgxdp: upstream's wrap, one unused include dropped from `nautilus-portal.c`
- Search: 51 gives providers a base class. The walker is written against it
- File type filter: 51 matches types in one function, which goes to the port's UTI matching on mac
- gvfs updated to 1.62.0, the version that goes with 51. `mac-gnome-49` keeps 1.58.5

### Browsing and looks

- App starts, shows a window, browses folders
- Sidebar: Applications, Documents, Downloads, Movies, Music, Pictures as fixed places in their own section with a divider (`src/macos/nautilus-mac-places.c`, `nautilus-sidebar.c`). They cannot be dragged or bookmarked twice
- Yaru icon theme is bundled with the app: `macos/scripts/install-yaru.sh` puts it in the app's own data folder (`share/nautilus/icons`) during `ninja install`, pinned to a Yaru commit, and Nautilus adds that folder to the icon search path itself. If the theme is missing, the app refuses to start
- macOS accent colour picks the Yaru colour variant, at startup and when it is changed in System Settings (`src/macos/nautilus-mac-appearance.c`): Multicolour and Blue to blue, Purple to purple, Pink to magenta, Red to red, Orange to Yaru's own, Yellow to yellow, Green to viridian, Graphite to bark
- Dark appearance uses Yaru's `-dark` variant of the same colour, whose folders are lighter. It follows macOS, also while the app runs
- Grid zoom steps are even: 48, 64, 88, 120, 160, each about 1.35 times the one before (upstream: 48, 64, 96, 168, 256)
- The startup disk has a row in the sidebar, under its own name ("Macintosh HD"). It is in the drives section at the bottom, after a divider, ahead of the drives that come and go
- Cloud folders have rows in the sidebar, in a section of their own above the drives: iCloud Drive (`~/Library/Mobile Documents/com~apple~CloudDocs`) and each folder in `~/Library/CloudStorage`, where the apps of Google Drive, OneDrive and others keep the user's files as normal folders. No sign-in code and no gvfs backend: gvfs's Google backend is deprecated and needs GNOME Online Accounts. The rows are looked at again when the window becomes active, so a service set up meanwhile shows without a restart. macOS names iCloud Drive's folder. It has no name for the others, so theirs is the folder's name without the account ("GoogleDrive-name@…" shows as "Google Drive"). Files and folders inside a cloud folder carry a small cloud mark on their icon, the way a link or a read-only file has its mark
- Preferences opens with Command-comma, as in other mac apps (upstream: Control-comma)
- Archives are extracted by Nautilus itself, on double-click and with "Extract": macOS's own Archive Utility showed its result in Finder. Zip, tar, gzip, bzip2, xz and 7z are known as archives on mac
- Undo of "Move to Trash" says why when macOS keeps Nautilus out of the Trash, where it did nothing
- Text is a step larger: the base font is set to 13 pt, where GTK takes 12 pt from macOS (`set_font_size()` in `nautilus-mac-appearance.c`)
- Dialogs that open inside the window (Properties, ...) draw right: GTK's renderer left most of the window undrawn on macOS, so full redraws are forced (`GSK_DEBUG=full-redraw`, set in `main()`)
- File type icons: with Yaru, the types macOS knows get their own icon (.docx, .pdf, .json, .zip, .md, ...)
- Read-only, not-accessible and link badges: 51 ships its own icons for them

Checked on screen on 51 by the owner: sidebar folders and divider, app icons, launching apps and "Show Package Contents", zoom steps, the "No Permission" page (Downloads), dialogs inside the window, Network (found servers, connecting, browsing), the Macintosh HD row, Command-comma for Preferences, the dark folders, the 13 pt font, the app icons from macOS (Books, Phone, their size next to folders), and the iCloud Drive and Google Drive rows (opening Google Drive, My Drive and the files in it).

### Apps

- Apps show the icon Finder shows (`src/macos/nautilus-mac-app-icon.m`, the one Objective-C file): it asks macOS for it (`NSWorkspace`). Many apps keep their icon where only macOS can read it, and the `.icns` file beside it is a leftover: Books' is blank, Phone's is the old square one. The icons come with the margins macOS gives them, so they are the size of the folders. About 30 ms the first time this Mac draws an app's icon, 4 ms after that: macOS keeps what it has drawn
- App icons follow "Icon & widget style" (Default, Dark, Clear, Tinted): macOS does not announce a change of it in a public way, so Nautilus reads the setting when its window becomes active again, and draws the app icons anew if it differs, one after the other in name order (20 ms apart), since all at once is a flash. Other files are left alone
- Double-click on an app launches it (`/usr/bin/open`). Right-click has "Show Package Contents" to browse inside
- Dragging over an app does not launch it, and dropping on one is refused
- Apps launched from Nautilus do not get its private bus
- Applications and Applications/Utilities have a banner: the apps that come with macOS are in `/System/Applications`, which Finder shows as part of Applications. Its button opens that folder. Without it Utilities only says "Folder is Empty"

### Search

- Files are found by name with a walker of the port's own (`src/macos/nautilus-search-engine-walker.c`). It asks macOS for a folder's files in bulk (`getattrlistbulk`) from 4 threads, where GIO reads one file at a time. `~/Documents` (800,000 files): 2 s with hidden files off, 8 s with them on, against 49 s before. Its results match `find` file for file. A search in a folder comes to it through upstream's simple engine. "Search Everywhere" goes to it in the place localsearch has on Linux, and looks through the home folder, `/Applications` and `/System/Applications`
- File type filter works on mac: the filter's MIME types are turned into the UTIs GIO uses there
- The texts about "search locations" are reworded on mac, and their "Search Settings" button is gone: both belong to GNOME's indexer. The note under the search bar no longer shows for a folder on this Mac
- The walker does not go into apps, hidden folders (unless hidden files are shown, so `~/Library` is left out), links, or the volumes macOS keeps for itself, which repeat the startup disk's files. Searching `/` takes 32 s

### Network, trash and recent (gvfs)

- `macos/scripts/install-gvfs.sh` builds gvfs into the prefix during `ninja install`, pinned to a commit, with the patches in `macos/patches/`
- Nautilus starts its own D-Bus session bus (`src/macos/nautilus-mac-session-bus.c`, `macos/data/dbus-session.conf.in`) and stops it on quit. gvfs daemons start on demand and exit with the bus. A second launch joins the running app
- The bus's sockets are in `~/.cache/nautilus`, since macOS sweeps `/tmp`. Two launches at once do not start two buses. A dead bus does not kill Nautilus, and a stuck one does not keep it from quitting
- A bus left by a Nautilus that crashed is taken over: the next launch uses it and stops it on quit. It asks the bus's socket which process is behind it. Before, such a bus ran until logout. Tested by killing a test copy
- `dbus-daemon` is built with the app, from a wrap pinned to dbus 1.16.2. Homebrew's is not used
- Network view opens. Connect by address works for `sftp://`, `dav://`, `davs://`, `ftp://`, `afp://` (tested with the `gio` tool against test servers, not through the window)
- Servers on the local network show up by themselves, under "Available on Current Network" (`src/macos/nautilus-mac-bonjour.c`). It asks macOS's own Bonjour service (`dns_sd.h`) for sftp, AFP, WebDAV and FTP servers, in Nautilus and not in gvfs, and lists them the way recent servers are listed. This Mac itself is left out
- Folders on a server show as folders: gvfs names them by MIME type, which GIO does not know on mac, so the type is translated
- Trash reads the macOS trash folders (`~/.Trash`, `.Trashes/<uid>` on other drives). Listing, opening and deleting for good work (tested against a test home folder)
- Trash knows where a file came from and when it was trashed, so "Restore" works: macOS has no call for it, so the gvfs patch reads the put-back records Finder keeps in the trash folder's `.DS_Store` file, and takes the date from the file's "date added". Tested on a disk image: a file whose name clashed in the trash went back to its folder under its own name. The reader was run 230,000 times on damaged files under sanitizers
- Recent works

### Permissions

- A folder macOS blocks (Trash, Downloads, ...) shows "No Permission" and a button "Open System Settings" in place of "Folder is Empty" (`src/macos/nautilus-mac-privacy.c`). The button opens the Files and Folders page for Desktop, Documents, Downloads and other drives, and the Full Disk Access page for the rest. No error dialog comes with it, and the files of the folder shown before are cleared
- A folder showing "No Permission" is looked at again when the window becomes active, and reloads if it can be read now: access is given in System Settings or in a prompt of macOS, both outside the window. For Full Disk Access macOS itself offers "Quit & Reopen"
- A folder the user's account may not read (another user's home, ...) shows "No Permission" too, with no button. Upstream reopens it as `admin://`, which asks for a password through PolKit: that is Linux only, so on mac it said "admin locations are not supported"
- Startup prints no "display server connection" message, and the build has no warnings in Nautilus's own code but one upstream deprecation

### Reviews

- Nine review rounds on search, sidebar, icons and accent colour, then one each on app icons and on launching, the bus and the gvfs script. Findings fixed, except those under "Known and accepted"
- Memory audit of the port's code on 49.6: no memory-corruption bugs. Its small findings are fixed

## Decided

- `main` mirrors GNOME untouched. `mac-development` holds the port, and upstream releases are merged into it. `mac-release` comes later
- Yaru is bundled with the app, not optional
- Sidebar folders are fixed and match macOS's own
- No Spotlight. A provider for it was written and is removed: its index can be off or lack folders, as on the dev Mac, and the walker is fast enough alone. The price is search in file contents, which only an index gives
- gvfs is ported, not replaced by native code
- `smb://` (Windows shares) is left out: it needs samba and its large dependency chain. So they are not looked for on the network either
- Stock extensions stay off. No AirDrop or Share menu. "Open in terminal" would be nice, not needed
- No thumbnails for now: the type icons are enough
- Graphite accent is not bark. What it is instead is open
- glycin stays a stand-in: no Rust in the build
- No "open as administrator": it would need a helper that runs as root
- Applications is not merged with `/System/Applications` the way Finder does it. A banner points there

## Known and accepted

- Dragging one of the six fixed sidebar folders onto "New bookmark" adds a second row for it
- If installing the icon theme is interrupted halfway, the next install repairs it
- Running the app before `ninja install` stops at startup, because the bundled icon theme is not there yet
- Building with a `datadir` outside the install prefix puts the icon theme in the wrong place
- The first time an app folder is opened on a Mac, drawing its icons can hold the window for about a second (30 ms per app). Loading them off the main thread was weighed and left out: it needs a guess at what macOS has cached
- A plain folder named `something.app` is treated as an app. macOS says the same of it (`kCFURLIsApplicationKey`), and so does Finder
