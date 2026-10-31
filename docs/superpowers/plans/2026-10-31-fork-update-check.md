# Fork Update Check Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** At startup, QK4-UJ offers newer fork releases in a popup with a changelog, installs them with one click on
Windows, and downloads the right file on Linux.

**Architecture:** Pure decision logic in `src/update/` and `tools/windows-launcher/updatelayout.*` is unit tested on Linux.
`UpdateFlow` holds `MainWindow`'s startup connection until the check clears. Windows runs each version from its own
folder, through a small Qt-free launcher that also owns every Win32 race-sensitive step: the update lock, job objects,
committing `current.txt`, and cleanup. `release-uj.yml` builds the assets, changelog and `update.json` as a draft release.

**Tech Stack:** C++17, Qt 6.7+ (Core, Network, Widgets, Test), Win32 (launcher only), GitHub Actions, Bash, jq.

**Spec:** `docs/superpowers/specs/2026-10-31-fork-update-check-design.md`. Read it before starting. It is the authority on
behaviour. This plan fixes names, files and order.

## Global Constraints

- Branch `feat/update-check`. Push to the `ujay` remote only, never `origin`. Open no PR, fork included, until the owner has
  tested and reviewed.
- Conventional commits, summary under 72 characters. **No `Co-Authored-By` or other AI trailers** (user rule).
- clang-format must pass: `find src -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror`.
- Configure: `cmake -B build-update -DCMAKE_PREFIX_PATH="$HOME/6.8.1/gcc_64" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`.
- CTest does not build. Every test run in this plan builds its target first, so it can never pass on a stale binary.
- Naming: classes PascalCase, members `m_camelCase`, constants SCREAMING_SNAKE, slots `on<Source><Event>`.
- All colours and dimensions from `K4Styles`. Font sizes in pixels.
- Windows is tested on native Windows, not WSLg.
- Do not edit `release.yml`, `build-signed.yml`, `_build-*.yml`, `cliff.toml` or `src/main.cpp`. Other upstream-owned files get
  the smallest additive change.
- Repository constant: `w1ujay/QK4-UJ`, in one place (`UPDATE_REPOSITORY`).
- Exact values: check limit 5 s; health wait 60 s; post-readiness watch 10 s; empty-job grace 5 s; closing-copy limit 30 s;
  lock wait 10 s; retry window 30 s; `.incoming-*` age 1 day; `history` 20 releases.

## Decisions This Plan Adds

- Cleanup is launcher mode `--qk4-cleanup`, started detached by QK4, so all lock and process code stays in one Win32 program.
- The launcher receives QK4's process ID, not a handle, and opens it at once. `startDetached` cannot pass a handle, and QK4
  is still running at that moment. An `OpenProcess` failure means already exited.
- Windows extraction uses `%SystemRoot%\System32\tar.exe`, which is present on Windows 11. Entries are listed and validated
  first, then extracted. This avoids Qt's private `QZipReader`. The exact command, which selects the version subtree and strips
  its three leading folders so `QK4.exe` lands directly in the temporary folder, is:
  `tar.exe -xf <zip> -C <incoming> --strip-components=3 QK4/versions/<version>`
- Manual connections are recorded synchronously in `MainWindow::connectToRadio`, the only route to
  `ConnectionController::connectToRadio`. `connectionStateChanged` comes from the I/O thread and arrives too late, so `UpdateFlow`
  does not watch it.
- Readiness and cleanup both run from `UpdateFlow::start()`, the first event-loop pass after `window.show()`.

## Review Focus

1. A captive portal or proxy answers `update.json` with HTTP 200 and an HTML page: nothing appears, and startup continues.
2. Someone more versions behind than `history` covers: the popup shows what it has and says older changes are not listed.
3. A release with no user-visible commits: the popup says "No listed changes", not an empty box.
4. The disk fills during extraction: an error appears, temporary files are removed, and the old version is untouched.
5. An install path with spaces, non-ASCII characters or a long folder name: install, launch and relaunch all work.

---

### Task 1: Version parsing and manifest parsing

**Files:**
- Create: `src/update/updateversion.h`, `src/update/updateversion.cpp`
- Create: `src/update/updatemanifest.h`, `src/update/updatemanifest.cpp`
- Create: `tests/test_updatemanifest.cpp`
- Modify: `CMakeLists.txt` (SOURCES, HEADERS), `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `namespace UpdateVersion { inline constexpr char UPDATE_REPOSITORY[] = "w1ujay/QK4-UJ"; std::optional<int> parseUjNumber(const QString &version); }`
  - `struct UpdateAsset { QString name; QUrl url; qint64 size; QByteArray sha256Hex; };`
  - `struct UpdateRelease { int uj; QString version; QString date; QStringList added, fixed, changed; };`
  - `struct UpdateManifest { int uj; QString version; QHash<QString, UpdateAsset> assets; QList<UpdateRelease> history; };`
  - `std::optional<UpdateManifest> parseUpdateManifest(const QByteArray &json);`

- [ ] **Step 1: Write the failing tests**

```cpp
void parseUjNumber_data();   // "0.8.0-uj.13"->13, "main-3d4e37b"->none, "0.8.0-uj.test-1"->none,
                             // "0.8.0-uj."->none, "0.8.0-uj.99999999999"->none (overflow), "dev"->none
