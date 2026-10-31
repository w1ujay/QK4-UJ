// The Windows launcher: the QK4.exe at the install root. It has three jobs.
//
//   QK4.exe [any args]                                       run the current version
//   QK4.exe --qk4-finish-update <version> <pid> -- <args>    finish an install: start, health check, commit or roll
//   back QK4.exe --qk4-cleanup <ownVersion> <failedUjCsv>         remove old version folders
//
// The spec's "Windows install" section is the authority. The decisions themselves are in updatelayout.cpp; this file
// is the Win32 around them. Nothing here may stop someone reaching their radio because of an update problem.
#include "jobtracker.h"
#include "updatelayout.h"
#include "updatelock.h"

#include <windows.h>

#include <bcrypt.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr DWORD HEALTH_WAIT_MS = 60000;
constexpr DWORD READY_WATCH_MS = 10000;
constexpr DWORD EMPTY_JOB_GRACE_MS = 5000;
constexpr DWORD CLOSING_COPY_LIMIT_MS = 30000;
constexpr DWORD RETRY_WINDOW_MS = 30000;
constexpr DWORD NOTE_RETRY_WINDOW_MS = 1000;
constexpr DWORD PUT_BACK_RETRY_WINDOW_MS = RETRY_WINDOW_MS;
constexpr DWORD JOB_STOP_WAIT_MS = 10000;
constexpr DWORD TERMINATE_WAIT_MS = 5000;
constexpr DWORD POLL_MS = 25;
constexpr ULONGLONG INCOMING_MAX_AGE_100NS = 24ULL * 3600 * 10000000;

constexpr wchar_t ATTEMPT_ENV[] = L"QK4_UPDATE_ATTEMPT";

enum ExitCode { EXIT_OK = 0, EXIT_NO_VERSION = 1, EXIT_ROLLED_BACK = 2, EXIT_COMMIT_FAILED = 3 };

// Retries transient failures, such as antivirus or the indexer briefly holding a new file.
class Backoff {
public:
    explicit Backoff(DWORD windowMs) : m_start(GetTickCount64()), m_windowMs(windowMs) {}
    bool expired() const { return GetTickCount64() - m_start >= m_windowMs; }
    void wait() {
        Sleep(m_delayMs);
        m_delayMs = std::min<DWORD>(m_delayMs * 2, 2000);
    }

private:
    ULONGLONG m_start;
    DWORD m_windowMs;
    DWORD m_delayMs = 100;
};

void showError(const std::wstring &text) {
    MessageBoxW(nullptr, text.c_str(), L"QK4", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

std::wstring randomHex() {
    unsigned char bytes[8] = {};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        const ULONGLONG fallback = GetTickCount64() ^ (static_cast<ULONGLONG>(GetCurrentProcessId()) << 32);
        memcpy(bytes, &fallback, sizeof(bytes));
    }
    static constexpr wchar_t DIGITS[] = L"0123456789abcdef";
    std::wstring out;
    for (unsigned char b : bytes) {
        out.push_back(DIGITS[b >> 4]);
        out.push_back(DIGITS[b & 15]);
    }
    return out;
}

// --- files ------------------------------------------------------------------------------------------------------

bool isFile(const std::wstring &path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool exists(const std::wstring &path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string readFile(const std::wstring &path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};
    std::string bytes(65536, '\0');
    DWORD read = 0;
    if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
        read = 0;
    CloseHandle(file);
    bytes.resize(read);
    return bytes;
}

// UTF-8, or UTF-16 with a byte order mark, because recovery means editing current.txt by hand.
std::wstring decodeText(const std::string &bytes) {
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring text((bytes.size() - 2) / 2, L'\0');
        memcpy(text.data(), bytes.data() + 2, text.size() * sizeof(wchar_t));
        return text;
    }
    const int size = static_cast<int>(bytes.size());
    const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), size, nullptr, 0);
    std::wstring text(length, L'\0');
    if (length > 0)
        MultiByteToWideChar(CP_UTF8, 0, bytes.data(), size, text.data(), length);
    return text;
}

