# Fork Update Check: Design

Date: 2026-10-31
Status: approved 2026-10-31

## Why

QK4-UJ (`w1ujay/QK4-UJ`) reaches its users as hand-built GitHub Releases. Nobody finds out about a new
build unless they are told, and updating is a manual download and unzip. This feature lets a person
launching QK4 see that a new version exists, read what changed, and decide whether to take it. On
Windows, taking it is one click.

## Goals

- At startup, QK4 checks for a newer fork release.
- If there is one, a popup shows the changelog and offers the update.
- Windows: one click downloads, verifies, installs and relaunches.
- Linux: one click downloads and verifies the right file, then shows how to finish installing.
- The changelog comes automatically from commits on the fork.
- A `uj.N` tag drives a repeatable release, replacing the hand-built ones.

## Non-goals

- macOS. It gets no check and no popup.
- Any check after startup, including a timer or a "Check for updates" menu item.
- Anything going upstream. No PR, no issue, and no change to `mikeg-dal/QK4`.
- Code signing, an installer (MSI/Inno), or a Flatpak repository.
- Silent or automatic installs. A person always clicks.
- Delta updates. Each update is a whole package.

## Anti-goals

These make the feature a failure even if it otherwise works:

- A popup or update activity while a radio is connected.
- A slow or broken GitHub connection delaying or preventing use of the radio.
- A failed update leaving someone without a working QK4.
- An update landing on another running copy of QK4.
- Fork-only work creating recurring upstream merge conflicts.

## Decisions made by the owner

- Fork only, and all of it stays out of upstream.
- Audience is Windows and Linux.
- Check at startup only, when no radio is connected.
- Auto-connect is blocked until the update prompt is cleared.
- Popup layout A: the changelog is visible immediately, not behind a link.
- The changelog is generated from commits on the fork.
- "No radio connected" applies per copy of QK4. A second copy can show the popup while another is on the air, and
  installing does not disturb the copy on the air. Detecting other copies is left for later.

## Calls made by the author, not the owner

- Popup buttons: Later asks again next launch, and Skip stays quiet until a higher version.
- The 5-second check limit, with auto-connect waiting behind it.
- Windows uses one folder per version and a small launcher instead of replacing files in place.
- A failed update returns to the old version, including a new build that shows no window within 60 seconds or crashes
  within 10 seconds of showing it, and that version is not offered again.
- If the closing copy of QK4 hangs for 30 seconds during an install, the launcher terminates it.
- Command-line output and exit codes through the launcher are given up. The version's own `QK4.exe` keeps them.
- A short update lock serializes shared steps, and a normal launch proceeds anyway if it cannot get the lock in 10 seconds.
- The current version and the one before it are kept on disk.
- Linux downloads and verifies a file but does not install it.
- A new `release-uj.yml`, with releases held as drafts until the owner publishes them.
- macOS left out of the fork release workflow.
- A rejected draft's tag is deleted and its number reused.
- Merged upstream commits collapse to one changelog line.
- No admin elevation. A read-only install folder gets manual install instructions.
- No option to turn the check off in this version.

## Current state

- `release.yml` runs only on `v*` tags, so `uj.N` releases never use it. They are assembled by hand
  from `Build · Signed` output. Assets are named like `QK4-main-3d4e37b-windows-x64.zip`.
- The fork has no gh-pages site. The Windows and macOS builds are not code signed on the fork.
- Builds report versions like `main-3d4e37b`, which cannot be ordered.
- `CMakeLists.txt` says `project(QK4 VERSION 0.7.0)`, behind the actual `v0.8.0-beta.1` base.
- `MainWindow`'s constructor ends with `QTimer::singleShot(0, this, &MainWindow::connectToStartupRadio)`,
  which handles both a saved radio's connect-at-startup flag and `--connect <name>`.
- There is no single-instance guard, and `--connect` exists so one install can have a shortcut per K4.
  Several copies can run from one folder.
- `src/main.cpp` can restart QK4 before showing the window, through `InterfaceScale::relaunch()`, which reuses
  `applicationFilePath()` and the same arguments.
- `k4discovery.cpp` binds UDP on all interfaces, and the WebSocket server can too.
- `K4PopupBase` positions popups against an anchor widget. Free-standing windows use `QDialog`
  (`src/ui/dialogs/radiomanagerdialog.h`).
