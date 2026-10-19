#include "overlay_window.h"
#include <gdiplus.h>
#include <algorithm>
#include <shellapi.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

// WndProc is a plain C callback, so we need a way back to the instance.
// One overlay window per process is assumed, which is the normal case here.
static OverlayWindow* g_instance = nullptr;
static constexpr UINT WM_TRAYICON = WM_APP + 42;
static constexpr UINT ID_TRAY_SHOW = 41001;
static constexpr UINT ID_TRAY_EXIT = 41002;

bool OverlayWindow::Create(HINSTANCE hInst, AppConfig& cfg, const std::wstring& cfgPath) {
    m_cfg = &cfg;
    m_cfgPath = cfgPath;
    g_instance = this;

    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiInput, nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"FpsOverlayWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    if (cfg.click_through) exStyle |= WS_EX_TRANSPARENT;

    m_hwnd = CreateWindowExW(exStyle, wc.lpszClassName, L"FPS Overlay",
                              WS_POPUP, cfg.pos_x, cfg.pos_y, 260, 100,
                              nullptr, nullptr, hInst, nullptr);
    if (!m_hwnd) return false;

    // Decode images once; rendering only composites the cached bitmaps.
    auto loadImage = [&](const std::wstring& configuredPath) -> Gdiplus::Image* {
        if (configuredPath.empty()) return nullptr;
        std::wstring imagePath = configuredPath;
        if (imagePath.find(L":\\") == std::wstring::npos && imagePath[0] != L'\\' && imagePath[0] != L'/') {
            const size_t slash = cfgPath.find_last_of(L"\\/");
            if (slash != std::wstring::npos) imagePath = cfgPath.substr(0, slash + 1) + imagePath;
        }
        Gdiplus::Image* image = new Gdiplus::Image(imagePath.c_str());
        if (image->GetLastStatus() != Gdiplus::Ok) { delete image; return nullptr; }
        return image;
    };
    m_backgroundImage = loadImage(cfg.overlay_image_path);
    m_backgroundImage2 = loadImage(cfg.overlay_image_path2);

    RegisterHotKey(m_hwnd, 1, 0, cfg.hotkey_toggle_visible);
    RegisterHotKey(m_hwnd, 2, 0, cfg.hotkey_toggle_clickthrough);
    RegisterHotKey(m_hwnd, 3, MOD_CONTROL | MOD_ALT, cfg.hotkey_cycle_profile);

    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

void OverlayWindow::SetLines(const std::vector<std::wstring>& lines) {
    m_lines = lines;
}

void OverlayWindow::ApplyExStyle() {
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (m_cfg->click_through) ex |= WS_EX_TRANSPARENT;
    else ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex);
}

void OverlayWindow::ToggleVisible() {
    m_visible = !m_visible;
    ShowWindow(m_hwnd, m_visible ? SW_SHOW : SW_HIDE);
    if (m_visible) RemoveTrayIcon(); else AddTrayIcon();
}

void OverlayWindow::AddTrayIcon() {
    if (!m_hwnd || m_trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid); nid.hWnd = m_hwnd; nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    lstrcpynW(nid.szTip, L"FPS Overlay - running in background", (int)(sizeof(nid.szTip) / sizeof(nid.szTip[0])));
    m_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void OverlayWindow::RemoveTrayIcon() {
    if (!m_hwnd || !m_trayAdded) return;
    NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid); nid.hWnd = m_hwnd; nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid); m_trayAdded = false;
}

void OverlayWindow::ToggleClickThrough() {
    m_cfg->click_through = !m_cfg->click_through;
    ApplyExStyle();
}

