#include "session_monitor.h"
#include "process_utils.h"
#include <tlhelp32.h>

namespace {
ULONGLONG FileTimeTo100ns(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}
} // namespace

bool SessionMonitor::TryReadProcessStart(const std::wstring& exeName, ULONGLONG& outStart100ns) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    DWORD pid = 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (!pid) return false;

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;

    FILETIME creation{}, exitT{}, kernelT{}, userT{};
    bool ok = GetProcessTimes(h, &creation, &exitT, &kernelT, &userT) != 0;
    CloseHandle(h);
    if (!ok) return false;

    outStart100ns = FileTimeTo100ns(creation);
    return true;
}

void SessionMonitor::AttachOrUpdate(const std::wstring& exeName) {
    const ULONGLONG nowMs = GetTickCount64();
    // Process discovery is not required every render frame. Polling once per
    // second keeps the session timer responsive without repeated snapshots.
    if (m_exeName == exeName && m_lastCheckMs != 0 && nowMs - m_lastCheckMs < 1000)
        return;
    m_lastCheckMs = nowMs;

    bool running = IsProcessRunning(exeName);

    if (!running) {
        m_running = false;
        m_exeName = exeName;
        return;
    }

    // Already tracking this exact app and it's still the running instance
    // we attached to - nothing to do (don't reset the counter every tick).
    if (m_running && m_exeName == exeName) return;

    m_exeName = exeName;
    m_running = true;

    ULONGLONG start100ns = 0;
    if (TryReadProcessStart(exeName, start100ns)) {
        m_start100ns = start100ns;
    } else {
        // Couldn't read the real creation time (rare - usually a
        // permissions quirk) - count from right now instead.
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        m_start100ns = FileTimeTo100ns(now);
    }
}

void SessionMonitor::Detach() {
    m_running = false;
    m_exeName.clear();
}

std::wstring SessionMonitor::GetElapsedString() const {
    if (!m_running) return L"--:--:--";

    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULONGLONG now100ns = FileTimeTo100ns(now);

    ULONGLONG elapsed100ns = (now100ns > m_start100ns) ? (now100ns - m_start100ns) : 0;
    ULONGLONG totalSeconds = elapsed100ns / 10000000ULL;

    unsigned hours = (unsigned)(totalSeconds / 3600);
    unsigned mins = (unsigned)((totalSeconds % 3600) / 60);
    unsigned secs = (unsigned)(totalSeconds % 60);

    wchar_t buf[32];
    swprintf_s(buf, L"%02u:%02u:%02u", hours, mins, secs);
    return buf;
}