- Upstream's `cliff.toml` matches only `v[0-9].*` tags.
- The Flatpak has `--share=network` but no filesystem access, so it cannot write to `~/Downloads` directly.

## Versioning

- Fork release tags stay `uj.N`, continuing from `uj.12`.
- The stamped version is `<base>-uj.<N>`, for example `0.8.0-uj.13`. `<base>` is the numeric part of the
  nearest `v*` tag.
- Ordering uses `N` alone. It is a plain integer, so it is unaffected by upstream's version scheme.
- A build whose `QK4_VERSION` does not end in `-uj.<N>` (branch, tester and local builds) never checks.

## Release pipeline

A new workflow, `.github/workflows/release-uj.yml`, runs on `uj.*` tags. `release.yml` is not edited.

Manual dispatch cannot rehearse it, because GitHub only dispatches workflows present on the default branch and this
stays off the fork's `main` until tested. A tag push uses the workflow in the tagged commit, so a rehearsal is a
tag such as `uj.test-1` on the feature branch. Non-numeric tags build a draft in the same way, stamp a version that
never checks for updates, and are ignored by the changelog generator. The draft and tag are deleted afterwards.

It:

1. Derives the version.
2. Calls `_build-windows.yml`, `_build-pi.yml` and `_build-flatpak.yml`. macOS is not built here, and
   `Build · Signed` still builds it on pushes to `main`.
3. Generates the changelog (below).
4. Generates `SHA256SUMS.txt` and `update.json`.
5. Creates a **draft** GitHub Release containing all assets.

The owner downloads and tests the draft, then publishes it with one click. Publishing is what exposes it to
users, so a tag alone never prompts anyone. The release must be marked as a full release and as latest,
because `releases/latest` ignores prereleases.

If a draft is rejected, its draft and tag are deleted and the same `N` is tagged again on the fix, so a rejected
build leaves no gap and its commits stay in the next changelog.

If a published version is bad, the fix is a higher `uj.N`. There is no downgrade path beyond the automatic restore
when a new build fails to start.

### `update.json`

Attached to every release and fetched from the stable URL
`https://github.com/w1ujay/QK4-UJ/releases/latest/download/update.json`. This avoids GitHub's anonymous API
rate limit and needs no gh-pages site.

```json
{
  "schema": 1,
  "version": "0.8.0-uj.13",
  "uj": 13,
  "date": "2026-11-02",
  "assets": {
    "windows-x64": { "name": "...", "url": "...", "size": 44841368, "sha256": "..." },
    "linux-flatpak-x86_64": { "name": "...", "url": "...", "size": 1855152, "sha256": "..." },
    "raspberry-pi-arm64": { "name": "...", "url": "...", "size": 22799132, "sha256": "..." }
  },
  "history": [
    { "uj": 13, "version": "0.8.0-uj.13", "date": "2026-11-02",
      "added": ["..."], "fixed": ["..."], "changed": ["..."] }
  ]
}
```

`history` holds the most recent 20 releases, newest first, so someone several versions behind sees everything
they would gain. A file with an unknown `schema` is ignored silently.

### Changelog

Generated for the range between consecutive numeric `uj.N` tags. Added, Fixed and Changed follow CLAUDE.md's
mapping (`feat`, `fix`, `refactor`/`perf`), and other types are left out.

The generator handles commits merged from upstream as described in "Upstream commits in the changelog" below.
`cliff.toml` is not edited. A fork-only config or a script under `scripts/` holds this logic.

`uj.13` has no `uj.N` predecessor with an `update.json`, so its history starts at `uj.13`.

## App behaviour

### Startup sequence

1. The window appears as usual.
2. If the version is a `-uj.<N>` build on Windows or Linux, `MainWindow` starts the check instead of
   scheduling `connectToStartupRadio`.
3. The check has a hard 5-second limit. No update, a network failure, a bad file or a timeout all produce no
   UI and a log line only.
4. If the person began a manual connection during the 5 seconds, whether or not it succeeded, the check is over for
   this session: no popup appears and the startup auto-connect is cancelled. `ConnectionController::connectToRadio`
   drops an existing connection, so running it here would replace the radio they chose. Any update is offered on
   the next launch.
5. Otherwise, one of these happens:
   - Nothing to offer: `connectToStartupRadio` runs.
   - Update found: the popup appears, and `connectToStartupRadio` runs after it is cleared by Later, Skip or
     Cancel. It does not run after a successful Install, because QK4 is closing.
