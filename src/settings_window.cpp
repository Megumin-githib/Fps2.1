#include "settings_window.h"
#include "process_utils.h"
#include <commctrl.h>
#include <vector>
#include <algorithm>
#pragma comment(lib, "comctl32.lib")

namespace {

const wchar_t* kClassName = L"FpsOverlaySettingsWindowClass";

enum ControlId : int {
    ID_CHK_AUTOCONNECT = 101,
    ID_COMBO_APP = 102,
    ID_BTN_REFRESH = 103,
    ID_BTN_BROWSE = 104,
    ID_CHK_SHOW_TIME = 105,
    ID_CHK_SHOW_FPS = 106,
    ID_CHK_SHOW_LOWS = 107,
    ID_CHK_SHOW_CPU = 108,
    ID_CHK_SHOW_GPU = 109,
    ID_CHK_SHOW_TEMPS = 110,
    ID_CHK_SHOW_SESSION = 111,
    ID_CHK_CLICKTHROUGH = 112,
    ID_CHK_SHOW_LAUNCHER = 113,
    ID_CHK_RUN_AT_STARTUP = 114,
    ID_BTN_SAVE = 115,
    ID_BTN_CANCEL = 116,
    ID_CHK_SHOW_PING = 117,
    ID_CHK_SHOW_NETWORK = 118,
    ID_CHK_SHOW_RAM = 119,
    ID_CHK_SHOW_FREQ = 120,
    ID_COMBO_PROFILE = 121,
    ID_EDIT_LAUNCHER_FPS = 122,
    ID_EDIT_OVERLAY_FPS = 123,
    ID_EDIT_PING_HOST = 124,
    ID_EDIT_PROFILE_HOTKEY = 125,
    ID_EDIT_WARN_FPS = 126,
    ID_EDIT_WARN_TEMP = 127,
    ID_EDIT_WARN_RAM = 128,
};

// One instance at a time (it's modal), so plain statics are fine - same
// pattern the other windows in this project use for their WndProc trampoline.
struct SettingsState {
    HWND hwnd = nullptr;
    AppConfig* cfg = nullptr;
    std::wstring cfgPath;
    HFONT font = nullptr;
    bool done = false;
    bool fadingIn = false;
    bool fadingOut = false;
    ULONGLONG fadeStartMs = 0;
    BYTE fadeStartAlpha = 0;

    HWND chkAutoConnect = nullptr, comboApp = nullptr, btnRefresh = nullptr, btnBrowse = nullptr;
    HWND chkShowTime = nullptr, chkShowFps = nullptr, chkShowLows = nullptr, chkShowCpu = nullptr;
    HWND chkShowGpu = nullptr, chkShowTemps = nullptr, chkShowSession = nullptr, chkClickThrough = nullptr;
    HWND chkShowLauncher = nullptr, chkRunAtStartup = nullptr;
    HWND chkShowPing = nullptr, chkShowNetwork = nullptr, chkShowRam = nullptr, chkShowFreq = nullptr;
    HWND comboProfile = nullptr, editLauncherFps = nullptr, editOverlayFps = nullptr, editPingHost = nullptr, editProfileHotkey = nullptr;
    HWND editWarnFps = nullptr, editWarnTemp = nullptr, editWarnRam = nullptr;
};

SettingsState* g_state = nullptr;

HWND MakeChild(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
               HWND parent, int id, HINSTANCE hInst) {
    HWND h2 = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE,
                               x, y, w, h, parent, (HMENU)(INT_PTR)id, hInst, nullptr);
    if (h2 && g_state && g_state->font) SendMessageW(h2, WM_SETFONT, (WPARAM)g_state->font, TRUE);
    return h2;
}

void RefreshProcessList(HWND combo, const std::wstring& keepText) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (auto& p : EnumerateRunningProcesses()) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)p.exe_name.c_str());
    }
    SetWindowTextW(combo, keepText.c_str());
}

void UpdateAppPickerEnabled(SettingsState* s) {
    bool enable = (SendMessageW(s->chkAutoConnect, BM_GETCHECK, 0, 0) != BST_CHECKED);
    EnableWindow(s->comboApp, enable);
    EnableWindow(s->btnRefresh, enable);
    EnableWindow(s->btnBrowse, enable);
}

void SetAutostartRegistry(bool enable) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                       0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;

    if (enable) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring quoted = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(key, L"FpsOverlay", 0, REG_SZ,
                        (const BYTE*)quoted.c_str(),
                        (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"FpsOverlay");
    }
    RegCloseKey(key);
}