void parsesValidManifest();  // uj==13, assets["windows-x64"].size==44841368, history.first().uj==13
void rejectsHtmlBody();      // parseUpdateManifest("<html>login</html>") == nullopt
void rejectsUnknownSchema(); // "schema":2 -> nullopt
void rejectsMissingAssetFields();
void rejectsNonIntegerSize();
void acceptsEmptyChangeLists(); // added/fixed/changed all [] parses, for Review Focus 3
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-update --target test_updatemanifest -j$(nproc)`. Expected: build fails, headers missing.

- [ ] **Step 3: Implement** both units. `parseUjNumber` matches `-uj\.(\d+)$` with `toInt` overflow checked. Asset keys are
`windows-x64`, `linux-flatpak-x86_64` and `raspberry-pi-arm64`. Parsing uses `QJsonDocument`, and any shape problem gives `nullopt`.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build-update --target test_updatemanifest -j$(nproc) && ctest --test-dir build-update -R UpdateManifest --output-on-failure`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/update CMakeLists.txt tests/CMakeLists.txt tests/test_updatemanifest.cpp
git commit -m "feat(update): parse fork versions and update.json"
```

### Task 2: Offer rules, asset choice and URL checks

**Files:**
- Create: `src/update/updatepolicy.h`, `src/update/updatepolicy.cpp`
- Create: `tests/test_updatepolicy.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 types.
- Produces:
  - `enum class UpdatePlatform { Windows, Flatpak, PiArm64, OtherLinux, Unsupported };`
  - `UpdatePlatform detectPlatform(bool isWindows, bool isMacos, bool isFlatpak, const QString &cpuArch);`
  - `QString assetKeyFor(UpdatePlatform platform);` (empty when none)
  - `bool shouldOffer(int runningUj, std::optional<int> currentTxtUj, int offeredUj, int skippedUj, const QSet<int> &failedUj);`
  - `bool isAllowedDownloadUrl(const QUrl &url);`
  - `QList<UpdateRelease> releasesNewerThan(const UpdateManifest &manifest, int runningUj, bool *olderChangesMissing);`
  - `bool validateZipEntries(const QStringList &entries, const QString &version);`

- [ ] **Step 1: Write the failing tests**

```cpp
void shouldOffer_data(); // running 13, offer 14 -> true; offer 13 -> false; current.txt 14, offer 14 -> false;
                         // current.txt 14, offer 15 -> true; skipped 14, offer 14 -> false;
                         // skipped 14, offer 15 -> true; failed {14}, offer 14 -> false
void detectsPlatforms(); // flatpak beats arch; linux arm64 -> PiArm64; linux x86_64 -> OtherLinux; macos -> Unsupported
void urlGuard_data();    // https .../w1ujay/QK4-UJ/releases/download/uj.13/x.zip -> true; http -> false;
                         // other repo -> false; ".../QK4-UJ/releases/download/../../evil" -> false
void historyWindow();    // running 2, history 3..22 -> 20 entries, olderChangesMissing true
void historyComplete();  // running 12, history 13..14 -> 2 entries, olderChangesMissing false
void zipEntries_data();  // full real listing below -> true; "QK4/versions/0.8.0-uj.14/QK4.exe" -> false;
                         // "QK4/versions/0.8.0-uj.13/../../x" -> false; absolute "C:/x" -> false; no QK4.exe -> false;
                         // "QK4/stray.txt" -> false
                         // real listing: "QK4/", "QK4/QK4.exe", "QK4/current.txt", "QK4/versions/",
                         // "QK4/versions/0.8.0-uj.13/", "QK4/versions/0.8.0-uj.13/QK4.exe"
```

- [ ] **Step 2: Run to verify failure.** Expected: build fails.

- [ ] **Step 3: Implement.** `validateZipEntries` allows exactly `QK4/QK4.exe`, `QK4/current.txt` and the ancestor directory
entries `QK4/`, `QK4/versions/` and `QK4/versions/<version>/`. It requires every other entry to be under
`QK4/versions/<version>/`, and needs that folder's `QK4.exe`. The redirect host is not checked here. Only the starting URL
is guarded, per the spec.

