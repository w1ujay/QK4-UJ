// The install-wide update lock: an OS file handle on <root>\.update.lock opened with no sharing. It is held only for
// moments. The kernel releases it if the holder dies, so a stale lock is impossible and the file itself means nothing.
#pragma once

#include <windows.h>

#include <string>

class UpdateLock {
public:
    static constexpr DWORD WAIT_MS = 10000;

    explicit UpdateLock(const std::wstring &root) {
        const std::wstring path = root + L"\\.update.lock";
        const ULONGLONG start = GetTickCount64();
        for (;;) {
            m_handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
            if (m_handle != INVALID_HANDLE_VALUE)
                return;
            // WHY only this error: anything else, such as a read-only root, will not clear by waiting, and waiting
            // would add 10 s to every launch.
            if (GetLastError() != ERROR_SHARING_VIOLATION || GetTickCount64() - start >= WAIT_MS)
                return;
            Sleep(50);
        }
    }

    ~UpdateLock() { release(); }
    UpdateLock(const UpdateLock &) = delete;
    UpdateLock &operator=(const UpdateLock &) = delete;

    bool held() const { return m_handle != INVALID_HANDLE_VALUE; }

    void release() {
        if (held())
            CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};
