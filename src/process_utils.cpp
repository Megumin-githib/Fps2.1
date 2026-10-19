#include "process_utils.h"
#include <tlhelp32.h>
#include <commdlg.h>
#include <algorithm>
#include <set>
#pragma comment(lib, "comdlg32.lib")

namespace {

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

} // namespace

std::vector<ProcessInfo> EnumerateRunningProcesses() {
    std::vector<ProcessInfo> result;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;

    wchar_t selfName[MAX_PATH]{};
    GetModuleFileNameW(nullptr, selfName, MAX_PATH);
    std::wstring self(selfName);
    auto selfPos = self.find_last_of(L"\\/");
    std::wstring selfExe = ToLower(selfPos == std::wstring::npos ? self : self.substr(selfPos + 1));

    std::set<std::wstring> seen;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring name(pe.szExeFile);
            if (name.empty()) continue;
            std::wstring lower = ToLower(name);

            // Skip ourselves and a short list of background/system noise
            // that would otherwise clutter the picker.
            if (lower == selfExe) continue;
            if (lower == L"system" || lower == L"system idle process" ||
                lower == L"registry" || lower == L"memory compression" ||
                lower == L"svchost.exe" || lower == L"csrss.exe" ||
                lower == L"wininit.exe" || lower == L"winlogon.exe" ||
                lower == L"smss.exe" || lower == L"dwm.exe" ||
                lower == L"fontdrvhost.exe" || lower == L"services.exe" ||
                lower == L"lsass.exe")
                continue;

            if (!seen.insert(lower).second) continue; // de-dupe by name

            ProcessInfo info;
            info.pid = pe.th32ProcessID;
            info.exe_name = name;
            result.push_back(info);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    std::sort(result.begin(), result.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
        return _wcsicmp(a.exe_name.c_str(), b.exe_name.c_str()) < 0;
    });
    return result;
}

bool IsProcessRunning(const std::wstring& exeName) {
    std::wstring target = ToLower(exeName);
    for (auto& p : EnumerateRunningProcesses()) {
        if (ToLower(p.exe_name) == target) return true;
    }
    return false;
}

std::wstring BrowseForExecutable(HWND owner) {
    wchar_t fileBuf[MAX_PATH] = L"";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Программы (*.exe)\0*.exe\0Все файлы\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Выберите приложение";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return L"";

    std::wstring full(fileBuf);
    auto pos = full.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? full : full.substr(pos + 1);
}
