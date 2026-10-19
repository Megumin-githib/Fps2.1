#pragma once
#include <windows.h>
#include "particle_background.h"
#include "config.h"

// What the user picked on the launcher's main menu.
enum class LauncherAction { None, Start, Exit };

// The full-screen, game-like "main menu" shown when the exe starts: a dark
// animated background (ParticleBackground) plus three buttons - Запустить
// (start the overlay), Настройки (opens SettingsWindow, modal, over this
// screen), Выход (quit). This is the first thing the user sees, matching
// the "launches like a game" request - the actual translucent HUD overlay
// only appears after they press Запустить.
class LauncherWindow {
public:
    // Creates and shows the window, then runs its own message loop until
    // the user picks Start or Exit (Settings is handled internally - it
    // doesn't end this loop, the launcher just reappears afterward).
    // cfgPath is forwarded to the settings screen so it can persist edits.
    LauncherAction Run(HINSTANCE hInst, AppConfig& cfg, const std::wstring& cfgPath);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT, WPARAM, LPARAM);

    void Layout();
    void Paint(HDC hdc);
    int HitTestButton(int x, int y) const; // -1 none, 0 Start, 1 Settings, 2 Exit
    void OpenSettings();
    void AddTrayIcon();
    void RemoveTrayIcon();

    HWND m_hwnd = nullptr;
    AppConfig* m_cfg = nullptr;
    std::wstring m_cfgPath;
    HINSTANCE m_hInst = nullptr;

    ParticleBackground m_bg;
    ULONG_PTR m_gdiplusToken = 0;

    RECT m_btnStart{}, m_btnSettings{}, m_btnExit{};
    int m_hoverBtn = -1;
    int m_width = 0, m_height = 0;

    LauncherAction m_result = LauncherAction::None;
    bool m_trayAdded = false;
};
