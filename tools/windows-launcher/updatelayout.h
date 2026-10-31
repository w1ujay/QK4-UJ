// Pure decision logic for the Windows launcher. Standard C++17 only (no Qt, no Win32),
// so it builds in the launcher and in the Linux unit tests.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

// N from a folder name ending in "-uj.<N>" (digits only). Anything else, including "-uj.test-1", gives nullopt.
std::optional<int> ujFromFolderName(const std::wstring &name);

struct VersionFolder {
    std::wstring name;
    bool hasExe;
    bool hasHealthy;
};

// Picks the folder a normal launch should run. currentTxt is the raw content of current.txt (may carry whitespace
// or newlines). Returns the folder name, or nullopt if nothing is runnable.
std::optional<std::wstring> selectVersion(const std::wstring &currentTxt, const std::vector<VersionFolder> &folders);

// Quote one argument so CommandLineToArgvW parses it back to the same string.
std::wstring quoteArgument(const std::wstring &arg);
std::wstring joinCommandLine(const std::vector<std::wstring> &args);

// True if a healthy new version should replace current.txt.
bool shouldCommit(int newUj, std::optional<int> currentUj);

enum class FailureAction { StartCurrentNoNote, KeepFolderWriteNote, TrashFolderWriteNote };
FailureAction decideFailure(int attemptUj, std::optional<int> currentUj, bool otherProcessesInFolder);

// Folders cleanup may try to remove. Empty unless ownVersion is the current version. Keeps the current version and
// the next lower N; keeps any higher N unless it is in failedUj. Result keeps the input order.
std::vector<std::wstring> foldersToClean(const std::wstring &ownVersion, const std::wstring &currentTxt,
                                         const std::vector<std::wstring> &folders, const std::set<int> &failedUj);