std::string encodeUtf8(const std::wstring &text) {
    const int size = static_cast<int>(text.size());
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
    std::string bytes(length, '\0');
    if (length > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.data(), size, bytes.data(), length, nullptr, nullptr);
    return bytes;
}

bool writeNewFile(const std::wstring &path, const std::string &bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    bool ok =
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    ok = FlushFileBuffers(file) && ok;
    CloseHandle(file);
    return ok;
}

std::wstring readCurrentTxt(const std::wstring &root) {
    return decodeText(readFile(root + L"\\current.txt"));
}

// Atomic: a crash or power loss leaves the old pointer or the new one.
bool writeCurrentTxt(const std::wstring &root, const std::wstring &version) {
    const std::wstring temp = root + L"\\current.txt." + randomHex() + L".tmp";
    if (!writeNewFile(temp, encodeUtf8(version) + "\n")) {
        DeleteFileW(temp.c_str());
        return false;
    }
    const std::wstring target = root + L"\\current.txt";
    if (MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(temp.c_str());
    return false;
}

// One version per line, and at most once. A lost note only means the build is offered again, so this never blocks.
void writeFailureNote(const std::wstring &root, const std::wstring &version) {
    const std::wstring path = root + L"\\update-failed.txt";
    const std::wstring text = decodeText(readFile(path));
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos)
            end = text.size();
        std::wstring line = text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        if (line == version)
            return;
        start = end + 1;
    }
    const std::string line = ((text.empty() || text.back() == L'\n') ? "" : "\n") + encodeUtf8(version) + "\n";
    Backoff retry(NOTE_RETRY_WINDOW_MS);
    for (;;) {
        HANDLE file =
            CreateFileW(path.c_str(), FILE_APPEND_DATA, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
            FlushFileBuffers(file);
            CloseHandle(file);
            return;
        }
        if (retry.expired())
            return;
        retry.wait();
    }
}

// Never follows a junction or symbolic link out of the folder.
bool deleteTreeOnce(const std::wstring &path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return true;
    if (attributes & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return DeleteFileW(path.c_str()) != 0;
    if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        WIN32_FIND_DATAW entry;
        HANDLE find = FindFirstFileW((path + L"\\*").c_str(), &entry);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = entry.cFileName;
                if (name != L"." && name != L"..")
                    deleteTreeOnce(path + L"\\" + name);
            } while (FindNextFileW(find, &entry));
            FindClose(find);
        }
    }
    return RemoveDirectoryW(path.c_str()) != 0 || !exists(path);
}

void deleteWithRetry(const std::wstring &path) {
    Backoff retry(RETRY_WINDOW_MS);
    while (!deleteTreeOnce(path) && !retry.expired())
        retry.wait();
}

// --- install layout -------------------------------------------------------------------------------------------------

std::wstring launcherFolder() {
    std::wstring path(32768, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), 32768));
    return path.substr(0, path.find_last_of(L'\\'));
}

std::wstring versionFolder(const std::wstring &root, const std::wstring &version) {
    return root + L"\\versions\\" + version;
}

std::wstring versionExe(const std::wstring &root, const std::wstring &version) {
    return versionFolder(root, version) + L"\\QK4.exe";
}

// Names in versions\, leaving out the .incoming-* and .trash-* working folders.
std::vector<VersionFolder> listVersions(const std::wstring &root) {
    std::vector<VersionFolder> folders;
    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW((root + L"\\versions\\*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE)
        return folders;
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || entry.cFileName[0] == L'.')
            continue;
        const std::wstring name = entry.cFileName;
        folders.push_back({name, isFile(versionExe(root, name)), isFile(versionFolder(root, name) + L"\\.healthy")});
    } while (FindNextFileW(find, &entry));
    FindClose(find);
    return folders;
}

// --- processes --------------------------------------------------------------------------------------------------