6. There is no other check until the next launch.

The check runs once per process, and each copy of QK4 makes its own.

A pending startup connection and a failed `--connect <name>` lookup still behave exactly as they do today, just
later. A saved radio or `--connect` user sees at most about 5 seconds of extra delay when GitHub is unreachable.

### Popup

A modal `QDialog`, centred on the main window, because it has no anchor button. Styled with `K4Styles` per
CLAUDE.md, and matching layout A in the approved mockup:

- Title: "QK4 update available"
- Current and new version
- Scrolling changelog grouped by release when more than one is new, showing every release newer than the
  running one that `history` contains
- Buttons: Skip this version, Later, and Install now (Windows) or Download (Linux)

Changelog text is rendered as plain text, never as HTML or rich text.

- **Later** asks again next launch.
- **Skip this version** stores that `N` in `QSettings`. The popup returns only for a higher `N`.
- **Install now / Download** swaps the buttons for a progress bar and Cancel. Cancel returns to the popup's
  normal buttons. Connecting stays blocked while the dialog is open because it is modal.

Download errors show in the dialog with a Retry button and the same Later choice.

### Download rules

- Only `https` URLs under `https://github.com/w1ujay/QK4-UJ/releases/download/` are accepted. GitHub's redirect
  to its storage host is allowed.
- File size and SHA-256 must match `update.json`. A mismatch deletes the file and shows an error.
- These rules detect damage. Because builds are unsigned and `update.json` comes from the same release, they do
  not protect against a compromised GitHub account. That risk is accepted.

### Windows install

Each version lives in its own folder, and installed files are never replaced. Windows cannot safely replace files that
a running copy may have loaded, and earlier designs that swapped files in place kept failing on that. Here, a failed
update never touches the working version.

```text
QK4\
  QK4.exe              launcher, with no Qt dependency
  current.txt          name of the version folder to run
  versions\
    0.8.0-uj.13\       complete app, including its own QK4.exe
    0.8.0-uj.14\
```

**Normal launch.** Holding the update lock (below), the launcher reads `current.txt` and starts that version's `QK4.exe` with
its own arguments, then releases the lock and exits. Arguments use wide-character APIs and correct quoting, so spaces,
non-ASCII radio names and trailing backslashes all survive. Existing shortcuts to `QK4.exe` keep working. If `current.txt`
is missing or names no folder with a `QK4.exe`, the launcher runs the highest `N` containing `QK4.exe`, preferring folders
with `.healthy`. If there is none, it shows a visible error. If the lock cannot be taken within 10 seconds, it launches
anyway, because a problem with the update lock must never stop someone reaching their radio.

**Managed installs.** QK4 offers Install now only when all of these are true:

