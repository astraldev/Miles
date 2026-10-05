# Nautilus on mac: todo

What is left. What is done and decided is in `port-progress.md`.

## Deferred by the owner

- [ ] Accent colour: a real switch in System Settings has not been tried since the fix. The handler works when the change is simulated. Run with `G_MESSAGES_DEBUG=nautilus-mac` and the log says what arrives and what is read
- [ ] Graphite accent: bark is not the same colour. Yaru has no grey variant. Pick one: make a grey one by taking the colour out of Yaru's folders (turning one icon grey with `sips` works and keeps its transparency), blue as for Multicolour, or sage
- [ ] Types macOS does not know get the blank page: meson.build, .cfg, and files without an extension (LICENSE, NEWS). The icons the known types get are enough for now. Yaru has icons for some (`text-x-meson`). Either a small mac mapping, or bundle the freedesktop MIME database (shared-mime-info) for name patterns and content sniffing

## Check on screen

- [ ] The app: `open .deps/prefix/Applications/Miles.app`. It starts with no terminal, is called Miles in the menu bar and the Dock, and macOS asks for folders in its name
- [ ] About: the port's author, Yaru under Icons and under Legal, and the links going to the GitHub repo
- [ ] Type icons: Python, plain text, shell scripts, C and C++ files, Java, Ruby, PHP, JavaScript, logs and patches have their own icon
- [ ] Recent: open a file from the window, find it under Recent
- [ ] "Uninstall" in an app's menu opens a submenu, "Keep Settings and Data" and "Remove Settings and Data". Each asks first; the second lists the settings and data it found. Not shown for the apps that come with macOS. macOS may refuse to move an app's folder in `~/Library/Containers`: see what it says then
- [ ] Search in the window: in a folder, and "Search Everywhere"
- [ ] Double-click on a `.zip` extracts it in place, with no Finder window
- [ ] A folder the account may not read shows "No Permission", with no dialog and no "admin" message
- [ ] App icons follow a switch between light and dark while the app runs. If they stay as they were, refresh them a second time a moment later
- [ ] App icons follow a change of "Icon & widget style" once the window is active again, one after the other
- [ ] Applications and Applications/Utilities have a banner at the top, and its button opens the folder with Apple's apps. That folder has one that leads back
- [ ] Search texts: the empty "Search Everywhere" page says "Find your files and apps by name", a search everywhere that finds nothing says "Try different words, or search inside a folder", and neither has a "Search Settings" button. Searching in a folder shows no "Folder Not in Search Locations" note
- [ ] A folder showing "No Permission" loads by itself once access is given and the window is active again (Downloads: allow it in System Settings, click back on the window)
- [ ] Starred files: star a file, find it under Starred, and again after a restart (tinysparql keeps them, not tested)
- [ ] Disks and volumes: what shows when a drive is plugged in

## Search

- [ ] Search in file contents is gone with Spotlight. The "Full Text" choice in the search popover is still shown and does nothing: hide it
- [ ] The walker's type filter goes by the file's name, not its content, so a file without an extension passes no type filter
- [ ] Type filter: a type macOS does not know matches nothing. 118 of the 207 MIME types in the filter groups are unknown to macOS, so some files are missed (.mkv in Video, .rtf in Documents, .psd in Picture)
- [ ] Type filter: the "Other Type…" list is empty on mac
- [ ] Empty files are typed as text on mac, so an empty `.mp4` shows under Text File, not Video

## Icons


## Network, trash and recent (gvfs)

- [ ] Trash: a file trashed from the top folder of a drive has no put-back record, so it cannot be restored. Check what Finder does with those
- [ ] Trash: when several files are trashed at once, macOS writes a put-back record for the first one only (seen on a disk image: three at once gave one record, three a second apart gave three). The others cannot be restored. Check the home trash, and try `NSWorkspace recycleURLs:`, which takes them as one batch
- [ ] From the audit, not fixed yet: the Uninstall dialog keeps a pointer to its view, which can be gone if the tab closes under it; the bus's process number is read at start and signalled at exit without a second look; Bonjour names are shown without checking they are UTF-8; the sidebar connects its window hook again on every re-root; a few unchecked CoreFoundation returns
- [ ] Trash: "Restore" and undo of "Move to Trash" need the Trash to be readable, so Full Disk Access. Try both in the window once it is granted
- [ ] Trash shows empty until the app has Full Disk Access: macOS blocks `~/.Trash` for every app but Finder, and never asks
- [ ] Scripts and "open in terminal" started from Nautilus still get its private bus in their environment
- [ ] The bus's socket path may be at most 104 characters, a limit of macOS. It is in `~/.cache/nautilus`, so a home folder path of about 80 characters breaks the bus, and with it network, trash and recent

