# Nautilus on mac: todo

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

## GNOME 51

- [x] Merged upstream 51.0.1 (48 conflicts, 14 in files the port changed). Builds, starts, browses, searches
- [x] glycin (Rust): replaced by a stand-in on gdk-pixbuf, `macos/shims/glycin/`. Only used for the picture a user picks as a custom icon
- [x] blueprint-compiler: pulled as a wrap with a patch, nothing to install
- [x] libgxdp: upstream's wrap, one unused include dropped from `nautilus-portal.c`
- [x] Spotlight provider moved to 51's provider base class (not committed). It lost its own thread hand-over code, about 100 lines
- [x] File type filter: 51 matches types in one function, which now goes to the port's UTI matching on mac
- [ ] Lost in the merge: an app whose icon cannot be read gets the generic file icon again, not a generic app icon. 51 no longer tells the caller that an icon is a fallback
- [x] gvfs updated to 1.62.0, the version that goes with 51. The trash patch was redone for it, and a second patch replaces `explicit_bzero`, which mac lacks. `mac-gnome-49` keeps 1.58.5
- [ ] Extensions are still off. The image one needs more glycin functions in the stand-in
- [ ] Look at 51 on screen: sidebar (rewritten upstream), fixed folders and their divider, app icons, permission page, Network, Trash
- [ ] The memory audit did not cover the files changed by the merge

## Done

- [x] Repo: `upstream` is GNOME, `origin` is astraldev/nautilus-mac, default branch `mac-development`
- [x] Patches for the two dependencies not in Homebrew: libportal 0.9.1, gnome-desktop 44.5 (committed in `macos/patches/`, now moved to `subprojects/packagefiles/`, move not committed)
- [x] Builds and links on mac (extensions, introspection, tests and docs off)
- [x] `g_set_date_time` clash with GLib 2.90 (upstream fix cherry-picked)
- [x] Removed unused desktop file launcher (upstream fix cherry-picked) and its includes
- [x] `memrchr` replaced with `strrchr`
- [x] autofs check uses `statfs` on mac
- [x] Install step no longer needs `update-desktop-database`
- [x] localsearch indexer disabled on mac (it made the app abort at startup)
- [x] App starts, shows a window, browses folders

## In progress (not committed)

- [ ] Spotlight search provider: `src/mac/nautilus-search-engine-spotlight.c`. Under review
- [ ] File type filter in search: matched nothing on mac. Fix in `nautilus-query.c` and `nautilus-search-engine-recent.c`. Under review
- [ ] Sidebar: Applications, Documents, Downloads, Movies, Music, Pictures as fixed places in their own section with a divider (`src/mac/nautilus-mac-places.c`, `nautilus-sidebar.c`). They cannot be dragged or bookmarked twice. Needs a look on screen
- [ ] macOS accent colour picks the Yaru colour variant, at startup and when it is changed in System Settings (`src/mac/nautilus-mac-appearance.c`): Multicolour and Blue to blue, Purple to purple, Pink to magenta, Red to red, Orange to Yaru's own, Yellow to yellow, Green to viridian, Graphite to bark. Needs a look on screen
- [ ] Yaru icon theme is bundled with the app (decided): `macos/scripts/install-yaru.sh` puts it in the app's own data folder (`share/nautilus/icons`) during `ninja install`, pinned to a Yaru commit, and Nautilus adds that folder to the icon search path itself (`nautilus-application.c`). It is not installed in the shared icons folder and does not depend on the environment. If the theme is missing, the app refuses to start
- [ ] libportal and gnome-desktop are pulled and patched by the build itself: `subprojects/*.wrap` pinned to commits, patches in `subprojects/packagefiles/`. `meson setup` on a clean clone now needs no manual steps
- [ ] App bundle files: `macos/bundle/Info.plist.in`, `macos/bundle/nautilus.entitlements`. Not wired into the build yet
- [ ] Research: a search that does not need Spotlight (see Search)
- [ ] Apps show their own icon (`src/mac/nautilus-mac-app-icon.c`): the icon set in Finder if there is one, else the `.icns` file the app's Info.plist names. About 7 ms per app the first time it is drawn. Needs a look on screen
- [ ] Double-click on an app launches it (`/usr/bin/open`). Right-click has "Show Package Contents" to browse inside. Needs a try on screen
- [ ] Grid zoom steps are even now: 48, 72, 112, 168, 256, each about 1.5 times the one before (upstream: 48, 64, 96, 168, 256). Needs a look on screen

