// Stand-in for a version folder's QK4.exe in the launcher integration tests.
//
// With QK4_UPDATE_ATTEMPT set it follows FAKE_MODE. Without it, it is an ordinary running copy, so a rollback or a
// normal launch of the previous version never inherits the failure being tested. It records every start in
// FAKE_LOG_DIR\<pid>.log. It exits by itself after SAFETY_LIMIT_MS so a broken run cannot leave processes behind.
#include <windows.h>

#include <shellapi.h>

#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr DWORD SAFETY_LIMIT_MS = 300000;

std::wstring envValue(const wchar_t *name) {
    wchar_t buffer[4096];
    const DWORD n = GetEnvironmentVariableW(name, buffer, 4096);
    return (n > 0 && n < 4096) ? std::wstring(buffer, n) : std::wstring();
}

std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring exePath() {
    std::wstring path(32768, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), 32768));
    return path;
}

std::wstring exeFolder() {
    const std::wstring path = exePath();
    return path.substr(0, path.find_last_of(L'\\'));
}

// Written to a temporary name and renamed so the test never reads half a record.
void recordStart(const std::wstring &attempt, bool restarted) {
    const std::wstring dir = envValue(L"FAKE_LOG_DIR");
    if (dir.empty())
        return;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const unsigned long long ticks = (static_cast<unsigned long long>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    const std::wstring base = dir + L"\\" + std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream out((base + L".tmp").c_str(), std::ios::binary);
        out << "exe=" << utf8(exePath()) << "\n";
        out << "attempt=" << utf8(attempt) << "\n";
        out << "restarted=" << (restarted ? 1 : 0) << "\n";
        out << "started=" << ticks << "\n";
        int argc = 0;
        wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; ++i)
            out << "arg=" << utf8(argv[i]) << "\n";
    }
    MoveFileW((base + L".tmp").c_str(), (base + L".log").c_str());
}

void writeReady(const std::wstring &attempt) {
    std::ofstream((exeFolder() + L"\\.ready-" + attempt).c_str(), std::ios::binary).put('1');
    SetEnvironmentVariableW(L"QK4_UPDATE_ATTEMPT", nullptr);
}

int runIdle() {
    Sleep(SAFETY_LIMIT_MS);
    return 0;
}

int runWindow(HINSTANCE instance) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = L"FakeQk4Window";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"Fake QK4", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr,
                                  nullptr, instance, nullptr);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    SetTimer(window, 1, SAFETY_LIMIT_MS, nullptr);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_TIMER)
            break;
        DispatchMessageW(&msg);
    }
    return 0;
}

// Same process creation as a plain restart: no breakaway, no job handling, the environment passes through.
int restartSelf() {
    SetEnvironmentVariableW(L"FAKE_RESTARTED", L"1");
    const std::wstring path = exePath();
    std::vector<wchar_t> commandLine(GetCommandLineW(), GetCommandLineW() + wcslen(GetCommandLineW()) + 1);
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(path.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return 7;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const std::wstring attempt = envValue(L"QK4_UPDATE_ATTEMPT");
    const std::wstring mode = attempt.empty() ? L"idle" : envValue(L"FAKE_MODE");
    const bool restarted = !envValue(L"FAKE_RESTARTED").empty();
    recordStart(attempt, restarted);

    if (mode == L"ready") {
        Sleep(300);
        writeReady(attempt);
        return runIdle();
    }
    if (mode == L"crash-after-ready") {
        writeReady(attempt);
        Sleep(2000);
        return 3;
    }
    if (mode == L"clean-exit-after-ready") {
        writeReady(attempt);
        Sleep(2000);
        return 0;
    }
    if (mode == L"never-ready")
        return runIdle();
    if (mode == L"no-file-but-window")
        return runWindow(instance);
    if (mode == L"restart-then-ready") {
        if (!restarted)
            return restartSelf();
        Sleep(300);
        writeReady(attempt);
        return runIdle();
    }
    if (mode == L"start-fail")
        return 5;
    return runIdle();
}