// The real path of something on disk: 8.3 names, junctions and subst drives resolved. It normalises our side of a
// comparison with a process's image path, not the image path itself, so a match proves a process runs from there but
// no match proves nothing. folderInUse() is the check to rely on.
std::wstring finalPath(const std::wstring &path) {
    HANDLE handle =
        CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return path;
    std::wstring result(32768, L'\0');
    const DWORD length =
        GetFinalPathNameByHandleW(handle, result.data(), 32768, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(handle);
    if (length == 0 || length >= 32768)
        return path;
    result.resize(length);
    if (result.rfind(L"\\\\?\\UNC\\", 0) == 0)
        return L"\\\\" + result.substr(8);
    if (result.rfind(L"\\\\?\\", 0) == 0)
        return result.substr(4);
    return result;
}

std::wstring processImage(HANDLE process) {
    std::wstring path(32768, L'\0');
    DWORD length = 32768;
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length))
        return {};
    path.resize(length);
    return path;
}

bool startsWithNoCase(const std::wstring &text, const std::wstring &prefix) {
    if (text.size() < prefix.size())
        return false;
    const int length = static_cast<int>(prefix.size());
    return CompareStringOrdinal(text.data(), length, prefix.data(), length, TRUE) == CSTR_EQUAL;
}

// Is a process visibly running from versions\<name>? The folder need not exist. A process that cannot be opened or
// queried, such as an elevated copy, is not seen, because treating those as running would match system processes and no
// folder would ever go.
bool processRunningFrom(const std::wstring &root, const std::wstring &name) {
    const std::wstring prefix = finalPath(root + L"\\versions") + L"\\" + name + L"\\";
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return true;
    bool running = false;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !running; more = Process32NextW(snapshot, &entry)) {
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process)
            continue;
        // The wait skips a process that has exited but is kept listed by an open handle, such as the attempt's own.
        running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT && startsWithNoCase(processImage(process), prefix);
        CloseHandle(process);
    }
    CloseHandle(snapshot);
    return running;
}

// A running image cannot be opened for writing, whoever owns the process and whatever name Windows reports for it. This
// needs no access to the process. Anything but a clean open or a missing exe counts as in use: a false alarm, say from
// antivirus, only leaves the folder for a later launch.
bool exeInUse(const std::wstring &folder) {
    HANDLE file = CreateFileW((folder + L"\\QK4.exe").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        return false;
    }
    const DWORD error = GetLastError();
    return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
}

bool folderInUse(const std::wstring &root, const std::wstring &name) {
    return exeInUse(versionFolder(root, name)) || processRunningFrom(root, name);
}

enum class TrashResult { Trashed, InUse, Failed };

// Renames versions\<name> to a .trash-* folder, setting trashName. Call under the update lock.
//
// WHY the check after the rename: NTFS allows a folder to be renamed while an exe inside it is running, so a failed
// rename can't be relied on to protect a running copy. A launch that slipped in after the caller's own check is visible
// here. A launch after the rename cannot find the folder at all. So the folder goes back, untouched, if anything runs
// from it. Both names are checked because which one Windows reports for a moved image is not documented. If the folder
// cannot be put back it stays a .trash-* folder, and deleteTrash() leaves that alone for as long as the copy runs. The
// caller needs no different handling: the version has left versions\ either way.
TrashResult trashFolder(const std::wstring &root, const std::wstring &name, std::wstring &trashName) {
    const std::wstring folder = versionFolder(root, name);
    const std::wstring candidate = L".trash-" + randomHex();
    const std::wstring moved = versionFolder(root, candidate);
    if (!MoveFileW(folder.c_str(), moved.c_str()))
        return TrashResult::Failed;
    if (!folderInUse(root, candidate) && !processRunningFrom(root, name)) {
        trashName = candidate;
        return TrashResult::Trashed;
    }
    Backoff retry(PUT_BACK_RETRY_WINDOW_MS);
    while (!MoveFileW(moved.c_str(), folder.c_str()) && !retry.expired())
        retry.wait();
    return TrashResult::InUse;
}

// Outside the lock. Never touches a folder something runs from: deleting around a running exe would remove the DLLs and
// plugins that copy has yet to load. The exe probe matters most here, because a copy stranded in this folder may still
// be reported under its original name, which nothing records.
void deleteTrash(const std::wstring &root, const std::wstring &trashName) {
    if (!folderInUse(root, trashName))
        deleteWithRetry(versionFolder(root, trashName));
}

std::wstring commandLineFor(const std::wstring &exe, const std::vector<std::wstring> &args) {
    std::vector<std::wstring> all{exe};
    all.insert(all.end(), args.begin(), args.end());
    return joinCommandLine(all);
}