- [ ] **Step 4: Run to verify pass.** `cmake --build build-update --target test_updatepolicy -j$(nproc) && ctest --test-dir build-update -R UpdatePolicy --output-on-failure`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/update CMakeLists.txt tests/CMakeLists.txt tests/test_updatepolicy.cpp
git commit -m "feat(update): add offer rules, asset choice and URL checks"
```

### Task 3: Launcher decision logic

**Files:**
- Create: `tools/windows-launcher/updatelayout.h`, `tools/windows-launcher/updatelayout.cpp`
- Create: `tests/test_updatelayout.cpp`
- Modify: `tests/CMakeLists.txt`

Standard C++17 only, using `std::wstring`, so it compiles on Linux for tests and in the launcher without Qt.

**Interfaces:**
- Produces:
  - `std::optional<int> ujFromFolderName(const std::wstring &name);`
  - `struct VersionFolder { std::wstring name; bool hasExe; bool hasHealthy; };`
  - `std::optional<std::wstring> selectVersion(const std::wstring &currentTxt, const std::vector<VersionFolder> &folders);`
  - `std::wstring quoteArgument(const std::wstring &arg);`, `std::wstring joinCommandLine(const std::vector<std::wstring> &args);`
  - `bool shouldCommit(int newUj, std::optional<int> currentUj);`
  - `enum class FailureAction { StartCurrentNoNote, KeepFolderWriteNote, TrashFolderWriteNote };`
  - `FailureAction decideFailure(int attemptUj, std::optional<int> currentUj, bool otherProcessesInFolder);`
  - `std::vector<std::wstring> foldersToClean(const std::wstring &ownVersion, const std::wstring &currentTxt, const std::vector<std::wstring> &folders, const std::set<int> &failedUj);`

- [ ] **Step 1: Write the failing tests**

```cpp
void selection_data();  // current names a folder with exe -> it; missing folder -> highest N with .healthy;
                        // none healthy -> highest N with exe; nothing -> nullopt; "..\\evil" in current.txt -> ignored
void quoting_data();    // CommandLineToArgvW rules: L"K4 West" -> L"\"K4 West\""; L"a\\" -> L"a\\";
                        // L"a b\\" -> L"\"a b\\\\\""; L"say \"hi\"" escaped; L"" -> L"\"\""; L"K4 Süd" unchanged in meaning
void commit_data();     // new 15, current 14 -> true; new 14, current 15 -> false; equal -> false; no current -> true
void failure_data();    // current >= attempt -> StartCurrentNoNote; others running -> KeepFolderWriteNote;
                        // else -> TrashFolderWriteNote
void cleanup_data();    // own != current -> empty; versions 10..15, current 15 -> {10,11,12,13};
                        // higher N than current kept; higher N in failedUj -> included; non-version names ignored
```

- [ ] **Step 2: Run to verify failure.** Expected: build fails.

- [ ] **Step 3: Implement** the pure functions. `quoteArgument` follows the documented `CommandLineToArgvW` backslash rules.

- [ ] **Step 4: Run to verify pass.** `cmake --build build-update --target test_updatelayout -j$(nproc) && ctest --test-dir build-update -R UpdateLayout --output-on-failure`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/windows-launcher tests/CMakeLists.txt tests/test_updatelayout.cpp
git commit -m "feat(update): add launcher version and failure decisions"
```

### Task 4: Windows launcher and its integration tests

**Files:**
- Create: `tools/windows-launcher/CMakeLists.txt`, `tools/windows-launcher/main.cpp`, `tools/windows-launcher/launcher.rc`
- Create: `tools/windows-launcher/updatelock.h`, `tools/windows-launcher/jobtracker.h`
- Create: `tools/windows-launcher/tests/fakeapp.cpp`, `tools/windows-launcher/tests/run_tests.ps1`
- Create: `.github/workflows/ci-update-launcher.yml`

**Interfaces:**
- Consumes: Task 3.
- Produces, a command line later tasks call:
  - Normal: `QK4.exe [any args]`
  - `QK4.exe --qk4-finish-update <version> <pid> -- <original args>`
  - `QK4.exe --qk4-cleanup <ownVersion> <failedUjCsv>`
  - Files: `<root>\.update.lock`, `<root>\update-failed.txt` (one version per line), `versions\<v>\.healthy`,
    `versions\<v>\.ready-<attempt>`; environment variable `QK4_UPDATE_ATTEMPT`
  - Version resource `FileDescription` exactly `QK4 Update Launcher`

- [ ] **Step 1: Write `fakeapp.cpp` and `run_tests.ps1` with these failing cases**

`fakeapp.exe` takes its behaviour from `FAKE_MODE`: `ready`, `crash-after-ready`, `never-ready`, `no-file-but-window`,
`restart-then-ready`, `start-fail`. Each case builds a temporary root with fake versions:

