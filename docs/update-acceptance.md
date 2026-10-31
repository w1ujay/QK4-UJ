# Update check: owner acceptance checklist

Run this on native Windows (not WSLg) and on Linux. CI already covers the launcher's 37 cases, the Qt unit tests and the
release-script tests, so none of that is repeated here. Each line is one action and what you should see.

The first updater build is `uj.14` (you already released `uj.13` by hand). The end-to-end pair is `uj.14`, then `uj.15`.
At `uj.14` nothing newer exists, so no popup appears until `uj.15` is published.

Tags you must cut: `uj.14`, `uj.15`, and `uj.16` (a pin surviving a second update needs a third build). Failure cases
need deliberately bad builds. Do not publish those as normal releases: test them as local builds, or as a throwaway
numbered release that you supersede with a higher N straight away.

## Decide

- [ ] Cleanup renames an old version folder to `.trash-<random>`, checks again that nothing runs from it, and puts it
  back if something does. Windows allows renaming a running program's folder, which is why it checks twice. Accept or
  change this rule (design, Cleanup section).

## Publish

- [ ] Tag `uj.14`. Expect the workflow to build and leave a draft release.
- [ ] Test the draft: download its zip and run "Windows first run" below.
- [ ] Publish with pre-release unticked and latest ticked. Releases `uj.10` to `uj.13` are pre-releases, so no client
  is offered anything until a full release exists.
- [ ] Tag `uj.15`. In the workflow log, check it downloaded the previous `update.json`, and that `uj.15`'s `update.json`
  history holds `uj.15` and `uj.14`. Publish it the same way.

## Windows first run

- [ ] Unzip `uj.14` into a fresh folder and start `<root>\QK4.exe`. Expect the app to open from the version folder.
- [ ] Run the real `tar.exe` zip check (Task 9, verify-windows-zip) on the release zip. Expect it to pass.
- [ ] Look at `<root>\QK4.exe`, a desktop shortcut to it and the pin. Expect the QK4 icon each time, not a blank one.
- [ ] Pin `<root>\QK4.exe` to the taskbar and start from the pin. Expect it to group with app ID `QK4UJ.QK4`.
- [ ] Start with a saved radio and `--connect <name>`. Expect it to connect to that radio. Nothing newer exists, so no
  popup.
- [ ] Open the Connect dialog in the real app. Expect it to work as before.
- [ ] Start with a real `--qk4-cleanup` run. Expect it to finish quietly and leave the current version alone.
- [ ] First numeric run: `uj.14` checks for updates (numeric version branch). Expect no popup and no error.
- [ ] Note any Windows Firewall prompt on first run. Expect none again after allowing once.

## Windows update

- [ ] With `uj.14` running and `uj.15` published, restart `uj.14`. Expect the popup to offer `uj.15` only, with its
  changelog (the popup lists only releases newer than the running one).
- [ ] In that popup, check `launcherHasIdentity` on real Windows: Install now is shown. A false result hides it
  silently.
- [ ] Radio A set to auto-connect: press Later, Skip or Cancel. Expect auto-connect to run after the popup clears. Do
  the same with Install now. Expect no auto-connect, because the old copy is closing.
- [ ] Make the network slow (block `github.com` in the hosts file, or use a throttled connection). With radio A on
  auto-connect, connect to radio B by hand within the 5-second check. Expect no popup this session, radio A's
  auto-connect cancelled and radio B kept. The update is offered at the next launch.
- [ ] Click Install now. Expect download, restart and `uj.15` running, with `uj.14` kept on disk.
- [ ] Pin the taskbar icon, then update from `uj.15` to `uj.16`. Expect the pin to still work after both updates.
- [ ] Update from a session using a non-default interface scale. Expect the new version to refit, not inherit the scale.
- [ ] Force an auto-scale restart during the update health wait. Expect the update to succeed.
- [ ] Keep another copy on the air during an update. Expect it undisturbed.
- [ ] Run a real elevated QK4 during cleanup. Expect it left alone and no errors.

## Failure and recovery

- [ ] Use a deliberately bad build (see the note at the top) that fails to start. Expect the old version back, and that
  version not offered again.
- [ ] A bad build that never shows its window. Expect the same recovery.
- [ ] GitHub unreachable (offline): expect no popup and no error, and a start delayed by up to about 5 seconds.
- [ ] Recovery by hand: edit `<root>\current.txt` to name the previous version folder, then start `<root>\QK4.exe`.
  Expect the previous version to run.

## Other Windows setups

- [ ] Install under Program Files. Expect no elevation prompt and no Install now. Expect the verified zip saved to
  Downloads, with instructions to unzip it into a folder you own.
- [ ] Install on a drive other than the one holding local app data. Expect update to work.
- [ ] Controlled Folder Access on. Expect update to work, or a clear message if blocked.
- [ ] Antivirus on (Defender and any other in use). Expect no quarantine of the launcher or the update.
- [ ] A second copy of QK4 open during an update. Expect it undisturbed.

## Linux

- [ ] Flatpak: click Download. Expect the portal save dialog, then a `flatpak install --user` command with a Copy
  button. The app stays running.
- [ ] Raspberry Pi (arm64): click Download. Expect the file saved to Downloads and `tar xzf` steps shown.
- [ ] Other Linux (neither Flatpak nor arm64): Expect the release page link and no download.
