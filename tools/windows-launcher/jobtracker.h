// One update attempt's processes, in a Windows job object. Terminating the job stops exactly what this attempt started,
// including a restarted replacement, and no other copy. There is no kill-on-close, so a healthy attempt keeps running
// after the launcher exits, and no breakaway flags.
#pragma once

#include <windows.h>

#include <map>
#include <string>
#include <vector>

class JobTracker {
public:
    JobTracker() = default;
    JobTracker(const JobTracker &) = delete;
    JobTracker &operator=(const JobTracker &) = delete;

    ~JobTracker() {
        for (auto &entry : m_processes)
            CloseHandle(entry.second);
        if (m_job)
            CloseHandle(m_job);
    }

    // Suspended until it is in the job, so it cannot start a child that escapes.
    bool start(const std::wstring &exe, std::wstring commandLine) {
        m_job = CreateJobObjectW(nullptr, nullptr);
        if (!m_job)
            return false;
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                            nullptr, &si, &pi))
            return false;
        if (!AssignProcessToJobObject(m_job, pi.hProcess)) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }
        m_processes[pi.dwProcessId] = pi.hProcess;
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        return true;
    }

    // Opens any new job process, because the job does not keep exit codes. Call this often: a replacement that lives
    // for less than one call is only seen as an empty job.
    void refresh() {
        for (DWORD id : activeIds()) {
            if (m_processes.count(id))
                continue;
            HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
            if (process)
                m_processes[id] = process;
        }
    }

    // -1 if the job cannot be queried, which callers must not read as empty.
    int activeCount() const {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info = {};
        if (!m_job ||
            !QueryInformationJobObject(m_job, JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr))
            return m_job ? -1 : 0;
        return static_cast<int>(info.ActiveProcesses);
    }

    void terminate() {
        if (m_job)
            TerminateJobObject(m_job, 1);
    }

    void waitUntilEmpty(DWORD timeoutMs) const {
        const ULONGLONG start = GetTickCount64();
        while (activeCount() > 0 && GetTickCount64() - start < timeoutMs)
            Sleep(25);
    }

    bool anyVisibleWindow() {
        struct Search {
            std::vector<DWORD> ids;
            bool found = false;
        } search;
        search.ids = activeIds();
        EnumWindows(
            [](HWND window, LPARAM param) -> BOOL {
                auto *s = reinterpret_cast<Search *>(param);
                DWORD owner = 0;
                GetWindowThreadProcessId(window, &owner);
                for (DWORD id : s->ids) {
                    if (id == owner && IsWindowVisible(window)) {
                        s->found = true;
                        return FALSE;
                    }
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    bool anyNonZeroExit() const {
        for (const auto &entry : m_processes) {
            // A process can leave the job's count a moment before its exit code is readable.
            WaitForSingleObject(entry.second, 1000);
            DWORD code = STILL_ACTIVE;
            if (GetExitCodeProcess(entry.second, &code) && code != STILL_ACTIVE && code != 0)
                return true;
        }
        return false;
    }

private:
    std::vector<DWORD> activeIds() const {
        std::vector<DWORD> ids;
        if (!m_job)
            return ids;
        constexpr DWORD MAX_IDS = 256;
        std::vector<char> buffer(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + MAX_IDS * sizeof(ULONG_PTR));
        auto *list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST *>(buffer.data());
        if (!QueryInformationJobObject(m_job, JobObjectBasicProcessIdList, list, static_cast<DWORD>(buffer.size()),
                                       nullptr))
            return ids;
        for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i)
            ids.push_back(static_cast<DWORD>(list->ProcessIdList[i]));
        return ids;
    }

    HANDLE m_job = nullptr;
    std::map<DWORD, HANDLE> m_processes;
};