## Left

### Search

- [ ] "Search everywhere" only uses Spotlight. With a broken Spotlight index it finds nothing useful
- [ ] Folder walk is slow on big folders: 49 s for `~/Documents` (810,000 entries), `find` does it in 19 s
- [ ] Spotlight provider is untested in home folders (the index on the dev Mac is empty)
- [ ] Search bar hint still talks about localsearch "search locations"
- [ ] Type filter: a type macOS does not know matches nothing. 118 of the 207 MIME types in the filter groups are unknown to macOS, so some files are missed (.mkv in Video, .rtf in Documents, .psd in Picture)
- [ ] Type filter: the "Other Type…" list is empty on mac
- [ ] Empty files are typed as text on mac, so an empty `.mp4` shows under Text File, not Video
- [ ] "Search everywhere" with a Folders or Files filter never returns apps: Spotlight types an `.app` as a package, not a folder (found by review, one-line fix in the Spotlight type clause)
- [ ] Replace the slow folder walk with a mac walker (`getattrlistbulk`, about 4 threads, breadth-first) and use it for "search everywhere" too. Measured on `~/Documents`: 4.4 s instead of 49 s
- [ ] "Search everywhere" skips `~/Library`, `node_modules` and other nested folders like them on its first pass (decided). They are lazy: searched only after the main results, or when searching inside one of them directly
- [ ] Work out the list of lazy folders (candidates: `~/Library`, `node_modules`, `.git`, build output folders, the inside of app bundles)

### Sidebar

- [ ] Recent is hidden (needs gvfs)
- [ ] Trash and Network are listed but do not work (need gvfs)
- [ ] Disks and volumes: check what shows when a drive is plugged in

### Icons

- [ ] Files only get two generic icons: lined page for text, blank page for the rest (.docx, .cfg, meson.build, LICENSE)
- [ ] Bundle Adwaita too for the standalone app: Yaru falls back to it for the icons it lacks, and today it comes from Homebrew
- [ ] The bundled theme's folder is fixed at build time (`NAUTILUS_DATADIR`). A relocatable `Files.app` has to find it relative to the app
- [ ] Yaru's `-dark` variants are not installed or used. Check on screen whether dark appearance needs them
- [ ] Homebrew formula: it cannot download during a build, so it has to supply the same pinned libportal, gnome-desktop and Yaru sources itself
- [ ] Credit Yaru in the app (About dialog or bundle). The licence files are already installed with the icons
- [ ] App icons come from the `.icns` file. Apps that keep their icon only in `Assets.car` (many in `/System/Library/CoreServices`) get a generic app icon, and on macOS 26 most `.icns` files stop at 256 px, so the two largest zoom steps are stretched on Retina. `NSWorkspace` gives the real icon at any size but takes 20 to 300 ms per app, so it has to run off the main thread
- [ ] A plain folder named `something.app` is treated as an app
- [ ] Types macOS does not know (meson.build, .cfg) and files without an extension (LICENSE, NEWS): detect them. Either a small mac mapping, or bundle the freedesktop MIME database (shared-mime-info) for name patterns and content sniffing
- [ ] No thumbnails for images, PDFs and videos
- [ ] Read-only badge icon is missing (`emblem-unwritable-symbolic`). Yaru and Adwaita call it `emblem-readonly`

### gvfs (in progress, not committed)