void OverlayWindow::Render() {
    if (!m_hwnd || m_lines.empty()) return;
    // Avoid unnecessary full-surface allocations when called more often than the configured cap.
    ULONGLONG nowMs = GetTickCount64();
    int cap = (std::max)(1, (std::min)(240, m_cfg->overlay_fps));
    ULONGLONG minFrameMs = (ULONGLONG)(1000 / cap);
    if (m_lastRenderMs && nowMs - m_lastRenderMs < minFrameMs) return;
    m_lastRenderMs = nowMs;

    const int padding = 10;
    const int lineHeight = m_cfg->font_size + 6;
    int width = 300;
    for (const auto& line : m_lines) width = (std::max)(width, (int)line.size() * (m_cfg->font_size * 3 / 5) + padding * 2);
    width = (std::min)(560, width);
    const int height = padding * 2 + lineHeight * (int)m_lines.size();

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hBmp);

    if (bits) {
        Gdiplus::Bitmap gdiBmp(width, height, width * 4,
                                PixelFormat32bppPARGB, (BYTE*)bits);
        Gdiplus::Graphics g(&gdiBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        BYTE bgA = (BYTE)m_cfg->opacity;
        BYTE bgR = (BYTE)((m_cfg->bg_color >> 16) & 0xFF);
        BYTE bgG = (BYTE)((m_cfg->bg_color >> 8) & 0xFF);
        BYTE bgB = (BYTE)(m_cfg->bg_color & 0xFF);

        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(bgA, bgR, bgG, bgB));
        Gdiplus::GraphicsPath path;
        const int radius = 8;
        Gdiplus::Rect rect(0, 0, width, height);
        path.AddArc(rect.X, rect.Y, radius * 2, radius * 2, 180, 90);
        path.AddArc(rect.X + rect.Width - radius * 2, rect.Y, radius * 2, radius * 2, 270, 90);
        path.AddArc(rect.X + rect.Width - radius * 2, rect.Y + rect.Height - radius * 2, radius * 2, radius * 2, 0, 90);
        path.AddArc(rect.X, rect.Y + rect.Height - radius * 2, radius * 2, radius * 2, 90, 90);
        path.CloseFigure();
        g.FillPath(&bgBrush, &path);

        // Optional timed rotation between two user-selected overlay images.
        Gdiplus::Image* activeImage = m_backgroundImage;
        if (m_backgroundImage2 && m_cfg->image_rotate_seconds > 0 &&
            ((GetTickCount64() / 1000ULL / (ULONGLONG)m_cfg->image_rotate_seconds) % 2ULL) == 1ULL)
            activeImage = m_backgroundImage2;
        // Crop-to-fill user image with configurable opacity, clipped to the rounded HUD panel.
        if (activeImage && activeImage->GetWidth() && activeImage->GetHeight() && m_cfg->image_opacity > 0) {
            const double sw = (double)activeImage->GetWidth();
            const double sh = (double)activeImage->GetHeight();
            const double targetRatio = (double)width / (double)height;
            const double sourceRatio = sw / sh;
            Gdiplus::REAL sx = 0, sy = 0, sWidth = (Gdiplus::REAL)sw, sHeight = (Gdiplus::REAL)sh;
            if (sourceRatio > targetRatio) { sWidth = (Gdiplus::REAL)(sh * targetRatio); sx = (Gdiplus::REAL)((sw - sWidth) / 2.0); }
            else { sHeight = (Gdiplus::REAL)(sw / targetRatio); sy = (Gdiplus::REAL)((sh - sHeight) / 2.0); }
            Gdiplus::ImageAttributes attrs;
            Gdiplus::ColorMatrix matrix{};
            matrix.m[0][0] = matrix.m[1][1] = matrix.m[2][2] = matrix.m[3][3] = 1.0f;
            matrix.m[4][4] = 1.0f;
            matrix.m[3][3] = (Gdiplus::REAL)m_cfg->image_opacity / 100.0f;
            attrs.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
            g.SetClip(&path);
            g.DrawImage(activeImage, Gdiplus::Rect(0, 0, width, height), sx, sy, sWidth, sHeight, Gdiplus::UnitPixel, &attrs);
            g.ResetClip();
        }

        BYTE txR = (BYTE)((m_cfg->text_color >> 16) & 0xFF);
        BYTE txG = (BYTE)((m_cfg->text_color >> 8) & 0xFF);
        BYTE txB = (BYTE)(m_cfg->text_color & 0xFF);

        Gdiplus::FontFamily fontFamily(m_cfg->font_name.c_str());
        Gdiplus::Font font(&fontFamily, (Gdiplus::REAL)m_cfg->font_size,
                            Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        int y = padding;
        for (const auto& line : m_lines) {
            Gdiplus::Color lineColor(255, txR, txG, txB);
            if (line.find(L"FPS:") == 0) {
                double fps = _wtof(line.c_str() + 4);
                if (fps < m_cfg->warning_fps) lineColor = Gdiplus::Color(255, 255, 85, 105);
            } else if (line.find(L"CPU Temp:") == 0 || line.find(L"GPU Temp:") == 0) {
                double temp = _wtof(line.c_str() + line.find(L":") + 1);
                if (temp >= m_cfg->warning_temp_c) lineColor = Gdiplus::Color(255, 255, 150, 60);
            } else if (line.find(L"RAM:") == 0) {
                size_t p = line.find(L"(");
                if (p != std::wstring::npos && _wtoi(line.c_str() + p + 1) >= m_cfg->warning_ram_percent)
                    lineColor = Gdiplus::Color(255, 255, 85, 105);
            }
            Gdiplus::SolidBrush textBrush(lineColor);
            g.DrawString(line.c_str(), -1, &font,
                          Gdiplus::PointF((Gdiplus::REAL)padding, (Gdiplus::REAL)y), &textBrush);
            y += lineHeight;
        }
        // Thin neon cyberpunk frame (cyan + purple accent).
        Gdiplus::Pen neonBlue(Gdiplus::Color(220, 30, 210, 255), 1.4f);
        Gdiplus::Rect frame(1, 1, width - 3, height - 3);
        Gdiplus::GraphicsPath framePath;
        framePath.AddArc(frame.X, frame.Y, 14, 14, 180, 90);
        framePath.AddArc(frame.X + frame.Width - 14, frame.Y, 14, 14, 270, 90);
        framePath.AddArc(frame.X + frame.Width - 14, frame.Y + frame.Height - 14, 14, 14, 0, 90);
        framePath.AddArc(frame.X, frame.Y + frame.Height - 14, 14, 14, 90, 90);
        framePath.CloseFigure();
        g.DrawPath(&neonBlue, &framePath);
        Gdiplus::Pen accent(Gdiplus::Color(180, 190, 70, 255), 1.0f);
        g.DrawLine(&accent, 18, height - 4, (std::min)(width - 18, 58), height - 4);
    }

    POINT ptSrc = {0, 0};
    POINT ptDst = {m_cfg->pos_x, m_cfg->pos_y};
    SIZE sz = {width, height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    SetWindowPos(m_hwnd, HWND_TOPMOST, m_cfg->pos_x, m_cfg->pos_y, width, height,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    UpdateLayeredWindow(m_hwnd, screenDC, &ptDst, &sz, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_instance) {
        g_instance->m_hwnd = hwnd; // set before CreateWindowExW returns, see launcher_window.cpp
        return g_instance->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        m_dragging = true;
        SetCapture(m_hwnd);
        POINT pt; GetCursorPos(&pt);
        m_dragOffset.x = pt.x - m_cfg->pos_x;
        m_dragOffset.y = pt.y - m_cfg->pos_y;
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (m_dragging) {
            POINT pt; GetCursorPos(&pt);
            m_cfg->pos_x = pt.x - m_dragOffset.x;
            m_cfg->pos_y = pt.y - m_dragOffset.y;
            Render();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (m_dragging) {
            m_dragging = false;
            ReleaseCapture();
        }
        return 0;
    }
    case WM_HOTKEY: {
        if (wParam == 1) ToggleVisible();
        else if (wParam == 2) ToggleClickThrough();
        else if (wParam == 3) {
            if (_wcsicmp(m_cfg->profile.c_str(), L"Minimum") == 0) m_cfg->profile = L"Game";
            else if (_wcsicmp(m_cfg->profile.c_str(), L"Game") == 0) m_cfg->profile = L"Full";
            else m_cfg->profile = L"Minimum";
            m_cfg->Save(m_cfgPath);
            if (OnTick) OnTick();
        }
        return 0;
    }
    case WM_TIMER:
        if (OnTick) OnTick();
        return 0;
    case WM_CLOSE:
        m_visible = false;
        KillTimer(m_hwnd, 1);
        ShowWindow(m_hwnd, SW_HIDE);
        AddTrayIcon();
        return 0;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            m_visible = true; ShowWindow(m_hwnd, SW_SHOW); SetForegroundWindow(m_hwnd); RemoveTrayIcon();
            SetTimer(m_hwnd, 1, (UINT)(1000 / (std::max)(1, (std::min)(240, m_cfg->overlay_fps))), nullptr);
            if (OnTick) OnTick();
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt{}; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"Показать оверлей");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Выход");
            SetForegroundWindow(m_hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_SHOW) {
            m_visible = true; ShowWindow(m_hwnd, SW_SHOW); SetForegroundWindow(m_hwnd); RemoveTrayIcon();
            SetTimer(m_hwnd, 1, (UINT)(1000 / (std::max)(1, (std::min)(240, m_cfg->overlay_fps))), nullptr);
            if (OnTick) OnTick(); return 0;
        }
        if (LOWORD(wParam) == ID_TRAY_EXIT) { RemoveTrayIcon(); DestroyWindow(m_hwnd); return 0; }
        break;
    case WM_DESTROY:
        RemoveTrayIcon();
        UnregisterHotKey(m_hwnd, 1);
        UnregisterHotKey(m_hwnd, 2);
        UnregisterHotKey(m_hwnd, 3);
        if (m_backgroundImage) { delete m_backgroundImage; m_backgroundImage = nullptr; }
        if (m_backgroundImage2) { delete m_backgroundImage2; m_backgroundImage2 = nullptr; }
        if (m_gdiplusToken) { Gdiplus::GdiplusShutdown(m_gdiplusToken); m_gdiplusToken = 0; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