```text
healthyCommits             ready, current.txt moves to 14, .healthy written, process left running
neverReadyRollsBack        never-ready, process stopped, folder gone, failure note names 14, previous started with args
crashAfterReadyRollsBack   crash-after-ready exits 3 inside 10 s, same outcome
windowWithoutFilePasses    no-file-but-window, committed, process still running at 60 s
restartThenReadyPasses     restart-then-ready, committed
restartThenFailStops       restart then never-ready, replacement stopped via the job
staleHealthyDoesNotPass    folder pre-holds .healthy, never-ready, not committed
concurrentVersions         finishers for 14 and 15 together, current.txt ends on 15
oneFailsOneWorks           two attempts on 14, second start-fail, first's process alive, folder kept
launcherKilled             kill finisher during wait, lock free, retry commits
cleanupInUse               running old version started via launcher, cleanup leaves that folder
cleanupPausedRace          QK4_LAUNCHER_TEST_PAUSE build pauses after the process check; launch that version;
                           resume; running copy unharmed
closingCopyHangs           pid stays alive, terminated at 30 s, new version starts
argsRoundTrip              spaces, non-ASCII, trailing backslash reach fakeapp unchanged, normal and after rollback
missingCurrentTxt          fallback selection, visible error path when nothing qualifies (exit code asserted)
pathWithSpaces             whole root under "C:\tmp\QK4 Süd long folder name"
```

- [ ] **Step 2: Add `ci-update-launcher.yml`** on `windows-latest`, triggered on pushes touching `tools/windows-launcher/**`,
running the script. Commit it with the failing cases, push, and confirm the run for that exact commit fails.

```bash
git add tools/windows-launcher/tests .github/workflows/ci-update-launcher.yml
git commit -m "test(update): add failing launcher integration cases"
git push ujay feat/update-check
gh run list -R w1ujay/QK4-UJ -w ci-update-launcher.yml --commit "$(git rev-parse HEAD)" --json databaseId,headSha
gh run watch -R w1ujay/QK4-UJ <that run id> --exit-status
```

Expected: `headSha` equals `git rev-parse HEAD`, and the run fails because the launcher is missing.

- [ ] **Step 3: Implement `updatelock.h`.** `class UpdateLock`: `CreateFileW` on `.update.lock` with share mode 0 and
`OPEN_ALWAYS`, retried for up to 10 s. Held only for the operations the spec lists.

- [ ] **Step 4: Implement `jobtracker.h`.** `class JobTracker`: `CreateJobObjectW` with no kill-on-close and no breakaway flags,
starting the child suspended, assigning it, then resuming. It exposes `activeCount()`, `terminate()`, `anyVisibleWindow()`
(`EnumWindows` matched against job process IDs) and `anyNonZeroExit()`.

- [ ] **Step 5: Implement `main.cpp`,** following the spec's Install steps 6-10, Health check, Cleanup, Transient locks and
Interruptions. Link statically with `/MT`. `launcher.rc` sets `FileDescription`. Show errors with `MessageBoxW`. In
`--qk4-cleanup`, compile the test pause only under `QK4_LAUNCHER_TEST_PAUSE`.

- [ ] **Step 6: Confirm job inheritance.** `restartThenFailStops` must use `QProcess::startDetached` semantics, so add a small
Qt fixture if `CreateProcess` in `fakeapp` does not prove it. If Qt breaks away, track by process path for this attempt and record
that in the spec.

- [ ] **Step 7: Commit and push**

```bash
git add tools/windows-launcher
git commit -m "feat(update): add Windows launcher with health-checked commit"
git push ujay feat/update-check
```

- [ ] **Step 8: Run to verify pass.** Use the `gh run list --commit` and `gh run watch` commands from Step 2.
Expected: `headSha` matches, and all cases PASS on `windows-latest`. A fix means a new commit and a new run for that commit.

### Task 5: Update check and download

