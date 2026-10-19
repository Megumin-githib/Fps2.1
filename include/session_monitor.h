#pragma once
#include <windows.h>
#include <string>

// Tracks how long the bound application has been open - this is the
// "счётчик" the Settings screen lets you attach to a chosen app.
//
// When we attach to a process that's already running, we read its real
// creation time via GetProcessTimes() so the counter shows true uptime
// (not just "time since FpsOverlay noticed it"). If that's unavailable
// (e.g. insufficient rights, or the process isn't running yet and we're
// just waiting for it) we fall back to counting from the moment we
// attached.
class SessionMonitor {
public:
    // (Re)attaches to exeName, resetting the counter to that process's
    // actual start time when possible. Safe to call every tick - it is a
    // no-op if we're already attached to a running instance of this name.
    void AttachOrUpdate(const std::wstring& exeName);

    void Detach();
    bool IsRunning() const { return m_running; }

    // "ЧЧ:ММ:СС" elapsed, or "--:--:--" if not currently attached/running.
    std::wstring GetElapsedString() const;

private:
    // Looks up the running process's real creation time. Returns it as
    // 100ns units since 1601 (the native FILETIME epoch), same units
    // GetSystemTimeAsFileTime() uses, so elapsed-time math stays simple.
    bool TryReadProcessStart(const std::wstring& exeName, ULONGLONG& outStart100ns);

    std::wstring m_exeName;
    bool m_running = false;
    ULONGLONG m_start100ns = 0;
    ULONGLONG m_lastCheckMs = 0;
};
