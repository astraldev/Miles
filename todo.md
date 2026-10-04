# Nautilus on mac: todo

What is left. What is done and decided is in `port-progress.md`.

## Reported on screen

- [ ] Changing the accent colour in System Settings does not change the folder icons. The handler runs when the notification is posted by hand. It now listens for a second notification and reads the setting again after 0.3 and 1.5 s, in case it was read too early (not committed). To be tried again: run with `G_MESSAGES_DEBUG=nautilus-mac` and the log says what arrives and what is read
- [ ] Network is not right. What was tried and what happened is not known yet. Connecting by address works with the `gio` tool on gvfs 1.62 (sftp)
- [ ] Graphite accent: bark is not the same colour (decided). Yaru has no grey variant. Pick one: sage, blue as for Multicolour, or make a grey one by taking the colour out of Yaru's folders

## Check on screen

- [ ] Yaru's `-dark` variants are not installed or used. Does dark appearance need them?
- [ ] Disks and volumes: what shows when a drive is plugged in

## GNOME 51

- [ ] Lost in the merge: an app whose icon cannot be read gets the generic file icon again, not a generic app icon. 51 no longer tells the caller that an icon is a fallback
- [ ] The memory audit did not cover the files changed by the merge

## Search

- [ ] "Search everywhere" only uses Spotlight. With a broken Spotlight index it finds nothing useful
- [ ] Replace the slow folder walk (49 s for `~/Documents`) with the mac walker, and use it for "search everywhere" too
- [ ] "Search everywhere" skips `~/Library`, `node_modules` and other nested folders like them on its first pass. They are lazy: searched only after the main results, or when searching inside one of them directly
- [ ] Work out the list of lazy folders (candidates: `~/Library`, `node_modules`, `.git`, build output folders, the inside of app bundles)
- [ ] Spotlight provider is untested in home folders (the index on the dev Mac is empty)
- [ ] "Search everywhere" with a Folders or Files filter never returns apps: Spotlight types an `.app` as a package, not a folder (one-line fix in the Spotlight type clause)
- [ ] Search bar hint still talks about localsearch "search locations"
- [ ] Type filter: a type macOS does not know matches nothing. 118 of the 207 MIME types in the filter groups are unknown to macOS, so some files are missed (.mkv in Video, .rtf in Documents, .psd in Picture)
- [ ] Type filter: the "Other Type…" list is empty on mac
- [ ] Empty files are typed as text on mac, so an empty `.mp4` shows under Text File, not Video

## Icons

- [ ] Types macOS does not know get the blank page: meson.build, .cfg, and files without an extension (LICENSE, NEWS). Yaru has icons for some (`text-x-meson`). Either a small mac mapping, or bundle the freedesktop MIME database (shared-mime-info) for name patterns and content sniffing
- [ ] A few types have an icon in Yaru under another name than GIO asks for on mac: Python (`text-x-python-script` asked, `text-x-python` there), plain text (`text-*` asked)
- [ ] Credit Yaru in the app (About dialog or bundle). The licence files are already installed with the icons

## Network, trash and recent (gvfs)

- [ ] Servers do not show up by themselves: gvfs finds them with avahi, which mac lacks. Rewrite that backend on Apple's `dns_sd.h` (Bonjour)
- [ ] Saved passwords: gvfs wants libsecret and a Secret Service. Mac has the Keychain instead
- [ ] Trash shows empty until the app has Full Disk Access: macOS blocks `~/.Trash` for every app but Finder, and never asks
- [ ] Trash: no "Restore" and no "Trashed on" date. macOS keeps the original place in `~/.Trash/.DS_Store` (put-back records), which needs a parser, and the date as the file's "date added"
- [ ] Recent is empty until files are opened from Nautilus, and opened files are not added to it ("no command line for the application")
- [ ] Scripts and "open in terminal" started from Nautilus still get its private bus in their environment
- [ ] If Nautilus crashes, the bus and daemons keep running. The next launch reuses them, nothing stops them

## Permissions

- [ ] The view does not notice when access is granted: the user has to reload, and for Full Disk Access restart the app. Offer "Quit and Reopen"
- [ ] The error dialog "You do not have the permissions necessary..." still shows next to the "No Permission" page
- [ ] The grant is tied to the app's identity, so today it goes to the terminal. Needs the signed `Files.app`. Check that the gvfs daemons get it through the app that started them

## System integration

- [ ] "Failed to initialize display server connection" at startup (file picker portal, X11/Wayland only)
- [ ] Window does not always come to the front when started from a terminal
- [ ] autofs check: confirm `statfs` does not trigger the mount
- [ ] Starred files (tinysparql) not tested
- [ ] A dialog is 3 px too short (GTK warning)
- [ ] GTK prints `gdk_surface_thaw_updates` errors when menus open or close

## Packaging and release

- [ ] Wire `macos/bundle/Info.plist.in` and the entitlements into the build to produce `Files.app`
- [ ] App icon (`.icns`)
- [ ] A relocatable `Files.app` has to find its data relative to itself: the bundled icon theme's folder is fixed at build time (`NAUTILUS_DATADIR`), and gvfs's files hold absolute paths (`.mount`, `.service`, rpath)
- [ ] Ship `dbus-daemon` in the app. Today it comes from Homebrew, with its path fixed at build time
- [ ] Bundle Adwaita too: Yaru falls back to it for the icons it lacks, and today it comes from Homebrew
- [ ] Homebrew tap formula. It cannot download during a build, so it has to supply the pinned sources itself: libportal, gnome-desktop, libgxdp, blueprint-compiler, gvfs, Yaru
- [ ] Signing and notarization
- [ ] `mac-release` branch and release tags (`51.0.1-mac.1`)

## Later

- [ ] Send the generic fixes to GNOME (`strrchr`, unused includes, the unused `gxdp-dbus.h` include in `nautilus-portal.c`)
- [ ] Real glycin in place of the stand-in, if Rust in the build is ever acceptable. A 72-line patch that makes glycin 2.2.1 build as a wrap is kept in `.deps/gnome51-research/`. It loads PNG and JPEG on mac, not SVG
