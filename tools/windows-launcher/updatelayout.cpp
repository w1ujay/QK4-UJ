#include "updatelayout.h"

#include <algorithm>
#include <limits>

namespace {

bool isSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\v' || c == L'\f' || c == 0xFEFF;
}

std::wstring trim(const std::wstring &s) {
    size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b]))
        ++b;
    while (e > b && isSpace(s[e - 1]))
        --e;
    return s.substr(b, e - b);
}

// A folder name must be one plain path component: no separators, no "..", no drive colon.
bool isSafeComponent(const std::wstring &n) {
    if (n.empty() || n == L"." || n == L"..")
        return false;
    return n.find_first_of(L"/\\:") == std::wstring::npos && n.find(L"..") == std::wstring::npos;
}

wchar_t lower(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

bool sameName(const std::wstring &a, const std::wstring &b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

} // namespace

std::optional<int> ujFromFolderName(const std::wstring &name) {
    static const std::wstring marker = L"-uj.";
    size_t end = name.size();
    size_t start = end;
    while (start > 0 && name[start - 1] >= L'0' && name[start - 1] <= L'9')
        --start;
    if (start == end || start < marker.size() || name.compare(start - marker.size(), marker.size(), marker) != 0)
        return std::nullopt;
    long long value = 0;
    for (size_t i = start; i < end; ++i) {
        value = value * 10 + (name[i] - L'0');
        if (value > std::numeric_limits<int>::max())
            return std::nullopt;
    }
    return static_cast<int>(value);
}

std::optional<int> currentUj(const std::wstring &currentTxt) {
    const std::wstring current = trim(currentTxt);
    return isSafeComponent(current) ? ujFromFolderName(current) : std::nullopt;
}

std::optional<std::wstring> selectVersion(const std::wstring &currentTxt, const std::vector<VersionFolder> &folders) {
    const std::wstring current = trim(currentTxt);
    if (isSafeComponent(current)) {
        for (const auto &f : folders)
            if (f.hasExe && sameName(f.name, current))
                return f.name;
    }
    // Fallback: highest N with .healthy, else highest N with an exe.
    const VersionFolder *bestHealthy = nullptr, *bestAny = nullptr;
    int healthyN = -1, anyN = -1;
    for (const auto &f : folders) {
        if (!f.hasExe || !isSafeComponent(f.name))
            continue;
        auto n = ujFromFolderName(f.name);
        if (!n)
            continue;
        if (*n > anyN) {
            anyN = *n;
            bestAny = &f;
        }
        if (f.hasHealthy && *n > healthyN) {
            healthyN = *n;
            bestHealthy = &f;
        }
    }
    if (bestHealthy)
        return bestHealthy->name;
    if (bestAny)
        return bestAny->name;
    return std::nullopt;
}

// CommandLineToArgvW rules: backslashes are literal unless they precede a double quote. 2n backslashes + quote gives
// n backslashes and a delimiter quote; 2n+1 backslashes + quote gives n backslashes and a literal quote. So before a
// quote (or the closing quote we add) every backslash is doubled, and a literal quote gets one extra backslash.
std::wstring quoteArgument(const std::wstring &arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(c);
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\'); // trailing backslashes precede the closing quote
    out.push_back(L'"');
    return out;
}

std::wstring joinCommandLine(const std::vector<std::wstring> &args) {
    std::wstring out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i)
            out.push_back(L' ');
        out += quoteArgument(args[i]);
    }
    return out;
}

bool shouldCommit(int newUj, std::optional<int> currentUj) {
    return !currentUj || newUj > *currentUj;
}

FailureAction decideFailure(int attemptUj, std::optional<int> currentUj, bool otherProcessesInFolder) {
    if (currentUj && *currentUj >= attemptUj)
        return FailureAction::StartCurrentNoNote;
    if (otherProcessesInFolder)
        return FailureAction::KeepFolderWriteNote;
    return FailureAction::TrashFolderWriteNote;
}

std::vector<std::wstring> foldersToClean(const std::wstring &ownVersion, const std::wstring &currentTxt,
                                         const std::vector<std::wstring> &folders, const std::set<int> &failedUj) {
    std::vector<std::wstring> result;
    const std::wstring current = trim(currentTxt);
    if (!isSafeComponent(current) || !sameName(ownVersion, current))
        return result;
    auto currentN = ujFromFolderName(current);
    if (!currentN)
        return result;
    int nextLower = -1;
    for (const auto &f : folders) {
        auto n = ujFromFolderName(f);
        if (n && *n < *currentN)
            nextLower = std::max(nextLower, *n);
    }
    for (const auto &f : folders) {
        auto n = ujFromFolderName(f);
        if (!n || !isSafeComponent(f) || sameName(f, current))
            continue;
        if (*n > *currentN) {
            if (failedUj.count(*n))
                result.push_back(f);
        } else if (*n != nextLower) {
            result.push_back(f);
        }
    }
    return result;
}