- [x] `macos/scripts/install-gvfs.sh` builds gvfs into the prefix during `ninja install`, pinned to a commit, with the patches in `macos/patches/`
- [x] Nautilus starts its own D-Bus session bus (`src/mac/nautilus-mac-session-bus.c`, `macos/data/dbus-session.conf.in`) and stops it on quit. gvfs daemons start on demand and exit with the bus. The D-Bus warnings are gone, and a second launch now joins the running app
- [x] Network view opens. Connect by address works for `sftp://`, `dav://`, `davs://`, `ftp://`, `afp://` (tested with the `gio` tool against test servers, not through the window)
- `smb://` (Windows shares) is left out (decided): it needs samba and its large dependency chain
- [ ] Servers do not show up by themselves: gvfs finds them with avahi, which mac lacks. Rewrite that backend on Apple's `dns_sd.h` (Bonjour)
- [ ] Saved passwords: gvfs wants libsecret and a Secret Service. Mac has the Keychain instead
- [x] Trash reads the macOS trash folders (`~/.Trash`, `.Trashes/<uid>` on other drives): `macos/patches/gvfs-1.62.0-macos-trash.patch`, applied by the script. Listing, opening and deleting for good work (tested against a test home folder)
- [ ] Trash still shows empty until the app has Full Disk Access: macOS blocks `~/.Trash` for every app but Finder, and never asks
- [ ] A folder macOS blocks (Trash, Downloads, ...) shows "No Permission" and a button "Open System Settings" in place of "Folder is Empty" (`src/mac/nautilus-mac-privacy.c`, not committed). The button opens the Files and Folders page for Desktop, Documents, Downloads and other drives, and the Full Disk Access page for the rest. Needs a look on screen. Left:
  - The view does not notice the change: the user has to reload, and for Full Disk Access restart the app. Offer "Quit and Reopen"
  - The error dialog "You do not have the permissions necessary..." still shows as well
  - Needs the signed `Files.app` to be useful: the grant is tied to the app's identity (today it goes to the terminal), and the gvfs daemons get it through the app that started them (check this)
- [ ] Trash: no "Restore" and no "Trashed on" date. macOS keeps the original place in `~/.Trash/.DS_Store` (put-back records), which needs a parser, and the date as the file's "date added"
- [ ] Recent works but is empty until files are opened from Nautilus. Sidebar row is still hidden
- [x] Review fixes (not committed): apps launched from Nautilus no longer get its private bus (they died when Nautilus quit); dragging over an app no longer launches it, and dropping on one is refused; the bus folder moved from `/tmp` (swept by macOS after 3 days) to `~/.cache/nautilus`; two launches at once no longer start two buses; a dead bus no longer kills Nautilus
- [ ] Scripts and "open in terminal" started from Nautilus still get its private bus in their environment
- [ ] If Nautilus crashes, the bus and daemons keep running. The next launch reuses them, nothing stops them
- [ ] `dbus-daemon` comes from Homebrew, its path is fixed at build time. A standalone `Files.app` has to ship it, and gvfs's files hold absolute paths (`.mount`, `.service`, rpath)

### System integration

- [ ] "Failed to initialize display server connection" at startup (file picker portal, X11/Wayland only)
- [ ] Window does not always come to the front when started from a terminal
- [ ] Opened files are not added to the recent list ("no command line for the application")
- [ ] autofs check: confirm `statfs` does not trigger the mount
- [ ] Starred files (tinysparql) not tested
- [ ] Stock extensions are off (image and audio/video properties need gexiv2 0.14 API and gstreamer)

### Looks

- [ ] A dialog is 3 px too short (GTK warning)
- [ ] GTK prints `gdk_surface_thaw_updates` errors when menus open or close

### Packaging and release

- [ ] Wire `Info.plist.in` and the entitlements into the build to produce `Files.app`
- [ ] App icon (`.icns`)
- [ ] Homebrew tap formula, including the two patched dependencies
- [ ] `mac-release` branch and release tags (`51.0.1-mac.1`)
- [ ] Signing and notarization
- [ ] Push `mac-development` and `mac-gnome-49`

### Known and accepted

- Dragging one of the six fixed sidebar folders onto "New bookmark" adds a second row for it
- If installing the icon theme is interrupted halfway, the next install repairs it
- Running the app before `ninja install` stops at startup, because the bundled icon theme is not there yet
- Building with a `datadir` outside the install prefix puts the icon theme in the wrong place

### Later

- [ ] Send the generic fixes to GNOME (`strrchr`, unused includes, the unused `gxdp-dbus.h` include in `nautilus-portal.c`)
- [ ] Real glycin in place of the stand-in, if Rust in the build is ever acceptable. A 72-line patch that makes glycin 2.2.1 build as a wrap is kept in `.deps/gnome51-research/`. It loads PNG and JPEG on mac, not SVG