## Permissions

- [ ] The grant is tied to the app's identity, so today it goes to the terminal. Needs the signed `Files.app`. Check that the gvfs daemons get it through the app that started them

## System integration

- [ ] Files cannot be dropped on a tab: upstream only sets that up on Wayland
- [ ] autofs check: confirm `statfs` does not trigger the mount
- [ ] A dialog is 3 px too short (GTK warning). Not seen again: Properties opens without it. Which dialog it was is not known

## Packaging and release

- [ ] Pipeline: `.github/workflows/release.yml` makes the disk image on an Apple silicon runner. A tag like `51.0.1-mac.1` drafts a release with it; run by hand it only keeps the image with the run. It has not run yet: run it by hand and fix what fails
- [ ] Decide on Intel: a second build, a universal one, or none
- [ ] Open the disk image (`build/Miles-51.0.1.dmg`) and look at its window: the picture, the two icons, the names under them in the light and the dark appearance
- [ ] Before a binary goes out (from the licence audit): a `THIRD-PARTY-NOTICES.md` and a `SOURCES.md` beside the release, with each package, version, licence and source; About and README must say this is a modified version, not made by GNOME; the three credit sentences (FreeType, libjpeg, ICU). The licence files themselves are already copied into the app
- [ ] From the licence audit, lesser: a copyright line in the port's files, a licence for the icon and the scripts, the Apple logo in a README badge, the wording "GNOME's file manager", whether `astralco.com` is ours, and asking GNOME and weighing Apple about the icon
- [ ] App icon: it is one fixed picture (`macos/bundle/AppIcon.svg`). The dark, clear and tinted styles of macOS need an Icon Composer `.icon` file
- [ ] A build on this Mac only runs on macOS 26 or newer, as Homebrew's libraries are built for the system they are installed on. The pipeline has to build on the oldest runner
- [ ] The app's name: Miles for now, set in one place (`macos_app_name` in `meson.build`). Nomtilus and GMacFiles were the other ideas. Texts inside the window still say Files
- [ ] The translations of GTK, libadwaita, GLib and gvfs are not in the movable app: those libraries look in the folder they were built for, and no setting changes it. Their texts show in English. A way: bind those text domains again after the libraries start
- [ ] The movable app was only run from the terminal, with Homebrew and the build folders hidden by a sandbox. Try it on screen, and on a Mac that never had Homebrew
- [ ] The certificates in the movable app are Mozilla's list as Homebrew ships it. A certificate the user added to the Keychain is not trusted by it
- [ ] tinysparql's path to its parser is changed inside the copied library (`bundle-app.sh`). It fails loudly if a new tinysparql holds the path differently. The clean way is a setting in tinysparql
- [ ] Homebrew tap formula. It cannot download during a build, so it has to supply the pinned sources itself: libportal, gnome-desktop, libgxdp, blueprint-compiler, dbus, gvfs, Yaru
- [ ] Signing and notarization: the scripts sign with `CODESIGN_IDENTITY` when it is set, and by no one otherwise. Needs the Apple Developer ID, then the notary step in the workflow
- [ ] `mac-release` branch and release tags (`51.0.1-mac.1`)

## Later

- [ ] Send the generic fixes to GNOME (`strrchr`, unused includes, the unused `gxdp-dbus.h` include in `nautilus-portal.c`)
- [ ] Report to GNOME: `nautilus-freedesktop-dbus.c` connects to a signal "destroyed" that no window has ("destroy" is meant), so showing Properties through D-Bus prints an error and keeps the app from quitting
- [ ] Report to GTK: `gdk_surface_thaw_updates` errors when a menu closes on macOS. They are GTK's own: a 30-line GTK program that opens and closes a menu prints them too (GTK 4.24.1). Harmless
- [ ] Real glycin in place of the stand-in. Not only for Rust in the build (the toolchain, and crates downloaded while building): it also needs a 72-line patch to build as a wrap (kept in `.deps/gnome51-research/`), and on mac it then loads PNG and JPEG but not SVG
