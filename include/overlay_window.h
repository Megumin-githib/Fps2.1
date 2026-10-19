#pragma once
#include <windows.h>
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <functional>
#include "config.h"

// A borderless, per-pixel-alpha ("layered") always-on-top window that
// renders a small translucent HUD. Draggable with the left mouse button;
// position is written back into AppConfig (and persisted on exit by the
// caller). Two global hotkeys are registered: toggle visibility and
// toggle click-through (so the overlay stops intercepting mouse clicks).
class OverlayWindow {
public:
    bool Create(HINSTANCE hInst, AppConfig& cfg, const std::wstring& cfgPath = L"config.ini");

    void SetLines(const std::vector<std::wstring>& lines);
    void Render();

    void ToggleVisible();
    void ToggleClickThrough();

    HWND Handle() const { return m_hwnd; }

    // Called on every WM_TIMER tick (set up by the caller with SetTimer).
    std::function<void()> OnTick;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT, WPARAM, LPARAM);
    void ApplyExStyle();
    void BeginVisibilityFade(bool show, bool hideToTray);
    void AddTrayIcon();
    void RemoveTrayIcon();

    HWND m_hwnd = nullptr;
    AppConfig* m_cfg = nullptr;
    std::wstring m_cfgPath;
    std::vector<std::wstring> m_lines;
    bool m_contentDirty = true;
    bool m_secondImageActive = false;
    bool m_haveWindowPosition = false;
    int m_lastWindowX = 0;
    int m_lastWindowY = 0;
    bool m_visible = true;
    bool m_dragging = false;
    POINT m_dragOffset{};
    ULONG_PTR m_gdiplusToken = 0;
    Gdiplus::Image* m_backgroundImage = nullptr;
    Gdiplus::Image* m_backgroundImage2 = nullptr;
    ULONGLONG m_lastRenderMs = 0;
    HDC m_memDC = nullptr;
    HBITMAP m_bitmap = nullptr;
    HBITMAP m_originalBitmap = nullptr;
    void* m_bitmapBits = nullptr;
    int m_bitmapWidth = 0;
    int m_bitmapHeight = 0;
    BYTE m_windowAlpha = 255;
    BYTE m_fadeStartAlpha = 255;
    BYTE m_fadeTargetAlpha = 255;
    ULONGLONG m_fadeStartMs = 0;
    bool m_alphaAnimating = false;
    bool m_hideAfterFade = false;
    bool m_trayAdded = false;
};