bool startVersion(const std::wstring &root, const std::wstring &version, const std::vector<std::wstring> &args) {
    const std::wstring exe = versionExe(root, version);
    std::wstring commandLine = commandLineFor(exe, args);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// --- normal launch --------------------------------------------------------------------------------------------------

int reportNoVersion() {
    showError(L"QK4 could not find an installed version to run.\n\nPlease install QK4 again.");
    return EXIT_NO_VERSION;
}

int reportStartFailed(const std::wstring &version) {
    showError(L"QK4 could not start version " + version + L".\n\nPlease install QK4 again.");
    return EXIT_NO_VERSION;
}

int normalLaunch(const std::wstring &root, const std::vector<std::wstring> &args) {
    UpdateLock lock(root); // proceeds without it: an update lock problem must never stop a launch
    const auto version = selectVersion(readCurrentTxt(root), listVersions(root));
    if (!version) {
        lock.release();
        return reportNoVersion();
    }
    const bool started = startVersion(root, *version, args);
    lock.release();
    return started ? EXIT_OK : reportStartFailed(*version);
}

// --- finish update ----------------------------------------------------------------------------------------------

// Opened at once, because QK4 is already on its way out. A failure means it has exited. The image check means a reused
// process ID can never make the launcher wait for, or terminate, something that is not QK4.
HANDLE openClosingCopy(const std::wstring &root, DWORD pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process && !startsWithNoCase(processImage(process), finalPath(root + L"\\versions") + L"\\")) {
        CloseHandle(process);
        return nullptr;
    }
    return process;
}

void waitForClosingCopy(HANDLE process) {
    if (!process)
        return;
    if (WaitForSingleObject(process, CLOSING_COPY_LIMIT_MS) == WAIT_TIMEOUT) {
        TerminateProcess(process, 1);
        WaitForSingleObject(process, TERMINATE_WAIT_MS);
    }
    CloseHandle(process);
}

bool watchAfterReady(JobTracker &job) {
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        job.refresh();
        if (job.activeCount() == 0)
            return !job.anyNonZeroExit(); // a clean exit is someone closing the window
        if (GetTickCount64() - start >= READY_WATCH_MS)
            return true;
        Sleep(POLL_MS);
    }
}

bool waitForHealth(JobTracker &job, const std::wstring &readyFile) {
    const ULONGLONG start = GetTickCount64();
    ULONGLONG emptySince = 0;
    for (;;) {
        job.refresh();
        if (exists(readyFile))
            return watchAfterReady(job);
        const ULONGLONG now = GetTickCount64();
        if (job.activeCount() != 0) {
            emptySince = 0;
        } else if (emptySince == 0) {
            emptySince = now;
        } else if (now - emptySince >= EMPTY_JOB_GRACE_MS) {
            return false;
        }
        if (now - start >= HEALTH_WAIT_MS)
            return job.anyVisibleWindow(); // a failed marker write must not cut off a working session
        Sleep(POLL_MS);
    }
}

// Each retry retakes the lock and re-reads current.txt, because another attempt may commit in between.
bool commitVersion(const std::wstring &root, const std::wstring &version, int uj) {
    Backoff retry(RETRY_WINDOW_MS);
    for (;;) {
        {
            UpdateLock lock(root);
            if (lock.held()) {
                if (!shouldCommit(uj, currentUj(readCurrentTxt(root))))
                    return true;
                if (writeCurrentTxt(root, version))
                    return true;
            }
        }
        if (retry.expired())
            return false;
        retry.wait();
    }
}

int finishHealthy(const std::wstring &root, const std::wstring &version, int uj, const std::wstring &readyFile) {
    writeNewFile(versionFolder(root, version) + L"\\.healthy", {});
    const bool readyRemoved = deleteTreeOnce(readyFile);
    const bool committed = commitVersion(root, version, uj);
    if (!readyRemoved)
        deleteWithRetry(readyFile);
    // A failed commit leaves the new version running. The next launch gets the old one and the update is offered again.
    return committed ? EXIT_OK : EXIT_COMMIT_FAILED;
}

