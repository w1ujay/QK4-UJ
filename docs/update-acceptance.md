# Update check: owner acceptance checklist

Run this on native Windows (not WSLg) and on Linux. CI already covers the launcher's 37 cases, the Qt unit tests and the
release-script tests, so none of that is repeated here. Each line is one action and what you should see.

The first updater build is `uj.14` (you already released `uj.13` by hand). The end-to-end pair is `uj.14`, then `uj.15`.

## Decide

- [ ] Cleanup renames an old version folder to `.trash-<random>`, checks again that nothing runs from it, and puts it
  back if something does. Windows allows renaming a running program's folder, which is why it checks twice. Accept or
  change this rule (design, Cleanup section).

## Publish

- [ ] Tag `uj.14`. Expect the workflow to build and leave a draft release.
- [ ] Test the draft: download its zip and run through "Windows first run" below.
- [ ] Publish with pre-release unticked and latest ticked. Releases `uj.10` to `uj.13` are pre-releases, so no client
  is offered anything until a full release exists.
- [ ] Repeat for `uj.15` once `uj.14` is installed, for the update tests.

## Windows first run

- [ ] Unzip `uj.14` into a fresh folder and start `<root>\QK4.exe`. Expect the app to open on the version folder's copy.
- [ ] Check `launcherHasIdentity` on real Windows: the update popup shows an Install now button. A false result hides it
  silently, so confirm it is there when an update is offered.
- [ ] Run the real `tar.exe` zip check (Task 9, verify-windows-zip) on the release zip. Expect it to pass.
- [ ] Pin `<root>\QK4.exe` to the taskbar and start from the pin. Expect it to group with app ID `QK4UJ.QK4`.
- [ ] Start with a saved radio and `--connect <name>`. Expect it to connect to that radio.
- [ ] Open the Connect dialog in the real app. Expect it to work as before.
- [ ] Start with a real `--qk4-cleanup` run. Expect it to finish quietly and leave the current version alone.
- [ ] First numeric run: the `uj.14` build checks for updates (numeric version branch) without error.
- [ ] Windows Firewall prompts: note any prompt on first run and after an update. Expect none after allowing once.

## Windows update

- [ ] With `uj.14` running and `uj.15` published, wait for the popup. Expect it to show `uj.15` with its changelog.
- [ ] Radio A set to auto-connect: start QK4 and expect auto-connect to be held while the popup is open, then released
  when you dismiss it or finish.
- [ ] On a slow network with radio A on auto-connect, connect to radio B by hand. Expect radio A not to grab the
  connection.
- [ ] Click Install now. Expect download, restart and `uj.15` running, with the old version kept on disk.
- [ ] First `previous-update.json` download happens at `uj.15`. Expect the changelog to show `uj.14` history.
- [ ] Taskbar pin still works after the update, and again after a second update.
- [ ] Update from a session using a non-default interface scale. Expect the new version to refit, not inherit the scale.
- [ ] Force an auto-scale restart during the update health wait. Expect the update to succeed.
- [ ] Keep another copy on the air during an update. Expect it undisturbed.
- [ ] Elevated copy: run a real elevated QK4 during cleanup. Expect it left alone and no errors.

## Failure and recovery

- [ ] Publish a build that fails to start. Expect the old version back, and that version not offered again.
- [ ] A build that never shows its window. Expect the same recovery.
- [ ] GitHub unreachable (offline): expect no popup and no errors.
- [ ] Recovery by hand: edit `<root>\current.txt` to name the previous version folder, then start `<root>\QK4.exe`.
  Expect the previous version to run.

## Other Windows setups

- [ ] Install under Program Files (read-only for you). Expect manual install instructions, no elevation prompt.
- [ ] Install on a drive other than the one holding local app data. Expect update to work.
- [ ] Controlled Folder Access on. Expect update to work, or a clear message if blocked.
- [ ] Antivirus on (Defender and any other in use). Expect no quarantine of the launcher or the update.
- [ ] A second copy of QK4 open during an update. Expect it undisturbed.

## Linux

- [ ] Run a build on Linux. Expect the popup to download and verify the file, with no install step.
- [ ] Flatpak: use the save option. Expect the file saved and a clear message, since it has no `~/Downloads` access.
- [ ] Raspberry Pi: same check. Expect download and verify only.
