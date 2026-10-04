# Nautilus on mac: todo

Base: upstream Nautilus 49.6, branch `mac-development`.
Where mac-only files go:

- `src/mac/`: code
- `macos/bundle/`: what goes into `Files.app` (Info.plist, entitlements)
- `macos/scripts/`: scripts the build runs
- `subprojects/`: dependencies the build pulls (`*.wrap`) and their patches (`packagefiles/`)

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
- [ ] Types macOS does not know (meson.build, .cfg) and files without an extension (LICENSE, NEWS): detect them. Either a small mac mapping, or bundle the freedesktop MIME database (shared-mime-info) for name patterns and content sniffing
- [ ] No thumbnails for images, PDFs and videos
- [ ] Read-only badge icon is missing (`emblem-unwritable-symbolic`). Yaru and Adwaita call it `emblem-readonly`

### gvfs (decided: port it, later)

- [ ] Port gvfs. Gives Network, Trash, Recent and more
- [ ] Until then, Network gives "Could not mount network:///"

### System integration

- [ ] No D-Bus session on mac: warnings at startup and on every folder change
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
- [ ] `mac-release` branch and release tags (`49.6-mac.1`)
- [ ] Signing and notarization
- [ ] Push `mac-development` (8 local commits not pushed)

### Known and accepted

- Dragging one of the six fixed sidebar folders onto "New bookmark" adds a second row for it
- If installing the icon theme is interrupted halfway, the next install repairs it
- Running the app before `ninja install` stops at startup, because the bundled icon theme is not there yet

### Later

- [ ] Send the generic fixes to GNOME (`strrchr`, unused includes)
- [ ] Merge upstream 50 (adds glycin) and 51 (adds libgxdp)