void SaveFromControls(SettingsState* s) {
    AppConfig& cfg = *s->cfg;

    cfg.auto_connect = SendMessageW(s->chkAutoConnect, BM_GETCHECK, 0, 0) == BST_CHECKED;

    wchar_t buf[MAX_PATH]{};
    GetWindowTextW(s->comboApp, buf, MAX_PATH);
    cfg.target_process = buf;

    cfg.show_time = SendMessageW(s->chkShowTime, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.show_fps = SendMessageW(s->chkShowFps, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool lows = SendMessageW(s->chkShowLows, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.show_1_low = lows;
    cfg.show_01_low = lows;
    cfg.show_cpu = SendMessageW(s->chkShowCpu, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.show_gpu = SendMessageW(s->chkShowGpu, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.show_temps = SendMessageW(s->chkShowTemps, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.show_ping = SendMessageW(s->chkShowPing, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.show_network = SendMessageW(s->chkShowNetwork, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.show_ram = SendMessageW(s->chkShowRam, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.show_frequencies = SendMessageW(s->chkShowFreq, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int profileIndex = (int)SendMessageW(s->comboProfile, CB_GETCURSEL, 0, 0);
    if (profileIndex >= 0 && profileIndex <= 2) {
        wchar_t profile[32]{}; SendMessageW(s->comboProfile, CB_GETLBTEXT, profileIndex, (LPARAM)profile); cfg.profile = profile;
    }
    wchar_t number[32]{};
    GetWindowTextW(s->editLauncherFps, number, 32); cfg.launcher_fps = (std::max)(1, (std::min)(240, _wtoi(number)));
    GetWindowTextW(s->editOverlayFps, number, 32); cfg.overlay_fps = (std::max)(1, (std::min)(240, _wtoi(number)));
    wchar_t host[128]{}; GetWindowTextW(s->editPingHost, host, 128); if (host[0]) cfg.ping_host = host;
    GetWindowTextW(s->editProfileHotkey, number, 32); cfg.hotkey_cycle_profile = (unsigned int)(std::max)(1, (std::min)(255, _wtoi(number)));
    GetWindowTextW(s->editWarnFps, number, 32); cfg.warning_fps = (std::max)(1, (std::min)(240, _wtoi(number)));
    GetWindowTextW(s->editWarnTemp, number, 32); cfg.warning_temp_c = (std::max)(30, (std::min)(120, _wtoi(number)));
    GetWindowTextW(s->editWarnRam, number, 32); cfg.warning_ram_percent = (std::max)(50, (std::min)(100, _wtoi(number)));
    cfg.show_session_time = SendMessageW(s->chkShowSession, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.click_through = SendMessageW(s->chkClickThrough, BM_GETCHECK, 0, 0) == BST_CHECKED;

    cfg.show_launcher_on_start = SendMessageW(s->chkShowLauncher, BM_GETCHECK, 0, 0) == BST_CHECKED;

    bool runAtStartup = SendMessageW(s->chkRunAtStartup, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (runAtStartup != cfg.run_at_startup) SetAutostartRegistry(runAtStartup);
    cfg.run_at_startup = runAtStartup;

    cfg.Save(s->cfgPath);
}

void BeginSettingsFadeOut(SettingsState* s) {
    if (!s || s->fadingOut) return;
    const ULONGLONG now = GetTickCount64();
    if (s->fadingIn) {
        const float progress = (std::min)(1.0f, (float)(now - s->fadeStartMs) / 180.0f);
        s->fadeStartAlpha = (BYTE)(255.0f * progress);
    } else {
        s->fadeStartAlpha = 255;
    }
    s->fadingIn = false;
    s->fadingOut = true;
    s->fadeStartMs = now;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsState* s = g_state;
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_CHK_AUTOCONNECT && HIWORD(wParam) == BN_CLICKED) {
            UpdateAppPickerEnabled(s);
        } else if (id == ID_BTN_REFRESH && HIWORD(wParam) == BN_CLICKED) {
            wchar_t cur[MAX_PATH]{};
            GetWindowTextW(s->comboApp, cur, MAX_PATH);
            RefreshProcessList(s->comboApp, cur);
        } else if (id == ID_BTN_BROWSE && HIWORD(wParam) == BN_CLICKED) {
            std::wstring picked = BrowseForExecutable(hwnd);
            if (!picked.empty()) SetWindowTextW(s->comboApp, picked.c_str());
        } else if (id == ID_BTN_SAVE && HIWORD(wParam) == BN_CLICKED) {
            SaveFromControls(s);
            BeginSettingsFadeOut(s);
        } else if (id == ID_BTN_CANCEL && HIWORD(wParam) == BN_CLICKED) {
            BeginSettingsFadeOut(s);
        }
        return 0;
    }
    case WM_CLOSE:
        BeginSettingsFadeOut(s);
        return 0;
    case WM_TIMER:
        if (wParam == 1 && s) {
            const ULONGLONG now = GetTickCount64();
            if (s->fadingIn) {
                const float progress = (std::min)(1.0f, (float)(now - s->fadeStartMs) / 180.0f);
                SetLayeredWindowAttributes(hwnd, 0, (BYTE)(255.0f * progress), LWA_ALPHA);
                if (progress >= 1.0f) s->fadingIn = false;
            } else if (s->fadingOut) {
                const float progress = (std::min)(1.0f, (float)(now - s->fadeStartMs) / 160.0f);
                SetLayeredWindowAttributes(hwnd, 0, (BYTE)(s->fadeStartAlpha * (1.0f - progress)), LWA_ALPHA);
                if (progress >= 1.0f) {
                    KillTimer(hwnd, 1);
                    DestroyWindow(hwnd);
                }
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        if (s) s->done = true;
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void SetCheck(HWND h, bool v) { SendMessageW(h, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }

} // namespace

void ShowSettingsWindow(HINSTANCE hInst, HWND owner, AppConfig& cfg, const std::wstring& cfgPath) {
    SettingsState state;
    state.cfg = &cfg;
    state.cfgPath = cfgPath;
    state.font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    g_state = &state;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);

    const int winW = 500, winH = 850;
    RECT ownerRect; GetWindowRect(owner, &ownerRect);
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - winW) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - winH) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_LAYERED, kClassName, L"Настройки — FPS Overlay",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                 x, y, winW, winH, owner, nullptr, hInst, nullptr);
    state.hwnd = hwnd;
    g_state = &state;
    if (!hwnd) {
        g_state = nullptr;
        return;
    }

    RECT rc; GetClientRect(hwnd, &rc);
    int clientW = rc.right - rc.left;

    // --- Group: counter binding ---
    MakeChild(L"BUTTON", L"Привязка счётчика", BS_GROUPBOX, 10, 8, clientW - 20, 150, hwnd, 0, hInst);
    state.chkAutoConnect = MakeChild(L"BUTTON", L"Автоматически подключаться к активному приложению",
                                      BS_AUTOCHECKBOX, 26, 30, clientW - 50, 20, hwnd, ID_CHK_AUTOCONNECT, hInst);
    MakeChild(L"STATIC", L"или выберите конкретное приложение для счётчика:", SS_LEFT,
              26, 58, clientW - 50, 18, hwnd, 0, hInst);
    state.comboApp = MakeChild(L"COMBOBOX", L"", CBS_DROPDOWN | WS_VSCROLL | CBS_AUTOHSCROLL,
                                26, 80, clientW - 52, 200, hwnd, ID_COMBO_APP, hInst);
    state.btnRefresh = MakeChild(L"BUTTON", L"Обновить список", BS_PUSHBUTTON,
                                  26, 112, 150, 26, hwnd, ID_BTN_REFRESH, hInst);
    state.btnBrowse = MakeChild(L"BUTTON", L"Обзор...", BS_PUSHBUTTON,
                                 184, 112, 120, 26, hwnd, ID_BTN_BROWSE, hInst);

    // --- Group: what the overlay shows ---
    int gy = 168;
    MakeChild(L"BUTTON", L"Что показывать в оверлее", BS_GROUPBOX, 10, gy, clientW - 20, 190, hwnd, 0, hInst);
    int c1 = 26, c2 = clientW / 2 + 10, rowH = 26, r0 = gy + 24;
    state.chkShowTime = MakeChild(L"BUTTON", L"Время (часы)", BS_AUTOCHECKBOX, c1, r0, 190, 20, hwnd, ID_CHK_SHOW_TIME, hInst);
    state.chkShowFps = MakeChild(L"BUTTON", L"FPS", BS_AUTOCHECKBOX, c2, r0, 190, 20, hwnd, ID_CHK_SHOW_FPS, hInst);
    state.chkShowLows = MakeChild(L"BUTTON", L"1% / 0.1% Low", BS_AUTOCHECKBOX, c1, r0 + rowH, 190, 20, hwnd, ID_CHK_SHOW_LOWS, hInst);
    state.chkShowCpu = MakeChild(L"BUTTON", L"Загрузка CPU", BS_AUTOCHECKBOX, c2, r0 + rowH, 190, 20, hwnd, ID_CHK_SHOW_CPU, hInst);
    state.chkShowGpu = MakeChild(L"BUTTON", L"GPU и VRAM", BS_AUTOCHECKBOX, c1, r0 + rowH * 2, 190, 20, hwnd, ID_CHK_SHOW_GPU, hInst);
    state.chkShowTemps = MakeChild(L"BUTTON", L"Температуры", BS_AUTOCHECKBOX, c2, r0 + rowH * 2, 190, 20, hwnd, ID_CHK_SHOW_TEMPS, hInst);
    state.chkShowSession = MakeChild(L"BUTTON", L"Счётчик времени сессии", BS_AUTOCHECKBOX, c1, r0 + rowH * 3, 190, 20, hwnd, ID_CHK_SHOW_SESSION, hInst);
    state.chkClickThrough = MakeChild(L"BUTTON", L"Клик-сквозь (click-through)", BS_AUTOCHECKBOX, c2, r0 + rowH * 3, 210, 20, hwnd, ID_CHK_CLICKTHROUGH, hInst);
    state.chkShowPing = MakeChild(L"BUTTON", L"Ping", BS_AUTOCHECKBOX, c1, r0 + rowH * 4, 190, 20, hwnd, ID_CHK_SHOW_PING, hInst);
    state.chkShowNetwork = MakeChild(L"BUTTON", L"Скорость сети", BS_AUTOCHECKBOX, c2, r0 + rowH * 4, 190, 20, hwnd, ID_CHK_SHOW_NETWORK, hInst);
    state.chkShowRam = MakeChild(L"BUTTON", L"Оперативная память", BS_AUTOCHECKBOX, c1, r0 + rowH * 5, 190, 20, hwnd, ID_CHK_SHOW_RAM, hInst);
    state.chkShowFreq = MakeChild(L"BUTTON", L"Частоты CPU/GPU", BS_AUTOCHECKBOX, c2, r0 + rowH * 5, 190, 20, hwnd, ID_CHK_SHOW_FREQ, hInst);

    // --- Group: startup behavior ---
    int gy2 = gy + 200 + 10;
    MakeChild(L"BUTTON", L"Запуск", BS_GROUPBOX, 10, gy2, clientW - 20, 90, hwnd, 0, hInst);
    state.chkShowLauncher = MakeChild(L"BUTTON", L"Показывать это меню при запуске exe", BS_AUTOCHECKBOX,
                                       26, gy2 + 24, clientW - 50, 20, hwnd, ID_CHK_SHOW_LAUNCHER, hInst);
    state.chkRunAtStartup = MakeChild(L"BUTTON", L"Запускать вместе с Windows", BS_AUTOCHECKBOX,
                                       26, gy2 + 50, clientW - 50, 20, hwnd, ID_CHK_RUN_AT_STARTUP, hInst);

    // --- Performance and profile controls ---
    int gy3 = gy2 + 100;
    MakeChild(L"BUTTON", L"Профиль и производительность", BS_GROUPBOX, 10, gy3, clientW - 20, 195, hwnd, 0, hInst);
    MakeChild(L"STATIC", L"Профиль:", SS_LEFT, 26, gy3 + 25, 70, 20, hwnd, 0, hInst);
    state.comboProfile = MakeChild(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 95, gy3 + 21, 120, 120, hwnd, ID_COMBO_PROFILE, hInst);
    SendMessageW(state.comboProfile, CB_ADDSTRING, 0, (LPARAM)L"Minimum");
    SendMessageW(state.comboProfile, CB_ADDSTRING, 0, (LPARAM)L"Game");
    SendMessageW(state.comboProfile, CB_ADDSTRING, 0, (LPARAM)L"Full");
    MakeChild(L"STATIC", L"FPS лаунчера:", SS_LEFT, 235, gy3 + 25, 95, 20, hwnd, 0, hInst);
    state.editLauncherFps = MakeChild(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 330, gy3 + 21, 55, 24, hwnd, ID_EDIT_LAUNCHER_FPS, hInst);
    MakeChild(L"STATIC", L"FPS оверлея:", SS_LEFT, 26, gy3 + 60, 90, 20, hwnd, 0, hInst);
    state.editOverlayFps = MakeChild(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 120, gy3 + 56, 55, 24, hwnd, ID_EDIT_OVERLAY_FPS, hInst);
    MakeChild(L"STATIC", L"Ping host (IPv4):", SS_LEFT, 190, gy3 + 60, 100, 20, hwnd, 0, hInst);
    state.editPingHost = MakeChild(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 290, gy3 + 56, 145, 24, hwnd, ID_EDIT_PING_HOST, hInst);
    MakeChild(L"STATIC", L"Код клавиши профиля:", SS_LEFT, 26, gy3 + 95, 135, 20, hwnd, 0, hInst);
    state.editProfileHotkey = MakeChild(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 165, gy3 + 91, 55, 24, hwnd, ID_EDIT_PROFILE_HOTKEY, hInst);
    MakeChild(L"STATIC", L"Ctrl+Alt + выбранная клавиша. FPS: 1–240.", SS_LEFT, 235, gy3 + 95, clientW - 255, 25, hwnd, 0, hInst);
    MakeChild(L"STATIC", L"Предупреждения: FPS ниже", SS_LEFT, 26, gy3 + 140, 130, 20, hwnd, 0, hInst);
    state.editWarnFps = MakeChild(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 155, gy3 + 136, 45, 24, hwnd, ID_EDIT_WARN_FPS, hInst);
    MakeChild(L"STATIC", L"Температура °C:", SS_LEFT, 210, gy3 + 140, 100, 20, hwnd, 0, hInst);
    state.editWarnTemp = MakeChild(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 310, gy3 + 136, 45, 24, hwnd, ID_EDIT_WARN_TEMP, hInst);
    MakeChild(L"STATIC", L"RAM %:", SS_LEFT, 360, gy3 + 140, 55, 20, hwnd, 0, hInst);
    state.editWarnRam = MakeChild(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 415, gy3 + 136, 40, 24, hwnd, ID_EDIT_WARN_RAM, hInst);

    // --- Save / Cancel ---
    int by = gy3 + 205;
    MakeChild(L"BUTTON", L"Сохранить", BS_PUSHBUTTON | BS_DEFPUSHBUTTON, clientW - 220, by, 100, 30, hwnd, ID_BTN_SAVE, hInst);
    MakeChild(L"BUTTON", L"Отмена", BS_PUSHBUTTON, clientW - 110, by, 100, 30, hwnd, ID_BTN_CANCEL, hInst);

    // Populate initial values from cfg.
    SetCheck(state.chkAutoConnect, cfg.auto_connect);
    RefreshProcessList(state.comboApp, cfg.target_process);
    SetCheck(state.chkShowTime, cfg.show_time);
    SetCheck(state.chkShowFps, cfg.show_fps);
    SetCheck(state.chkShowLows, cfg.show_1_low || cfg.show_01_low);
    SetCheck(state.chkShowCpu, cfg.show_cpu);
    SetCheck(state.chkShowGpu, cfg.show_gpu);
    SetCheck(state.chkShowTemps, cfg.show_temps);
    SetCheck(state.chkShowPing, cfg.show_ping);
    SetCheck(state.chkShowNetwork, cfg.show_network);
    SetCheck(state.chkShowRam, cfg.show_ram);
    SetCheck(state.chkShowFreq, cfg.show_frequencies);
    SetCheck(state.chkShowSession, cfg.show_session_time);
    SetCheck(state.chkClickThrough, cfg.click_through);
    SetCheck(state.chkShowLauncher, cfg.show_launcher_on_start);
    SetCheck(state.chkRunAtStartup, cfg.run_at_startup);
    int profileIndex = _wcsicmp(cfg.profile.c_str(), L"Minimum") == 0 ? 0 : (_wcsicmp(cfg.profile.c_str(), L"Game") == 0 ? 1 : 2);
    SendMessageW(state.comboProfile, CB_SETCURSEL, profileIndex, 0);
    wchar_t num[16]; swprintf_s(num, L"%d", cfg.launcher_fps); SetWindowTextW(state.editLauncherFps, num);
    swprintf_s(num, L"%d", cfg.overlay_fps); SetWindowTextW(state.editOverlayFps, num);
    SetWindowTextW(state.editPingHost, cfg.ping_host.c_str());
    swprintf_s(num, L"%u", cfg.hotkey_cycle_profile); SetWindowTextW(state.editProfileHotkey, num);
    swprintf_s(num, L"%d", cfg.warning_fps); SetWindowTextW(state.editWarnFps, num);
    swprintf_s(num, L"%d", cfg.warning_temp_c); SetWindowTextW(state.editWarnTemp, num);
    swprintf_s(num, L"%d", cfg.warning_ram_percent); SetWindowTextW(state.editWarnRam, num);
    UpdateAppPickerEnabled(&state);

    EnableWindow(owner, FALSE);
    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    state.fadingIn = true;
    state.fadeStartMs = GetTickCount64();
    SetTimer(hwnd, 1, 16, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    g_state = nullptr;
}