**Files:**
- Create: `src/update/updatechecker.h/.cpp`, `src/update/updatedownloader.h/.cpp`
- Create: `tests/test_updatenetwork.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

Tests run against a local `QTcpServer`. Both classes take their URL by constructor, so tests need no flag in release builds.

**Interfaces:**
- Consumes: Tasks 1-2.
- Produces:
  - `class UpdateChecker : QObject`: `explicit UpdateChecker(QUrl url, QObject *parent)`, `void start()`, signal `finished(std::optional<UpdateManifest>)`
  - `class UpdateDownloader : QObject`: `UpdateDownloader(UpdateAsset asset, QString destinationPath, QObject *parent)`, `void start()`, `void cancel()`, signals `progress(qint64 received, qint64 total)`, `succeeded(QString path)`, `failed(QString message)`

- [ ] **Step 1: Write the failing tests**

```cpp
void checkerReturnsManifest();
void checkerGivesNullOnHtml200();     // Review Focus 1
void checkerGivesNullOn404();
void checkerGivesNullAfterFiveSeconds(); // server never answers; finished within 5.5 s
void checkerEmitsFinishedOnce();
void downloaderVerifiesHash();
void downloaderRejectsWrongHash();    // file deleted, failed() emitted
void downloaderRejectsWrongSize();
void downloaderCancelDeletesFile();
void downloaderReportsDiskError();    // unwritable destination gives failed(), no crash
```

- [ ] **Step 2: Run to verify failure.** Expected: build fails.

- [ ] **Step 3: Implement.** One `QNetworkAccessManager` per object. `setRedirectPolicy(NoLessSafeRedirectPolicy)`. Checker
timeout through `QNetworkRequest::setTransferTimeout(5000)` plus a guarding `QTimer`. The downloader writes to `<destination>.part`,
hashes while writing and renames only after verification. `isAllowedDownloadUrl` is enforced in production by `UpdateFlow`
(Task 8), so the local test server is not blocked here.

- [ ] **Step 4: Run to verify pass.** `cmake --build build-update --target test_updatenetwork -j$(nproc) && ctest --test-dir build-update -R UpdateNetwork --output-on-failure`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/update CMakeLists.txt tests/CMakeLists.txt tests/test_updatenetwork.cpp
git commit -m "feat(update): add update check and verified download"
```

### Task 6: Windows install preparation

**Files:**
- Create: `src/update/managedinstall.h/.cpp`, `src/update/windowsinstaller.h/.cpp`
- Create: `tests/test_managedinstall.cpp`
- Modify: `src/settings/interfacescale.h/.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 2 and 4 (`--qk4-finish-update`, `--qk4-cleanup`, resource string).
- Produces:
  - `struct ManagedInstall { QString root; QString version; };`
  - `std::optional<ManagedInstall> detectManagedInstall(const QString &exePath);`, with a seam: `bool launcherHasIdentity(const QString &path);`
  - `std::optional<int> readCurrentTxtUj(const QString &root);`
  - `class WindowsInstaller : QObject`: `WindowsInstaller(ManagedInstall install, QString zipPath, QString version, QObject *parent)`, `void start()`, signals `readyToQuit()`, `failed(QString message)`, `alreadyInstalled()`
  - `QProcessEnvironment InterfaceScale::childEnvironment();`

- [ ] **Step 1: Write the failing tests**

```cpp
void detectsManagedLayout();     // temp tree, identity seam true -> root + version
void rejectsOldStyleFolder();    // exe not under versions\<name>
void rejectsMissingCurrentTxt();
void rejectsLauncherWithoutIdentity();
void rejectsReadOnlyRoot();      // create-file probe fails (chmod on Linux stands in)
void rejectsReadOnlyVersions();
void readsCurrentTxtUj();        // "0.8.0-uj.14\r\n" -> 14; garbage -> nullopt
void existingVersionFolderIsReused(); // incoming discarded, existing folder untouched
void extractionFailureRemovesIncoming(); // Review Focus 4, fake extractor returns failure
void childEnvironmentStripsScale(); // QK4_SCALE_RELAUNCHED always removed; QT_SCALE_FACTOR removed only when QK4 set it
```

- [ ] **Step 2: Run to verify failure.** Expected: build fails.

- [ ] **Step 3: Implement `InterfaceScale::childEnvironment()`** beside `relaunch()`. Leave `relaunch()` unchanged.

- [ ] **Step 4: Implement `managedinstall`.** Probe writability by creating and deleting a uniquely named file in both
folders. `launcherHasIdentity` uses `GetFileVersionInfoW` on Windows and returns false elsewhere. Add `version` to the Windows
link libraries.

- [ ] **Step 5: Implement `WindowsInstaller`** for spec Install steps 1 and 3-5. Extraction goes through a small injected function
so tests can fake it. Production uses `tar.exe -tf`, then `validateZipEntries`, then the exact extraction command from
Decisions This Plan Adds into `versions\.incoming-<QUuid>`, then confirms `<incoming>\QK4.exe` exists. Start the launcher with `QProcess` using `childEnvironment()` and `startDetached`, then emit `readyToQuit`.

- [ ] **Step 6: Run to verify pass.** `cmake --build build-update --target test_managedinstall -j$(nproc) && ctest --test-dir build-update -R ManagedInstall --output-on-failure`. Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/update src/settings/interfacescale.h src/settings/interfacescale.cpp CMakeLists.txt tests/CMakeLists.txt tests/test_managedinstall.cpp
git commit -m "feat(update): prepare Windows version folders and detect installs"
```

### Task 7: Update popup

