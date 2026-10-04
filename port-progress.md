# Nautilus on mac: progress

What is done and decided. What is left is in `todo.md`.

Base: upstream Nautilus 51.0.1, branch `mac-development`. The port as it was on 49.6 is kept on `mac-gnome-49`.

Where mac-only files go:

- `src/mac/`: code
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
- Spotlight provider moved to 51's provider base class. It lost its own thread hand-over code, about 100 lines
- File type filter: 51 matches types in one function, which goes to the port's UTI matching on mac
- gvfs updated to 1.62.0, the version that goes with 51. `mac-gnome-49` keeps 1.58.5

### Browsing and looks

- App starts, shows a window, browses folders
- Sidebar: Applications, Documents, Downloads, Movies, Music, Pictures as fixed places in their own section with a divider (`src/mac/nautilus-mac-places.c`, `nautilus-sidebar.c`). They cannot be dragged or bookmarked twice
- Yaru icon theme is bundled with the app: `macos/scripts/install-yaru.sh` puts it in the app's own data folder (`share/nautilus/icons`) during `ninja install`, pinned to a Yaru commit, and Nautilus adds that folder to the icon search path itself. If the theme is missing, the app refuses to start
- macOS accent colour picks the Yaru colour variant, at startup and when it is changed in System Settings (`src/mac/nautilus-mac-appearance.c`): Multicolour and Blue to blue, Purple to purple, Pink to magenta, Red to red, Orange to Yaru's own, Yellow to yellow, Green to viridian, Graphite to bark
- Grid zoom steps are even: 48, 72, 112, 168, 256, each about 1.5 times the one before (upstream: 48, 64, 96, 168, 256)
- File type icons: with Yaru, the types macOS knows get their own icon (.docx, .pdf, .json, .zip, .md, ...)
- Read-only, not-accessible and link badges: 51 ships its own icons for them

Checked on screen on 51 by the owner: sidebar folders and divider, app icons, launching apps and "Show Package Contents", zoom steps, the "No Permission" page.

### Apps

- Apps show their own icon (`src/mac/nautilus-mac-app-icon.c`): the icon set in Finder if there is one, else the `.icns` file the app's Info.plist names. About 7 ms per app the first time it is drawn
- Double-click on an app launches it (`/usr/bin/open`). Right-click has "Show Package Contents" to browse inside
- Dragging over an app does not launch it, and dropping on one is refused
- Apps launched from Nautilus do not get its private bus

### Search

- Spotlight search provider, in the place localsearch has on Linux (`src/mac/nautilus-search-engine-spotlight.c`)
- File type filter works on mac: the filter's MIME types are turned into the UTIs GIO uses there
- Research on a search that does not need Spotlight: a mac walker on `getattrlistbulk` with about 4 threads, breadth-first. Measured on `~/Documents` (810,000 entries): 4.4 s, against 49 s for the current walk

### Network, trash and recent (gvfs)

- `macos/scripts/install-gvfs.sh` builds gvfs into the prefix during `ninja install`, pinned to a commit, with the patches in `macos/patches/`
- Nautilus starts its own D-Bus session bus (`src/mac/nautilus-mac-session-bus.c`, `macos/data/dbus-session.conf.in`) and stops it on quit. gvfs daemons start on demand and exit with the bus. A second launch joins the running app
- The bus's sockets are in `~/.cache/nautilus`, since macOS sweeps `/tmp`. Two launches at once do not start two buses. A dead bus does not kill Nautilus, and a stuck one does not keep it from quitting
- Network view opens. Connect by address works for `sftp://`, `dav://`, `davs://`, `ftp://`, `afp://` (tested with the `gio` tool against test servers, not through the window)
- Trash reads the macOS trash folders (`~/.Trash`, `.Trashes/<uid>` on other drives). Listing, opening and deleting for good work (tested against a test home folder)
- Recent works

### Permissions

- A folder macOS blocks (Trash, Downloads, ...) shows "No Permission" and a button "Open System Settings" in place of "Folder is Empty" (`src/mac/nautilus-mac-privacy.c`). The button opens the Files and Folders page for Desktop, Documents, Downloads and other drives, and the Full Disk Access page for the rest

### Reviews

- Nine review rounds on search, sidebar, icons and accent colour, then one each on app icons and on launching, the bus and the gvfs script. Findings fixed, except those under "Known and accepted"
- Memory audit of the port's code on 49.6: no memory-corruption bugs. Its small findings are fixed

## Decided

- `main` mirrors GNOME untouched. `mac-development` holds the port, and upstream releases are merged into it. `mac-release` comes later
- Yaru is bundled with the app, not optional
- Sidebar folders are fixed and match macOS's own
- Search must not depend on Spotlight. "Search everywhere" will skip `~/Library`, `node_modules` and folders like them on its first pass
- gvfs is ported, not replaced by native code
- `smb://` (Windows shares) is left out: it needs samba and its large dependency chain
- Stock extensions stay off. No AirDrop or Share menu. "Open in terminal" would be nice, not needed
- No thumbnails for now: the type icons are enough
- Graphite accent is not bark. What it is instead is open
- glycin stays a stand-in: no Rust in the build

## Known and accepted

- Dragging one of the six fixed sidebar folders onto "New bookmark" adds a second row for it
- If installing the icon theme is interrupted halfway, the next install repairs it
- Running the app before `ninja install` stops at startup, because the bundled icon theme is not there yet
- Building with a `datadir` outside the install prefix puts the icon theme in the wrong place
- App icons come from the `.icns` file. An app that keeps its icon only in `Assets.car` gets a generic icon, and most `.icns` files stop at 256 px, so the two largest zoom steps are stretched on Retina
- A plain folder named `something.app` is treated as an app. macOS says the same of it (`kCFURLIsApplicationKey`), and so does Finder
