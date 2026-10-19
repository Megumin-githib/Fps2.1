#pragma once
#include <windows.h>
#include <string>
#include <vector>

// A single running process, as shown in the Settings app picker.
struct ProcessInfo {
    DWORD pid = 0;
    std::wstring exe_name; // e.g. L"game.exe" (no path)
};

// Snapshots currently running processes (via Toolhelp32), de-duplicated by
// executable name and sorted alphabetically. System/idle entries and this
// process itself are filtered out. Safe to call repeatedly (e.g. from a
// "Refresh" button) - each call takes a fresh snapshot.
std::vector<ProcessInfo> EnumerateRunningProcesses();

// Returns true if a process with this executable name is currently running.
bool IsProcessRunning(const std::wstring& exeName);

// Opens the standard "Open File" dialog filtered to *.exe so the user can
// point at a game/app that isn't currently running. Returns just the
// executable's file name (e.g. L"game.exe"), or an empty string if the
// user cancelled.
std::wstring BrowseForExecutable(HWND owner);