**Files:**
- Create: `src/ui/dialogs/updatedialog.h/.cpp`
- Create: `tests/test_updatedialog.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 types, Task 2 `releasesNewerThan`.
- Produces: `class UpdateDialog : public QDialog`:
  - `UpdateDialog(QString currentVersion, QString newVersion, QList<UpdateRelease> releases, bool olderChangesMissing, bool canInstall, QWidget *parent)`
  - Signals: `installRequested()`, `laterRequested()`, `skipRequested()`, `cancelRequested()`, `retryRequested()`
  - Slots: `showProgress(qint64, qint64)`, `showError(QString)`, `showManualSteps(QString text, QString copyText)`, `showIdleButtons()`

- [ ] **Step 1: Write the failing tests** (`QT_QPA_PLATFORM=offscreen`)

```cpp
void titleAndVersions();          // "QK4 update available", both version strings shown
void buttonLabelFollowsPlatform();// canInstall true -> "Install now", false -> "Download"
void changelogIsPlainText();      // "<b>x</b>" appears literally; QPlainTextEdit, not a rich text widget
void groupsMultipleReleases();    // two releases -> both version headings, newest first
void emptyReleaseSaysNoChanges(); // "No listed changes" (Review Focus 3)
void olderMissingNote();          // "Older changes are not listed" (Review Focus 2)
void progressSwapsButtons();      // showProgress hides Install, Later and Skip, shows Cancel
void cancelRestoresButtons();
void errorOffersRetryAndLater();
void escapeMeansLater();          // Esc emits laterRequested, never skipRequested
```

- [ ] **Step 2: Run to verify failure.** Expected: build fails.

- [ ] **Step 3: Implement.** Layout A: title, versions, read-only `QPlainTextEdit` changelog, then a row with Skip this version
at the left and Later and Install now at the right. Modal, centred on the parent. Use `K4Styles::dialogButton()` and the
existing label helpers. `showManualSteps` shows the text and a Copy button when `copyText` is not empty.

- [ ] **Step 4: Run to verify pass.** `cmake --build build-update --target test_updatedialog -j$(nproc) && ctest --test-dir build-update -R UpdateDialog --output-on-failure`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ui/dialogs/updatedialog.h src/ui/dialogs/updatedialog.cpp CMakeLists.txt tests/CMakeLists.txt tests/test_updatedialog.cpp
git commit -m "feat(ui): add the update popup"
```

### Task 8: Startup flow and MainWindow hook

**Files:**
- Create: `src/update/updategate.h/.cpp` (pure state machine)
- Create: `src/update/updateflow.h/.cpp`, `src/update/windowsappid.h/.cpp`
- Create: `tests/test_updategate.cpp`, `tests/test_updateflow.cpp`
- Modify: `src/mainwindow.h`, `src/mainwindow.cpp:644`, `src/mainwindow.cpp:1444`, `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: all earlier tasks.
- Produces:
  - `class UpdateGate`: `enum class Action { None, AllowStartupRadio, ShowPopup, Finish };` with `Action onCheckDone(bool updateFound)`, `Action onManualConnection()`, `Action onPopupCleared()`, `bool startupRadioCancelled() const`
  - `class UpdateFlow : QObject`: `UpdateFlow(QString runningVersion, QUrl manifestUrl, QWidget *window, QObject *parent)`, `void start()`, `void noteManualConnection()`, signal `startupRadioAllowed()`
  - `void applyWindowsRelaunchIdentity(QWidget *window, const QString &launcherPath);`
  - QSettings keys `update/skippedUj` (int), `update/failedUj` (QStringList)

- [ ] **Step 1: Write the failing tests**

```cpp
void noUpdateAllowsRadio();          // onCheckDone(false) -> AllowStartupRadio
void updateShowsPopupThenRadio();    // ShowPopup, then onPopupCleared -> AllowStartupRadio
void manualConnectCancelsEverything(); // onManualConnection then onCheckDone(true) -> Finish, cancelled true
void manualConnectWithNoUpdate();    // onManualConnection then onCheckDone(false) -> Finish
void failedManualAttemptStillCancels();
void checkTimeoutActsLikeNoUpdate();
void radioAllowedOnlyOnce();
```

`tests/test_updateflow.cpp` uses a local server that answers `update.json` after a delay, and no connection signal is ever sent,
which proves the flow does not depend on one:

```cpp
void manualConnectBeforeSlowCheckBlocksRadio();  // no update on server; noteManualConnection, then reply arrives;
                                                 // startupRadioAllowed never emitted
