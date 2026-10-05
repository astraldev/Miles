# Nautilus on mac: todo

What is left. What is done and decided is in `port-progress.md`.

## Deferred by the owner

- [ ] Accent colour: a real switch in System Settings has not been tried since the fix. The handler works when the change is simulated. Run with `G_MESSAGES_DEBUG=nautilus-mac` and the log says what arrives and what is read
- [ ] Graphite accent: bark is not the same colour. Yaru has no grey variant. Pick one: make a grey one by taking the colour out of Yaru's folders (turning one icon grey with `sips` works and keeps its transparency), blue as for Multicolour, or sage
- [ ] Types macOS does not know get the blank page: meson.build, .cfg, and files without an extension (LICENSE, NEWS). The icons the known types get are enough for now. Yaru has icons for some (`text-x-meson`). Either a small mac mapping, or bundle the freedesktop MIME database (shared-mime-info) for name patterns and content sniffing

## Check on screen

- [ ] Double-click on a `.zip` extracts it in place, with no Finder window
- [ ] A folder the account may not read shows "No Permission", with no dialog and no "admin" message
- [ ] Zoom steps of 1.35: 48, 64, 88, 120, 160
- [ ] App icons follow a switch between light and dark while the app runs. If they stay as they were, refresh them a second time a moment later
- [ ] App icons follow a change of "Icon & widget style" once the window is active again, one after the other
- [ ] Applications and Applications/Utilities have a banner at the top, and its button opens the folder with Apple's apps. That folder has one that leads back
- [ ] Search texts: the empty "Search Everywhere" page says "Find files and folders on this Mac", a search everywhere that finds nothing says "Try other words, or search inside a folder", and neither has a "Search Settings" button. Searching in a folder shows no "Folder Not in Search Locations" note
- [ ] A folder showing "No Permission" loads by itself once access is given and the window is active again (Downloads: allow it in System Settings, click back on the window)
- [ ] Starred files: star a file, find it under Starred, and again after a restart (tinysparql keeps them, not tested)
- [ ] Disks and volumes: what shows when a drive is plugged in

## GNOME 51

- [ ] The memory audit did not cover the files changed by the merge

## Search

- [ ] "Search everywhere" only uses Spotlight. With a broken Spotlight index it finds nothing useful
- [ ] Replace the slow folder walk (49 s for `~/Documents`) with the mac walker, and use it for "search everywhere" too
- [ ] "Search everywhere" skips `~/Library`, `node_modules` and other nested folders like them on its first pass. They are lazy: searched only after the main results, or when searching inside one of them directly
- [ ] Work out the list of lazy folders (candidates: `~/Library`, `node_modules`, `.git`, build output folders, the inside of app bundles)
- [ ] Spotlight provider is untested in home folders (the index on the dev Mac is empty)
- [ ] "Search everywhere" with a Folders or Files filter never returns apps: Spotlight types an `.app` as a package, not a folder (one-line fix in the Spotlight type clause)
- [ ] Type filter: a type macOS does not know matches nothing. 118 of the 207 MIME types in the filter groups are unknown to macOS, so some files are missed (.mkv in Video, .rtf in Documents, .psd in Picture)
- [ ] Type filter: the "Other Type…" list is empty on mac
- [ ] Empty files are typed as text on mac, so an empty `.mp4` shows under Text File, not Video

## Icons

- [ ] A few types have an icon in Yaru under another name than GIO asks for on mac: Python (`text-x-python-script` asked, `text-x-python` there), plain text (`text-*` asked)
- [ ] Credit Yaru in the app (About dialog or bundle). The licence files are already installed with the icons

## Network, trash and recent (gvfs)

- [ ] Trash: a file trashed from the top folder of a drive has no put-back record, so it cannot be restored. Check what Finder does with those
- [ ] Trash: "Restore" and undo of "Move to Trash" need the Trash to be readable, so Full Disk Access. Try both in the window once it is granted
- [ ] `.rar` and `.jar` are not offered for extraction (macOS does not know `.rar`, and a `.jar` is left alone)
- [ ] Saved passwords: gvfs keeps them with libsecret, which mac lacks, so a server asks for its password every time. Possible with the Keychain: gvfs has all of it in one file of three functions (`daemon/gvfskeyring.c`, 274 lines), and the Keychain's "internet password" has the same fields (server, user, protocol, port). A third gvfs patch of about 150 lines, in C (`Security.framework`). Until the app is signed, macOS asks to allow each gvfs helper after every rebuild
- [ ] Trash shows empty until the app has Full Disk Access: macOS blocks `~/.Trash` for every app but Finder, and never asks
- [ ] Recent is empty until files are opened from Nautilus, and opened files are not added to it ("no command line for the application")
- [ ] Scripts and "open in terminal" started from Nautilus still get its private bus in their environment
- [ ] The bus's socket path may be at most 104 characters, a limit of macOS. It is in `~/.cache/nautilus`, so a home folder path of about 80 characters breaks the bus, and with it network, trash and recent

## Permissions

- [ ] The grant is tied to the app's identity, so today it goes to the terminal. Needs the signed `Files.app`. Check that the gvfs daemons get it through the app that started them

## System integration

- [ ] Files cannot be dropped on a tab: upstream only sets that up on Wayland
- [ ] autofs check: confirm `statfs` does not trigger the mount
- [ ] A dialog is 3 px too short (GTK warning). Not seen again: Properties opens without it. Which dialog it was is not known

## Packaging and release

- [ ] Pipeline: `.github/workflows/build.yml` builds on an Apple silicon runner. It is written and has not run yet: push it and fix what fails
- [ ] Apple silicon build of `Files.app` from the pipeline, as a download. Decide on Intel: a second build, a universal one, or none
- [ ] `.dmg` with `Files.app` and a link to Applications, built by the pipeline
- [ ] Wire `macos/bundle/Info.plist.in` and the entitlements into the build to produce `Files.app`
- [ ] App icon (`.icns`)
- [ ] A relocatable `Files.app` has to find its data relative to itself: the bundled icon theme's folder is fixed at build time (`NAUTILUS_DATADIR`), and gvfs's files hold absolute paths (`.mount`, `.service`, rpath)
- [ ] `Info.plist` needs `NSLocalNetworkUsageDescription` and `NSBonjourServices` (the five service types in `nautilus-mac-bonjour.c`), or the bundled app may not look for servers
- [ ] Bundle Adwaita too: Yaru falls back to it for the icons it lacks, and today it comes from Homebrew
- [ ] Homebrew tap formula. It cannot download during a build, so it has to supply the pinned sources itself: libportal, gnome-desktop, libgxdp, blueprint-compiler, dbus, gvfs, Yaru
- [ ] Signing and notarization
- [ ] `mac-release` branch and release tags (`51.0.1-mac.1`)

## Later

- [ ] Send the generic fixes to GNOME (`strrchr`, unused includes, the unused `gxdp-dbus.h` include in `nautilus-portal.c`)
- [ ] Report to GNOME: `nautilus-freedesktop-dbus.c` connects to a signal "destroyed" that no window has ("destroy" is meant), so showing Properties through D-Bus prints an error and keeps the app from quitting
- [ ] Report to GTK: `gdk_surface_thaw_updates` errors when a menu closes on macOS. They are GTK's own: a 30-line GTK program that opens and closes a menu prints them too (GTK 4.24.1). Harmless
- [ ] Real glycin in place of the stand-in. Not only for Rust in the build (the toolchain, and crates downloaded while building): it also needs a 72-line patch to build as a wrap (kept in `.deps/gnome51-research/`), and on mac it then loads PNG and JPEG but not SVG