int rollBack(const std::wstring &root, const std::wstring &version, int uj, const std::wstring &readyFile,
             const std::vector<std::wstring> &args) {
    const std::wstring folder = versionFolder(root, version);
    Backoff retry(RETRY_WINDOW_MS);
    std::wstring trash;
    std::optional<std::wstring> selected;
    bool started = false;
    for (;;) {
        UpdateLock lock(root);
        const std::wstring current = readCurrentTxt(root);
        // Without the lock nothing may be renamed, so it is treated like a folder still in use.
        const FailureAction action =
            decideFailure(uj, currentUj(current), !lock.held() || processRunningFrom(root, version));
        if (action == FailureAction::TrashFolderWriteNote && exists(folder)) {
            // InUse retries too: the attempt's own exe can stay busy for a moment, and antivirus may scan it. A real
            // second attempt is seen by the decision above on the next pass.
            if (trashFolder(root, version, trash) != TrashResult::Trashed && !retry.expired()) {
                lock.release();
                retry.wait();
                continue;
            }
        }
        std::vector<VersionFolder> candidates = listVersions(root);
        if (action != FailureAction::StartCurrentNoNote) {
            writeFailureNote(root, version);
            // With a damaged current.txt the fallback must not pick the build that has just failed.
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                            [&](const VersionFolder &f) { return f.name == version; }),
                             candidates.end());
        }
        selected = selectVersion(current, candidates);
        started = selected && startVersion(root, *selected, args);
        break;
    }
    if (!trash.empty())
        deleteTrash(root, trash);
    else
        deleteWithRetry(readyFile);
    if (!selected)
        return reportNoVersion();
    if (!started)
        return reportStartFailed(*selected);
    return EXIT_ROLLED_BACK;
}

int finishUpdate(const std::wstring &root, const std::wstring &version, int uj, DWORD closingPid,
                 const std::vector<std::wstring> &args) {
    HANDLE closing = openClosingCopy(root, closingPid);
    // The launcher must not pin a version folder as its working directory, and the new version inherits this one.
    SetCurrentDirectoryW(root.c_str());
    waitForClosingCopy(closing);

    const std::wstring attempt = randomHex();
    const std::wstring readyFile = versionFolder(root, version) + L"\\.ready-" + attempt;
    const std::wstring exe = versionExe(root, version);
    JobTracker job;
    SetEnvironmentVariableW(ATTEMPT_ENV, attempt.c_str());
    const bool started = job.start(exe, commandLineFor(exe, args));
    SetEnvironmentVariableW(ATTEMPT_ENV, nullptr);

    if (started && waitForHealth(job, readyFile))
        return finishHealthy(root, version, uj, readyFile);

    job.terminate();
    job.waitUntilEmpty(JOB_STOP_WAIT_MS);
    return rollBack(root, version, uj, readyFile, args);
}

// --- cleanup ----------------------------------------------------------------------------------------------------

#ifdef QK4_LAUNCHER_TEST_PAUSE
// Test build only. Holds cleanup between its process check and its rename, so a test can launch into that gap.
void testPause() {
    wchar_t path[32768];
    if (GetEnvironmentVariableW(L"QK4_TEST_PAUSE_FILE", path, 32768) == 0)
        return;
    writeNewFile(std::wstring(path) + L".paused", {});
    while (!exists(std::wstring(path) + L".resume"))
        Sleep(20);
}
#endif

std::set<int> parseFailedCsv(const std::wstring &csv) {
    std::set<int> failed;
    size_t start = 0;
    while (start <= csv.size()) {
        size_t end = csv.find(L',', start);
        if (end == std::wstring::npos)
            end = csv.size();
        const std::wstring item = csv.substr(start, end - start);
        if (!item.empty() && item.size() <= 9 && item.find_first_not_of(L"0123456789") == std::wstring::npos)
            failed.insert(_wtoi(item.c_str()));
        start = end + 1;
    }
    return failed;
}