void manualConnectBeforeUpdateFoundShowsNoPopup(); // update on server; no UpdateDialog created, radio not allowed
void noManualConnectAllowsRadioOnce();           // reply arrives; exactly one startupRadioAllowed
void timeoutWithoutManualConnectAllowsRadio();   // server silent; allowed within 5.5 s
void noteAfterRadioAllowedIsHarmless();          // the startup connection itself calls noteManualConnection
void nonUjBuildAllowsRadioImmediately();         // runningVersion "main-3d4e37b"; no request made
```

- [ ] **Step 2: Run to verify failure.** `cmake --build build-update --target test_updategate test_updateflow -j$(nproc)`. Expected: build fails.

- [ ] **Step 3: Implement `UpdateGate`.**

- [ ] **Step 4: Implement `UpdateFlow::start()`,** in this order:
1. If `QK4_UPDATE_ATTEMPT` is set, write `.ready-<attempt>` beside the exe, then `qunsetenv`.
2. On Windows, read `update-failed.txt`, record its versions in `update/failedUj`, show the note once and delete the file.
3. If `parseUjNumber(QK4_VERSION)` has no value, or the platform is `Unsupported`: emit `startupRadioAllowed` and stop.
4. If managed, set the relaunch identity. If own version matches `current.txt`, start `--qk4-cleanup` detached.
5. Start `UpdateChecker`, and feed its result and `noteManualConnection()` into `UpdateGate`. Do not watch connection signals.
6. Filter the manifest through `shouldOffer` and `isAllowedDownloadUrl`.

Install path: re-read `current.txt`, download, run `WindowsInstaller`, then `window->close()` on `readyToQuit`. Not managed:
save to Downloads and call `showManualSteps`. Linux: Flatpak uses `QFileDialog::getSaveFileName`, Pi uses
`QStandardPaths::DownloadLocation`, and `OtherLinux` shows the release page link.

- [ ] **Step 5: Hook `MainWindow`.** Replace the `QTimer::singleShot(0, this, &MainWindow::connectToStartupRadio)` line:

```cpp
    m_updateFlow = new UpdateFlow(QStringLiteral(QK4_VERSION), UpdateFlow::defaultManifestUrl(), this, this);
    connect(m_updateFlow, &UpdateFlow::startupRadioAllowed, this, &MainWindow::connectToStartupRadio);
    QTimer::singleShot(0, m_updateFlow, &UpdateFlow::start);
```

Keep the existing WHY comment. Add `static QUrl defaultManifestUrl();` to `UpdateFlow`, built from `UPDATE_REPOSITORY`.

In `MainWindow::connectToRadio`, as the first line, before anything is queued:

```cpp
    m_updateFlow->noteManualConnection(); // WHY: synchronous. The state signal comes from the I/O thread, too late to stop A replacing B.
```

The startup connection also passes through here, after the gate has finished, where the call does nothing.

- [ ] **Step 6: Implement `applyWindowsRelaunchIdentity`.** Sets the window's `System.AppUserModel.ID`,
`RelaunchCommand` and `RelaunchDisplayNameResource` through `SHGetPropertyStoreForWindow`, in a `_WIN32` block. Elsewhere it does
nothing.

- [ ] **Step 7: Run the whole suite and formatter**

Run: `cmake --build build-update -j$(nproc) && ctest --test-dir build-update --output-on-failure && find src -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror`
Expected: all tests PASS, and no format errors.

- [ ] **Step 8: Run the app.** `LD_LIBRARY_PATH="$HOME/6.8.1/gcc_64/lib" ./build-update/QK4`. Expected: a local build shows no
popup, and the Connect dialog works as before.

- [ ] **Step 9: Commit**

```bash
git add src/update src/mainwindow.h src/mainwindow.cpp CMakeLists.txt tests/CMakeLists.txt tests/test_updategate.cpp tests/test_updateflow.cpp
git commit -m "feat(update): check at startup and hold auto-connect for the popup"
```

### Task 9: Fork release workflow

**Files:**
- Create: `.github/workflows/release-uj.yml`
- Create: `scripts/uj-changelog.sh`, `scripts/uj-update-json.sh`, `scripts/uj-assemble-windows.sh`
- Create: `tests/scripts/test_uj_scripts.sh`
- Modify: `tests/CMakeLists.txt` (`add_test` for the shell test)

**Interfaces:**
- Consumes: asset keys from Task 1; zip layout from Task 2; launcher output `QK4.exe` from Task 4.
- Produces:
  - `scripts/uj-changelog.sh <prev-tag> <tag> <upstream-ref>` prints JSON `{"added":[],"fixed":[],"changed":[]}`
  - `scripts/uj-update-json.sh <version> <uj> <date> <assets-dir> <base-url> <history-dir>` prints `update.json`
  - `scripts/uj-assemble-windows.sh <artifact-dir> <launcher-exe> <version> <out-zip>`

- [ ] **Step 1: Write the failing shell tests** using a temporary git repo

```text
forkCommitsListed      feat: and fix: map to added and fixed, refactor: and perf: to changed
skippedTypesLeftOut    docs:, chore:, ci:, test:, style: left out
upstreamCollapsed      3 commits reachable from the upstream ref give "Includes 3 upstream changes"
numericTagsOnly        uj.test-1 is ignored as a range boundary
updateJsonValid        jq validates schema, sizes and sha256 match the real files
historyCappedAt20
zipLayout              QK4/QK4.exe, QK4/current.txt, QK4/versions/<v>/QK4.exe, nothing else at the top level
zipRoundTrip           real zip from uj-assemble-windows.sh, extracted with the plan's exact command using bsdtar,
                       leaves <incoming>/QK4.exe directly, with nested subfolders intact
