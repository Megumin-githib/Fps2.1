#pragma once
#include <windows.h>
#include <string>
#include "config.h"

// Shows the Settings screen as a modal window over `owner` (normal system
// window chrome - this one doesn't need the "game menu" look, just to be
// easy to read and use). Blocks until the user saves or cancels; on save,
// writes the changes into cfg and persists them to cfgPath, and applies
// the "run at Windows startup" registry entry accordingly.
void ShowSettingsWindow(HINSTANCE hInst, HWND owner, AppConfig& cfg, const std::wstring& cfgPath);