// Each folder gets its own turn under the lock, with current.txt re-read, so a commit between two folders stops this.
void trashOldVersions(const std::wstring &root, const std::wstring &ownVersion, const std::set<int> &failedUj) {
    std::set<std::wstring> skipped;
    std::wstring retrying;
    Backoff retry(RETRY_WINDOW_MS);
    for (;;) {
        UpdateLock lock(root);
        if (!lock.held())
            return;
        std::vector<std::wstring> names;
        for (const VersionFolder &f : listVersions(root))
            names.push_back(f.name);
        std::wstring target;
        for (const std::wstring &name : foldersToClean(ownVersion, readCurrentTxt(root), names, failedUj)) {
            if (!skipped.count(name)) {
                target = name;
                break;
            }
        }
        if (target.empty())
            return;
        if (folderInUse(root, target)) {
            skipped.insert(target); // a folder in use stays until a later launch
            continue;
        }
#ifdef QK4_LAUNCHER_TEST_PAUSE
        testPause();
#endif
        std::wstring trash;
        const TrashResult result = trashFolder(root, target, trash);
        if (result == TrashResult::Trashed)
            continue;
        if (result == TrashResult::InUse) {
            skipped.insert(target);
            continue;
        }
        lock.release();
        if (target != retrying) {
            retrying = target;
            retry = Backoff(RETRY_WINDOW_MS);
        }
        if (retry.expired())
            skipped.insert(target);
        else
            retry.wait();
    }
}

void removeWorkingFolders(const std::wstring &root) {
    FILETIME nowTime;
    GetSystemTimeAsFileTime(&nowTime);
    const ULONGLONG now = (static_cast<ULONGLONG>(nowTime.dwHighDateTime) << 32) | nowTime.dwLowDateTime;
    std::vector<std::wstring> remove;
    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW((root + L"\\versions\\.*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        const std::wstring name = entry.cFileName;
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        const ULONGLONG written =
            (static_cast<ULONGLONG>(entry.ftLastWriteTime.dwHighDateTime) << 32) | entry.ftLastWriteTime.dwLowDateTime;
        if (name.rfind(L".trash-", 0) == 0 ||
            (name.rfind(L".incoming-", 0) == 0 && now > written && now - written > INCOMING_MAX_AGE_100NS))
            remove.push_back(name);
    } while (FindNextFileW(find, &entry));
    FindClose(find);
    for (const std::wstring &name : remove)
        deleteTrash(root, name);
}

int cleanup(const std::wstring &root, const std::wstring &ownVersion, const std::wstring &failedCsv) {
    SetCurrentDirectoryW(root.c_str());
    trashOldVersions(root, ownVersion, parseFailedCsv(failedCsv));
    removeWorkingFolders(root);
    return EXIT_OK;
}

// --- entry point --------------------------------------------------------------------------------------------------

std::vector<std::wstring> commandLineArgs() {
    std::vector<std::wstring> args;
    int count = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv)
        return args;
    for (int i = 1; i < count; ++i)
        args.push_back(argv[i]);
    LocalFree(argv);
    return args;
}

int runFinishUpdate(const std::wstring &root, const std::vector<std::wstring> &args) {
    const auto separator = std::find(args.begin(), args.end(), L"--");
    const std::vector<std::wstring> original(separator == args.end() ? args.end() : separator + 1, args.end());
    const auto uj = (args.size() >= 4 && args[3] == L"--") ? currentUj(args[1]) : std::nullopt;
    if (!uj)
        return normalLaunch(root, original); // QK4 has exited, so a bad call must still leave the person a working app
    wchar_t *end = nullptr;
    const unsigned long pid = wcstoul(args[2].c_str(), &end, 10);
    return finishUpdate(root, args[1], *uj, (end && *end == L'\0') ? pid : 0, original);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Only an attempt started below may carry this. A leftover value must not reach a normal launch.
    SetEnvironmentVariableW(ATTEMPT_ENV, nullptr);

    const std::wstring root = launcherFolder();
    const std::vector<std::wstring> args = commandLineArgs();
    if (!args.empty() && args[0] == L"--qk4-finish-update")
        return runFinishUpdate(root, args);
    if (args.size() >= 2 && args[0] == L"--qk4-cleanup")
        return cleanup(root, args[1], args.size() >= 3 ? args[2] : std::wstring());
    return normalLaunch(root, args);
}