```

- [ ] **Step 2: Run to verify failure.** `bash tests/scripts/test_uj_scripts.sh`. Expected: FAIL, scripts missing.

- [ ] **Step 3: Implement the scripts.** `uj-update-json.sh` builds `history` from the previous release's `update.json`, fetched
by `gh release download`, with the new entry prepended, capped at 20.

- [ ] **Step 4: Run to verify pass.** `bash tests/scripts/test_uj_scripts.sh`. Expected: PASS.

- [ ] **Step 5: Write `release-uj.yml`.** Trigger `push: tags: ['uj.*']`. Jobs:
- `version`: `<base>` from `git describe --tags --match 'v[0-9]*' --abbrev=0` with `-*` and the `v` stripped; result `<base>-uj.<N>`,
  or `<base>-uj.test-<x>` for rehearsals.
- `windows`, `raspberry-pi`, `flatpak`: the reusable builders, with that version.
- `launcher`: `windows-latest`, builds `tools/windows-launcher`, uploads `QK4.exe`.
- `release`: fetches `https://github.com/mikeg-dal/QK4.git development`, runs the scripts, writes `SHA256SUMS.txt`, and creates a
  draft with `softprops/action-gh-release` pinned to the SHA in `release.yml`, `draft: true`, `prerelease: false`, `make_latest: true`.

Release notes body: the changelog, plus the manual recovery note for `current.txt` from the spec's Known risks.

A `verify-windows-zip` job on `windows-latest` downloads the assembled zip and runs the plan's exact `tar.exe` command with
real Windows `tar.exe`, failing unless `<incoming>\QK4.exe` exists. `release` needs it.

- [ ] **Step 6: Commit and push**

```bash
git add .github/workflows/release-uj.yml scripts/uj-*.sh tests/scripts tests/CMakeLists.txt
git commit -m "ci(release): add fork release workflow with update.json"
git push ujay feat/update-check
```

- [ ] **Step 7: Rehearse on that commit**

```bash
git tag uj.test-1 HEAD
git push ujay uj.test-1
gh run list -R w1ujay/QK4-UJ -w release-uj.yml --commit "$(git rev-parse HEAD)" --json databaseId,headSha
gh run watch -R w1ujay/QK4-UJ <that run id> --exit-status
```

Expected: `headSha` equals `git rev-parse HEAD`, the run is green including Flatpak and `verify-windows-zip`, and there is a draft
holding three packages, `SHA256SUMS.txt` and `update.json`. If Flatpak fails with `unable to read tree`, fix that in
`release-uj.yml`. A fix means a new commit, then a new `uj.test-<n>` tag on it.

- [ ] **Step 8: Remove the rehearsal.** `gh release delete uj.test-1 -R w1ujay/QK4-UJ --yes --cleanup-tag && git tag -d uj.test-1`,
repeated for any other `uj.test-<n>`. Expected: drafts and tags gone.

### Task 10: Owner acceptance and documentation

**Files:**
- Create: `docs/update-acceptance.md`
- Modify: `docs/BACKLOG.md`

- [ ] **Step 1: Write `docs/update-acceptance.md`,** a checklist for native Windows and Linux, holding the spec's Testing cases
that need a real QK4 or a person. Those are the popup on a real install, auto-connect held and released, connecting to radio B by
hand on a slow network while radio A is set to auto-connect, a copy on the air during an update, a forced auto-scale restart with a scaled session, Firewall prompts, a pin surviving two updates, Program Files,
a second drive, Controlled Folder Access, antivirus, and the Flatpak and Pi paths.

- [ ] **Step 2: Add a `docs/BACKLOG.md` entry** for the upstream merge (179 commits, 22 conflicted files on 2026-10-31), to do
after this feature and before release testing, and one for detecting other copies.

- [ ] **Step 3: Commit**

```bash
git add docs/update-acceptance.md docs/BACKLOG.md
git commit -m "docs(update): add the acceptance checklist and merge follow-up"
```

- [ ] **Step 4: Push.** `git push ujay feat/update-check`. Expected: branch updated on the fork. No PR. Stop here for the owner.

The upstream merge, `uj.13`, `uj.14` and the checklist run happen after this, under the owner's direction.