- It is running from `<root>\versions\<name>\`.
- `<root>\current.txt` exists.
- `<root>\QK4.exe` is the launcher, confirmed by an identifying string in its Windows version resource.
- Creating a test file succeeds in both `<root>` and `versions\`. Qt's `isWritable` is not used, because it ignores
  NTFS permissions by default.

Otherwise (an old-style folder, or a read-only one such as Program Files) it does not elevate. It saves the verified zip
to the Downloads folder and explains how to unzip it into a folder the person owns.

**Version folder rule.** A folder only reaches its final `versions\<version>` name by an atomic rename after extraction
has finished, so a folder at that name is always complete. Installing never deletes one. If it already exists, it is
reused.

**Update lock.** Steps that read and change shared state run one at a time under an OS file handle on `<root>\.update.lock`,
opened with no sharing. It is held only for moments and never across a wait for a process, a download or the health check.
The kernel releases it if the holder dies, so a stale lock is impossible, and the file itself means nothing. It covers
exactly these operations:

- selecting a version and creating its process, in a normal launch
- reading, comparing and replacing `current.txt`
- deciding whether a failed attempt's folder can go, and renaming it away
- cleanup's check of a folder and its rename

Retries release and retake the lock and re-read `current.txt` each time. A process is visible by its path as soon as it
is created, so cleanup under the lock always sees a launch that went before it.

**Install.**

1. QK4 re-reads `current.txt`. If its `N` is at or above the offered `N`, another copy has already updated, so it installs
   nothing and asks the person to reopen QK4.
2. It downloads the zip to a uniquely named file under local app data and verifies it.
3. It extracts the package's version folder to a uniquely named `versions\.incoming-<random>`. Every zip entry must stay
   inside that folder, and the version folder's name must match `update.json`. Any problem deletes the temporary folder
   and shows an error. The downloaded zip is deleted whatever the outcome. Windows `tar.exe` reads its arguments in the ANSI
   code page, which cannot hold every profile or install path, so the zip first moves to `versions\.package-<random>.zip` and
   tar runs inside the temporary folder with only relative paths. Any `.package-*` file more than a day old, left by a crash,
   is removed when an install starts.
4. It confirms the app's `QK4.exe` is present, then renames the folder to `versions\<version>`. If that name already
   exists, it discards its own folder and carries on with the existing one.
5. It starts the launcher in finish-update mode with the new version, its own process handle and its original arguments.
   It removes `QT_SCALE_FACTOR` if QK4 set that variable itself, and always removes `QK4_SCALE_RELAUNCHED`, using the same
   rule `InterfaceScale::relaunch()` already applies, so the new version fits itself to the screen. Then QK4 exits. If the
   launcher cannot be started, QK4 stays open and shows the error. The complete folder stays for a later attempt or cleanup.
6. The launcher waits for that QK4 process to exit, so the new version can claim the USB devices and listening ports. If
   it has not exited after 30 seconds, the launcher terminates that one process. No radio is connected, and the person
   already asked to install. Other running copies are left alone and carry on from their old folders.
7. It creates a Windows job object for this attempt and starts the new version inside it, with the original arguments and
   a random attempt ID in the `QK4_UPDATE_ATTEMPT` environment variable. If that fails, or the folder has gone, it goes to
   step 10.
8. It waits up to 60 seconds for the health check (below).
9. **Healthy:** it writes `.healthy` into the version folder, removes its readiness file and commits. Under the update lock,
   it reads `current.txt`, and replaces it only if the new `N` is higher. Then it exits, leaving the job's processes running.
10. **Not healthy:** it terminates this attempt's job, which stops only the processes this attempt started, and waits for
    them to exit. Then, under the update lock, it decides what happens:
    - If `current.txt` is already at or above this `N`, another attempt succeeded. It writes no failure note, leaves the folder
      and starts the current version with the original arguments.
    - Otherwise, if another process is still running from the folder, another attempt is still testing it. It leaves the
      folder, writes the failure note and starts the previous version.
    - Otherwise it renames the folder to `.trash-<random>`, writes the failure note and starts the previous version. The
      rename's failure does not stop this. The delete happens outside the lock.

    The note is written unless one already names this version, and the original arguments go to whichever version starts.

The next launch of the previous version shows the failure note once and records that `N` as failed, so a bad build is not
offered again. A higher `N` is offered as usual.

**Health check.** Readiness must come from this attempt, so a marker left in a reused folder proves nothing. When
`QK4_UPDATE_ATTEMPT` is set, QK4 writes `.ready-<attempt>` into its version folder after the main window is first shown and
before any startup connection, then clears the variable so later restarts do not repeat it. Without the variable, it writes
nothing. `.healthy` only records that a version once passed, and only the launcher's fallback selection reads it.

- Once `.ready-<attempt>` appears, the launcher watches the job's processes for 10 more seconds. This catches a build that
  crashes on first render. If all exit and any exit code is non-zero, the result is Not healthy. A clean exit counts as
  healthy, so someone closing the window is not treated as a failure.
- If no readiness file appears within 60 seconds but a process in the job owns a visible top-level window, the launcher
  treats it as healthy. A failed file write, for example under Controlled Folder Access, must never cut off a working session.
- A crash after those 10 seconds is not caught. Recovery is by hand: point `current.txt` at the previous version, which is
  kept for this reason.

**Auto-scale restart.** `src/main.cpp` can close a process before showing the window and start a replacement from the
same path, with the same arguments. The replacement inherits the job (`QProcess::startDetached` does not break away,
confirmed on real Windows by the launcher's `restartThenFailStops` test), and it inherits `QK4_UPDATE_ATTEMPT` through the environment `InterfaceScale::relaunch()` passes on. So it writes the
readiness file, and terminating the job stops it too. The launcher treats the attempt as failed early only if the job has
had no running process for 5 seconds.

**Launches during an update.** A launch reads `current.txt` and gets the old version until the new one is proven, so no
launch gate is needed and nothing can load a half-installed version.

**Older copies.** A copy running an old version compares the offer against `current.txt` as well as its own `N`. If
`current.txt` is already at or above the offer, it shows no popup. Because every commit compares under the update lock,
`current.txt` only moves forward, even when attempts for different versions finish together.

**Two copies clicking Install.** Unique downloads and temporary folders keep them apart, and the version folder rule
means neither deletes the other's work. Each attempt has its own job and readiness file, so one failing cannot stop the
other's processes. Each copy relaunches itself.

**Cleanup.** At startup, a managed build runs cleanup only if its own version matches `current.txt`, so a new version
under test never cleans anything. It keeps the current version and the next lower `N`, and never touches a higher `N`
unless that `N` is recorded as failed. For each other folder, under the update lock, it checks that no process is running
from it and renames it to `.trash-<random>`. NTFS allows the folder of a running exe to be renamed, so it checks again after
the rename and puts the folder back if anything is running from it; rollback's rename does the same. A folder counts as in
use if a process is seen running from it or if its `QK4.exe` cannot be opened for writing. The second test needs no access to
the process, so it also finds an elevated copy, and a false alarm only leaves the folder for later. Cleanup deletes the renamed
folder after releasing the lock, and leaves any `.trash-*` folder that is in use. It also removes `.trash-*`
folders, and `.incoming-*` folders more than a day old. A folder in use stays until a later launch.

Normal launches only select the current version, and they hold the same lock while creating the process. Running an old
version's `QK4.exe` directly bypasses the launcher and the lock. That tiny race is accepted.

**Transient locks.** Antivirus and the search indexer briefly hold newly written files. The folder rename, the commit
and every delete retry with backoff for up to 30 seconds. If the commit still fails, the new version keeps running, the next
launch gets the old version, and the update is offered again, reusing the existing folder and repeating the health check.

**Interruptions.** `current.txt` is replaced by writing a uniquely named temporary file, flushing it and calling
`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`. A crash or power loss therefore leaves either the old
pointer or the new one, and the launcher's fallback handles a damaged file. An interrupted update is offered again on
the next launch.

**Launcher.** A small standalone Windows project in `tools/windows-launcher/`, with no Qt dependency and a statically
linked C runtime. It is built by its own job in `release-uj.yml`, which also assembles the zip in the layout above, so no
upstream-owned workflow changes. Updates do not replace it, so it stays small enough to rarely change. A change to the
launcher ships as a fresh manual install.

**Taskbar pins.** With no installer shortcut, a pin resolves to the running exe, which would tie it to one version folder
that cleanup later removes. On Windows, QK4 sets an explicit app ID and `System.AppUserModel.RelaunchCommand` on its window,
pointing at the launcher.

**Command line.** Because the launcher exits straight away, QK4 started through it has no parent console, so `--version`
and usage errors appear in a message box and the exit code is lost. This is accepted. Running the version's own `QK4.exe`
still behaves as it does today.

User settings are not touched. They live in `QSettings` under the user's profile, not the install folder. A code search
found no `applicationDirPath` or `QStandardPaths` use in `src/` on this branch, and the builder rechecks after the upstream
merge.

### Linux download

| Running as | Detected by | Asset | After download |
|---|---|---|---|
| Flatpak | `FLATPAK_ID` set or `/.flatpak-info` exists | `linux-flatpak-x86_64` | Show the `flatpak install --user` command with a Copy button |
| Linux on arm64 | CPU architecture | `raspberry-pi-arm64` | Show the `tar xzf` steps |
| Anything else | Fallback | none | Show the release page link, no download |

Flatpak saves through the portal's save dialog, since the sandbox cannot reach `~/Downloads`. The Pi build saves to
the Downloads folder. In every Linux case the app does not install anything itself and stays running.

## Code layout

To keep upstream merges cheap:

- New code goes in a new `src/update/` directory, plus `src/ui/dialogs/` for the dialog.
- Logic is split from UI, so that version comparison, `update.json` parsing, the offer decision, asset choice and
  URL checks can be tested without a network or a window.
- The `MainWindow` hook is a few lines around the `singleShot`. `src/mainwindow.cpp` is already an upstream
  conflict file, so the change stays small.
- `src/main.cpp` and the upstream-owned workflows need no changes.
- `CMakeLists.txt` and `tests/CMakeLists.txt` get only additive lines.
- The fork's repository URL is a single constant.

## Testing

- Qt Test unit tests cover the logic above, including malformed `update.json`, skipped versions and non-`uj` builds.
- Local build and tests run with `ctest`. clang-format passes.
- `release-uj.yml` is rehearsed with a `uj.test-*` tag first.
- The owner tests on native Windows, not WSLg.
- End-to-end needs two real releases: install `uj.13` by hand, publish `uj.14`, then update through the popup.
  Also covered: a second copy of QK4 open, a read-only install folder, GitHub unreachable, and a saved radio with
  auto-connect on.
- Windows update cases, each run for real:
  - a new build that fails to start, and one that never shows its window, confirming the old version returns and that
    version is not offered again
  - a build that crashes just after showing its window
  - a failed marker write with a working window, confirming the session is not stopped
  - forced auto-scale restart during the health wait, confirming the update succeeds, and the same with a build that
    then fails, confirming the replacement process is stopped
  - after an update from a scaled session, confirming the new version refits rather than inheriting the old scale
  - a shortcut launched while the updating copy is still closing, confirming the new version survives cleanup
  - an old copy with its popup open while another updates, confirming it cannot reinstall or move `current.txt` back
  - a closing copy that hangs
  - another copy on the air during an update, confirming it is undisturbed
  - launching another copy during the health wait, confirming it runs the old version
  - two copies clicking Install together, including one succeeding while the other fails to start, confirming the working
    copy is untouched
  - attempts for different versions committing together, confirming `current.txt` ends on the higher one
  - a hang before the window appears in a reused folder holding a `.healthy` file, confirming it does not pass
  - cleanup paused after its process check while the launcher starts that version, confirming the running copy is unharmed
  - the launcher killed mid-update, then a retry
  - a corrupt zip, and a zip with entries outside the version folder
  - relaunch through `--connect` shortcuts after both success and failure, using names with spaces, non-ASCII
    characters and a trailing backslash
  - a read-only root with a writable `versions\`, and an old-style `QK4.exe` at the root
  - a damaged or missing `current.txt`
  - an install on a drive other than the one holding local app data
  - cleanup with an old version running, started through the launcher, confirming that copy is unharmed
  - Windows Firewall prompts, and a taskbar pin surviving two updates
- Linux end-to-end covers the Flatpak and Pi paths.

## Rollout

- The first updater build must be installed by hand into a fresh folder, because it introduces the version folder
  layout. Existing `uj.12` users cannot be prompted, and their shortcuts need pointing at the new folder.
- Work happens on a feature branch from the current branch tip, pushed to `ujay` only. No PR, fork included, until
  the owner has tested and reviewed it.
- Upstream's `development` is merged after this feature is built, and real testing happens on the merged result.
  That merge is a separate task. Trial merge on 2026-10-31: 22 conflicted files, including `src/mainwindow.cpp`,
  `CMakeLists.txt` and `tests/CMakeLists.txt`.

## Known risks

- `flatpak / build` failed on this branch's PR run on 2026-10-20 with `fatal: unable to read tree`, while checking out
  the source inside the Flatpak build. The new workflow uses the same builder, so a rehearsal must prove it passes on a
  tag.
- Antivirus or SmartScreen may flag the unsigned launcher or a newly unpacked version. This can only be checked on real
  Windows. If it blocks an update, the health wait fails and the old version returns.
- Windows Firewall ties rules to a program's path, so it may prompt again after each update because the version folder
  changes. Discovery and the WebSocket server are the likely triggers. This is accepted unless testing shows it is
  disruptive.
- A crash more than 10 seconds after the window appears, such as one that only happens once a radio connects, is not rolled
  back. Recovery means editing `current.txt` by hand, so the release notes must explain it.
- Windows job objects depend on child processes not breaking away. `QProcess::startDetached` was confirmed not to, on Qt 6.10.
  The launcher's `restartThenFailStops` test keeps checking it, using the same calls as `InterfaceScale::relaunch()`.
- Upstream code merged later must not assume files sit beside the launcher at the install root.

## Left to the builder

- Launcher internals and how the zip is extracted.
- Exact file and class names.
- Dialog sizing and how the history groups are drawn.
- Whether the changelog generator is a script or a git-cliff config.
- How rehearsal output is presented.

## Upstream commits in the changelog

Commits authored on the fork are listed individually. Commits that arrived through a merge from upstream are not, because
one merge can add 179 entries. They are collapsed to a single line, such as "Includes 179 upstream changes". A commit counts
as upstream if it is reachable from `mikeg-dal/QK4`'s `development` branch.
