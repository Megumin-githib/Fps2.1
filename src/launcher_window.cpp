#include "launcher_window.h"
#include "settings_window.h"
#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM
#include <gdiplus.h>
#include <string>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace {
LauncherWindow* g_instance = nullptr;
const wchar_t* kClassName = L"FpsOverlayLauncherWindowClass";
const UINT_PTR kAnimTimerId = 1;
const UINT WM_LAUNCHER_TRAY = WM_APP + 43;
const UINT ID_LAUNCHER_TRAY_SHOW = 42001;
const UINT ID_LAUNCHER_TRAY_EXIT = 42002;
}

LRESULT CALLBACK LauncherWindow::WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (g_instance) {
        // Messages (WM_NCCREATE, WM_SIZE...) arrive *during* CreateWindowExW,
        // before it returns and m_hwnd is assigned. Capture the handle here,
        // otherwise DefWindowProcW(nullptr, WM_NCCREATE) fails and the window
        // is never created (the app silently exits).
        g_instance->m_hwnd = hwnd;
        return g_instance->HandleMessage(msg, w, l);
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

void LauncherWindow::Layout() {
    RECT rc{}; if (!m_hwnd || !GetClientRect(m_hwnd, &rc)) return;
    m_width = rc.right - rc.left;
    m_height = rc.bottom - rc.top;

    const int btnW = 320, btnH = 56, gap = 20;
    int cx = m_width / 2;
    int topY = m_height / 2 - 10;

    m_btnStart = {cx - btnW / 2, topY, cx + btnW / 2, topY + btnH};
    m_btnSettings = {cx - btnW / 2, topY + btnH + gap, cx + btnW / 2, topY + 2 * btnH + gap};
    m_btnExit = {cx - btnW / 2, topY + 2 * (btnH + gap), cx + btnW / 2, topY + 3 * btnH + 2 * gap};

    m_bg.Resize(m_width, m_height);
}

int LauncherWindow::HitTestButton(int x, int y) const {
    POINT pt{x, y};
    if (PtInRect(&m_btnStart, pt)) return 0;
    if (PtInRect(&m_btnSettings, pt)) return 1;
    if (PtInRect(&m_btnExit, pt)) return 2;
    return -1;
}

namespace {
BYTE LerpByte(BYTE from, BYTE to, float amount) {
    amount = (std::max)(0.0f, (std::min)(1.0f, amount));
    return (BYTE)(from + (to - from) * amount);
}

void DrawButton(Graphics& g, const RECT& r, const wchar_t* label, float hover, float press, const FontFamily& ff) {
    const int yOffset = (int)(2.0f * press);
    Rect rect(r.left, r.top + yOffset, r.right - r.left, r.bottom - r.top);
    GraphicsPath path;
    const int radius = 10;
    path.AddArc(rect.X, rect.Y, radius * 2, radius * 2, 180, 90);
    path.AddArc(rect.X + rect.Width - radius * 2, rect.Y, radius * 2, radius * 2, 270, 90);
    path.AddArc(rect.X + rect.Width - radius * 2, rect.Y + rect.Height - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();

    const Color fillTop(LerpByte(190, 230, hover), LerpByte(28, 60, hover),
                        LerpByte(32, 90, hover), LerpByte(50, 150, hover));
    const Color fillBottom(LerpByte(190, 230, hover), LerpByte(18, 40, hover),
                           LerpByte(20, 60, hover), LerpByte(36, 120, hover));
    LinearGradientBrush fill(rect, fillTop, fillBottom, LinearGradientModeVertical);
    g.FillPath(&fill, &path);

    Pen border(Color(LerpByte(140, 255, hover), LerpByte(90, 150, hover),
                     LerpByte(100, 190, hover), LerpByte(140, 255, hover)),
               1.0f + 0.6f * hover);
    g.DrawPath(&border, &path);

    Font font(&ff, 17.0f, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(LerpByte(230, 255, hover), LerpByte(215, 255, hover),
                               LerpByte(220, 255, hover), LerpByte(235, 255, hover)));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    RectF textRect((Gdiplus::REAL)r.left, (Gdiplus::REAL)(r.top + yOffset), (Gdiplus::REAL)(r.right - r.left), (Gdiplus::REAL)(r.bottom - r.top));
    g.DrawString(label, -1, &font, textRect, &fmt, &textBrush);
}
} // namespace

void LauncherWindow::Paint(HDC hdc) {
    RECT rc; GetClientRect(m_hwnd, &rc);
    if (m_width <= 0 || m_height <= 0) return;
    if (!m_paintDC) m_paintDC = CreateCompatibleDC(hdc);
    if (!m_paintDC) return;

    if (!m_paintBitmap || m_paintWidth != m_width || m_paintHeight != m_height) {
        if (m_paintBitmap) {
            SelectObject(m_paintDC, m_paintOriginalBitmap);
            DeleteObject(m_paintBitmap);
            m_paintBitmap = nullptr;
            m_paintOriginalBitmap = nullptr;
        }
        m_paintBitmap = CreateCompatibleBitmap(hdc, m_width, m_height);
        if (!m_paintBitmap) return;
        m_paintOriginalBitmap = (HBITMAP)SelectObject(m_paintDC, m_paintBitmap);
        m_paintWidth = m_width;
        m_paintHeight = m_height;
    }

    {
        Graphics g(m_paintDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        m_bg.Draw(g);

        // Vector cyberpunk emblem: neon-blue hexagon with a purple circuit accent.
        const INT logoCx = m_width / 2;
        const INT logoCy = m_height / 2 - 220;
        Point logoPoints[6] = {
            Point(logoCx, logoCy - 34), Point(logoCx + 31, logoCy - 17),
            Point(logoCx + 31, logoCy + 17), Point(logoCx, logoCy + 34),
            Point(logoCx - 31, logoCy + 17), Point(logoCx - 31, logoCy - 17)
        };
        Pen logoGlow(Color(55, 30, 205, 255), 8.0f);
        Pen logoBlue(Color(240, 45, 215, 255), 2.0f);
        Pen logoPurple(Color(230, 190, 75, 255), 2.0f);
        g.DrawPolygon(&logoGlow, logoPoints, 6);
        g.DrawPolygon(&logoBlue, logoPoints, 6);
        g.DrawLine(&logoPurple, logoCx - 11, logoCy - 15, logoCx + 12, logoCy - 15);
        g.DrawLine(&logoBlue, logoCx - 11, logoCy - 15, logoCx - 11, logoCy + 15);
        g.DrawLine(&logoPurple, logoCx - 11, logoCy, logoCx + 7, logoCy);
        g.DrawLine(&logoBlue, logoCx - 11, logoCy + 15, logoCx + 12, logoCy + 15);
        g.DrawLine(&logoPurple, logoCx + 18, logoCy - 26, logoCx + 28, logoCy - 26);
        g.DrawLine(&logoPurple, logoCx + 28, logoCy - 26, logoCx + 28, logoCy - 16);

        FontFamily ff(L"Segoe UI");

        // Title
        Font titleFont(&ff, 40.0f, FontStyleBold, UnitPixel);
        SolidBrush titleBrush(Color(255, 235, 240, 255));
        StringFormat centerFmt;
        centerFmt.SetAlignment(StringAlignmentCenter);
        RectF titleRect(0.0f, (Gdiplus::REAL)(m_height / 2 - 170), (Gdiplus::REAL)m_width, 60.0f);
        g.DrawString(L"F P S   O V E R L A Y", -1, &titleFont, titleRect, &centerFmt, &titleBrush);

        // Subtitle: current binding status
        Font subFont(&ff, 14.0f, FontStyleRegular, UnitPixel);
        SolidBrush subBrush(Color(210, 170, 185, 220));
        std::wstring status = m_cfg->auto_connect
            ? L"Автоподключение: к активному приложению"
            : (m_cfg->target_process.empty()
                   ? L"Приложение для счётчика не выбрано — зайдите в настройки"
                   : (L"Привязано к: " + m_cfg->target_process));
        RectF subRect(0.0f, (Gdiplus::REAL)(m_height / 2 - 110), (Gdiplus::REAL)m_width, 30.0f);
        g.DrawString(status.c_str(), -1, &subFont, subRect, &centerFmt, &subBrush);

        DrawButton(g, m_btnStart, L"Запустить", m_hoverAmounts[0], m_pressAmounts[0], ff);
        DrawButton(g, m_btnSettings, L"Настройки", m_hoverAmounts[1], m_pressAmounts[1], ff);
        DrawButton(g, m_btnExit, L"Выход", m_hoverAmounts[2], m_pressAmounts[2], ff);

        // Footer hint
        Font footFont(&ff, 12.0f, FontStyleRegular, UnitPixel);
        SolidBrush footBrush(Color(130, 150, 160, 190));
        StringFormat footFmt;
        footFmt.SetAlignment(StringAlignmentCenter);
        RectF footRect(0.0f, (Gdiplus::REAL)(m_height - 40), (Gdiplus::REAL)m_width, 24.0f);
        g.DrawString(L"Insert — показать/скрыть оверлей   ·   End — клик-сквозь", -1,
                      &footFont, footRect, &footFmt, &footBrush);
    }

    BitBlt(hdc, 0, 0, m_width, m_height, m_paintDC, 0, 0, SRCCOPY);
}

void LauncherWindow::BeginFadeIn() {
    if (!m_hwnd) return;
    m_fadingOut = false;
    m_hideAfterFade = false;
    m_fadingIn = true;
    m_fadeStartMs = GetTickCount64();
    m_lastAnimTickMs = m_fadeStartMs;
    m_fadeStartAlpha = 0;
    SetLayeredWindowAttributes(m_hwnd, 0, 0, LWA_ALPHA);
    SetTimer(m_hwnd, kAnimTimerId, 16, nullptr);
}

void LauncherWindow::BeginFadeOut(bool hideToTray) {
    if (!m_hwnd || m_fadingOut) return;
    const ULONGLONG now = GetTickCount64();
    m_fadeStartAlpha = m_fadingIn
        ? (BYTE)(255.0f * (std::min)(1.0f, (float)(now - m_fadeStartMs) / 240.0f))
        : 255;
    m_fadingIn = false;
    m_fadingOut = true;
    m_hideAfterFade = hideToTray;
    m_fadeStartMs = now;
    m_lastAnimTickMs = now;
    SetTimer(m_hwnd, kAnimTimerId, 16, nullptr);
}

void LauncherWindow::OpenSettings() {
    ShowSettingsWindow(m_hInst, m_hwnd, *m_cfg, m_cfgPath);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void LauncherWindow::AddTrayIcon() {
    if (!m_hwnd || m_trayAdded) return;
    NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid); nid.hWnd = m_hwnd; nid.uID = 2;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; nid.uCallbackMessage = WM_LAUNCHER_TRAY;
    nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    lstrcpynW(nid.szTip, L"FPS Overlay launcher", (int)(sizeof(nid.szTip) / sizeof(nid.szTip[0])));
    m_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void LauncherWindow::RemoveTrayIcon() {
    if (!m_hwnd || !m_trayAdded) return;
    NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid); nid.hWnd = m_hwnd; nid.uID = 2;
    Shell_NotifyIconW(NIM_DELETE, &nid); m_trayAdded = false;
}

LRESULT LauncherWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        Layout();
        return 0;
    case WM_TIMER:
        if (wParam == kAnimTimerId) {
            const ULONGLONG nowMs = GetTickCount64();
            float frameSeconds = m_lastAnimTickMs == 0
                ? 1.0f / (float)(std::max)(1, (std::min)(240, m_cfg->launcher_fps))
                : (float)(nowMs - m_lastAnimTickMs) / 1000.0f;
            m_lastAnimTickMs = nowMs;
            frameSeconds = (std::max)(0.0f, (std::min)(0.1f, frameSeconds));

            if (m_fadingIn) {
                const float progress = (std::min)(1.0f, (float)(nowMs - m_fadeStartMs) / 240.0f);
                const BYTE alpha = (BYTE)(255.0f * progress);
                SetLayeredWindowAttributes(m_hwnd, 0, alpha, LWA_ALPHA);
                if (progress >= 1.0f) {
                    m_fadingIn = false;
                    SetTimer(m_hwnd, kAnimTimerId,
                             (UINT)(1000 / (std::max)(1, (std::min)(240, m_cfg->launcher_fps))), nullptr);
                }
            }
            if (m_fadingOut) {
                const float progress = (std::min)(1.0f, (float)(nowMs - m_fadeStartMs) / 200.0f);
                const BYTE alpha = (BYTE)(m_fadeStartAlpha * (1.0f - progress));
                SetLayeredWindowAttributes(m_hwnd, 0, alpha, LWA_ALPHA);
                if (progress >= 1.0f) {
                    m_fadingOut = false;
                    if (m_hideAfterFade) {
                        KillTimer(m_hwnd, kAnimTimerId);
                        ShowWindow(m_hwnd, SW_HIDE);
                        AddTrayIcon();
                    } else {
                        RemoveTrayIcon();
                        DestroyWindow(m_hwnd);
                    }
                    return 0;
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            bool hoverChanging = false;
            for (int i = 0; i < 3; ++i) {
                const float target = (m_hoverBtn == i) ? 1.0f : 0.0f;
                const float old = m_hoverAmounts[i];
                const float blend = (std::min)(1.0f, frameSeconds * 14.0f);
                m_hoverAmounts[i] += (target - m_hoverAmounts[i]) * blend;
                if (std::fabs(m_hoverAmounts[i] - target) < 0.01f) m_hoverAmounts[i] = target;
                if (std::fabs(m_hoverAmounts[i] - old) > 0.002f) hoverChanging = true;

                const float pressTarget = (m_pressedBtn == i) ? 1.0f : 0.0f;
                const float oldPress = m_pressAmounts[i];
                const float pressBlend = (std::min)(1.0f, frameSeconds * 24.0f);
                m_pressAmounts[i] += (pressTarget - m_pressAmounts[i]) * pressBlend;
                if (std::fabs(m_pressAmounts[i] - pressTarget) < 0.01f) m_pressAmounts[i] = pressTarget;
                if (std::fabs(m_pressAmounts[i] - oldPress) > 0.002f) hoverChanging = true;
            }

            if (GetForegroundWindow() == m_hwnd) {
                m_bg.Update(frameSeconds);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            } else {
                // Lower redraw pressure while the launcher is in the background.
                static unsigned idleTick = 0;
                if ((++idleTick % 8) == 0) {
                    m_bg.Update(frameSeconds * 8.0f);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
            if (hoverChanging) InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int hit = HitTestButton(x, y);
        if (hit != m_hoverBtn) {
            m_hoverBtn = hit;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        m_pressedBtn = HitTestButton(x, y);
        if (m_pressedBtn >= 0) SetCapture(m_hwnd);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int hit = HitTestButton(x, y);
        m_pressedBtn = -1;
        if (GetCapture() == m_hwnd) ReleaseCapture();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        if (hit == 0) {
            m_result = LauncherAction::Start;
            RemoveTrayIcon();
            BeginFadeOut(false);
        } else if (hit == 1) {
            OpenSettings();
        } else if (hit == 2) {
            m_result = LauncherAction::Exit;
            RemoveTrayIcon();
            BeginFadeOut(false);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            m_result = LauncherAction::Start;
            BeginFadeOut(false);
        } else if (wParam == VK_ESCAPE) {
            m_result = LauncherAction::Exit;
            RemoveTrayIcon();
            BeginFadeOut(false);
        }
        return 0;
    case WM_CLOSE:
        BeginFadeOut(true);
        return 0;
    case WM_LAUNCHER_TRAY:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(m_hwnd, SW_SHOW); SetForegroundWindow(m_hwnd); RemoveTrayIcon();
            SetTimer(m_hwnd, kAnimTimerId, (UINT)(1000 / (std::max)(1, (std::min)(240, m_cfg->launcher_fps))), nullptr);
            BeginFadeIn();
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt{}; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_LAUNCHER_TRAY_SHOW, L"Открыть лаунчер");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_LAUNCHER_TRAY_EXIT, L"Выход");
            SetForegroundWindow(m_hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_LAUNCHER_TRAY_SHOW) {
            ShowWindow(m_hwnd, SW_SHOW); SetForegroundWindow(m_hwnd); RemoveTrayIcon();
            SetTimer(m_hwnd, kAnimTimerId, (UINT)(1000 / (std::max)(1, (std::min)(240, m_cfg->launcher_fps))), nullptr);
            BeginFadeIn(); return 0;
        }
        if (LOWORD(wParam) == ID_LAUNCHER_TRAY_EXIT) {
            m_result = LauncherAction::Exit; RemoveTrayIcon(); DestroyWindow(m_hwnd); return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        m_pressedBtn = -1;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1; // avoid flicker - WM_PAINT fully repaints every pixel
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        Paint(hdc);
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        RemoveTrayIcon();
        KillTimer(m_hwnd, kAnimTimerId);
        if (m_paintDC) {
            if (m_paintOriginalBitmap) SelectObject(m_paintDC, m_paintOriginalBitmap);
            if (m_paintBitmap) DeleteObject(m_paintBitmap);
            DeleteDC(m_paintDC);
            m_paintDC = nullptr;
            m_paintBitmap = nullptr;
            m_paintOriginalBitmap = nullptr;
            m_paintWidth = m_paintHeight = 0;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

LauncherAction LauncherWindow::Run(HINSTANCE hInst, AppConfig& cfg, const std::wstring& cfgPath) {
    m_cfg = &cfg;
    m_cfgPath = cfgPath;
    m_hInst = hInst;
    g_instance = this;

    GdiplusStartupInput gdiInput;
    GdiplusStartup(&m_gdiplusToken, &gdiInput, nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    m_bg.Init(screenW, screenH);

    m_hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_LAYERED, kClassName, L"FPS Overlay",
                              WS_POPUP, 0, 0, screenW, screenH,
                              nullptr, nullptr, hInst, nullptr);
    if (!m_hwnd) {
        m_bg.ReleaseResources();
        GdiplusShutdown(m_gdiplusToken);
        return LauncherAction::Exit;
    }

    // Done here (after m_hwnd is actually assigned) rather than in a
    // WM_CREATE handler: WM_CREATE fires synchronously *inside*
    // CreateWindowExW, before it has returned and before m_hwnd is set -
    // calling Layout()/SetTimer(m_hwnd, ...) from there would run against
    // a still-null handle and silently corrupt the first frame.
    Layout();
    SetTimer(m_hwnd, kAnimTimerId, 16, nullptr);

    SetLayeredWindowAttributes(m_hwnd, 0, 0, LWA_ALPHA);
    m_fadingIn = true;
    m_fadeStartMs = GetTickCount64();
    m_lastAnimTickMs = m_fadeStartMs;
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    m_bg.ReleaseResources();
    GdiplusShutdown(m_gdiplusToken);
    g_instance = nullptr;
    return m_result;
}
