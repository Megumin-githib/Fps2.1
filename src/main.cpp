#include <windows.h>
#include <vector>
#include <string>
#include "config.h"
#include "cpu_monitor.h"
#include "gpu_monitor.h"
#include "fps_monitor.h"
#include "overlay_window.h"
#include "session_monitor.h"
#include "launcher_window.h"
#include "process_utils.h"
#include "network_monitor.h"
#include <algorithm>

namespace {

std::wstring GetForegroundProcessName() {
    HWND fg = GetForegroundWindow();
    if (!fg) return L"";

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) return L"";

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";

    wchar_t buf[MAX_PATH];
    DWORD size = MAX_PATH;
    std::wstring name;
    if (QueryFullProcessImageNameW(h, 0, buf, &size)) {
        std::wstring full(buf);
        auto pos = full.find_last_of(L"\\/");
        name = (pos == std::wstring::npos) ? full : full.substr(pos + 1);
    }
    CloseHandle(h);
    return name;
}

// Runs the translucent HUD overlay until the user closes it. This is the
// same overlay the project started with, now also showing a "Session"
// line (how long the bound app has been open) and driven by the explicit
// auto_connect / target_process choice made on the Settings screen instead
// of an implicit "auto" sentinel string.
void RunOverlay(HINSTANCE hInst, AppConfig& cfg, const std::wstring& cfgPath) {
    CpuMonitor cpu;
    cpu.Init();

    GpuMonitor gpu;
    bool gpuOk = gpu.Init();

    FpsMonitor fps;
    SessionMonitor session;
    NetworkMonitor network(cfg.ping_host);

    std::wstring presentmonPath = cfg.presentmon_path;
    if (presentmonPath.find(L":\\") == std::wstring::npos && (presentmonPath.empty() || (presentmonPath[0] != L'\\' && presentmonPath[0] != L'/'))) {
        const size_t slash = cfgPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) presentmonPath = cfgPath.substr(0, slash + 1) + presentmonPath;
    }
    std::wstring currentTarget = cfg.auto_connect ? GetForegroundProcessName() : cfg.target_process;
    if (!currentTarget.empty()) {
        auto savedGame = cfg.per_game.find(currentTarget);
        if (savedGame != cfg.per_game.end()) {
            cfg.pos_x = savedGame->second.pos_x; cfg.pos_y = savedGame->second.pos_y; cfg.profile = savedGame->second.profile;
        }
        fps.Start(presentmonPath, cfg.presentmon_args, currentTarget);
        session.AttachOrUpdate(currentTarget);
    }

    OverlayWindow win;
    if (!win.Create(hInst, cfg, cfgPath)) return;

    int tickCount = 0;
    ULONGLONG lastHardwareSample = 0, lastSensorSample = 0, lastFpsSample = 0;
    double cpuUsageCached = 0.0, gpuUsageCached = 0.0, gpuVramCachedMB = 0.0, cpuClockCachedMHz = 0.0;
    double cpuTempCached = 0.0, gpuTempCached = 0.0;
    bool cpuTempValid = false, gpuTempValid = false;
    FpsStats fpsCached{};

    win.OnTick = [&]() {
        // In auto-connect mode, re-check the foreground app periodically and
        // retarget both FPS capture and the session counter if it changed
        // (e.g. alt-tabbed into a different game).
        if (cfg.auto_connect && (++tickCount % 4 == 0)) {
            std::wstring fg = GetForegroundProcessName();
            if (!fg.empty() && fg != currentTarget) {
                if (!currentTarget.empty()) {
                    GameSettings& oldGame = cfg.per_game[currentTarget];
                    oldGame.pos_x = cfg.pos_x; oldGame.pos_y = cfg.pos_y; oldGame.profile = cfg.profile;
                }
                currentTarget = fg;
                auto found = cfg.per_game.find(currentTarget);
                if (found != cfg.per_game.end()) {
                    cfg.pos_x = found->second.pos_x; cfg.pos_y = found->second.pos_y; cfg.profile = found->second.profile;
                } else {
                    GameSettings fresh; fresh.pos_x = cfg.pos_x; fresh.pos_y = cfg.pos_y; fresh.profile = cfg.profile;
                    cfg.per_game[currentTarget] = fresh;
                }
                fps.Start(presentmonPath, cfg.presentmon_args, currentTarget);
                cfg.Save(cfgPath);
            }
        }
        if (!currentTarget.empty()) {
            session.AttachOrUpdate(currentTarget);
            GameSettings& activeGame = cfg.per_game[currentTarget];
            activeGame.pos_x = cfg.pos_x; activeGame.pos_y = cfg.pos_y; activeGame.profile = cfg.profile;
        }

        network.Update();
        const ULONGLONG nowMs = GetTickCount64();
        // Expensive counters and WMI thermal queries are sampled independently
        // from rendering, so a 240 FPS HUD does not issue 240 hardware queries/sec.
        if (nowMs - lastHardwareSample >= 250 || lastHardwareSample == 0) {
            cpuUsageCached = cpu.GetUsagePercent();
            cpuClockCachedMHz = cpu.GetFrequencyMHz();
            if (gpuOk) {
                gpuUsageCached = gpu.GetUsagePercent();
                gpuVramCachedMB = gpu.GetVramUsedBytes() / (1024.0 * 1024.0);
            }
            lastHardwareSample = nowMs;
        }
        if (nowMs - lastSensorSample >= 1500 || lastSensorSample == 0) {
            double sample = 0.0;
            gpuTempValid = gpuOk && gpu.GetTemperatureC(sample);
            if (gpuTempValid) gpuTempCached = sample;
            cpuTempValid = GetCpuTemperatureC(sample);
            if (cpuTempValid) cpuTempCached = sample;
            lastSensorSample = nowMs;
        }
        if (nowMs - lastFpsSample >= 100 || lastFpsSample == 0) {
            fpsCached = fps.GetStats();
            lastFpsSample = nowMs;
        }
        std::vector<std::wstring> lines;
        wchar_t buf[160];
        const bool minimumProfile = _wcsicmp(cfg.profile.c_str(), L"Minimum") == 0;
        const bool gameProfile = _wcsicmp(cfg.profile.c_str(), L"Game") == 0;
        const bool fullProfile = !minimumProfile && !gameProfile;
        const bool showFps = minimumProfile || gameProfile || (fullProfile && cfg.show_fps);
        const bool showPing = minimumProfile || gameProfile || (fullProfile && cfg.show_ping);
        const bool showNetwork = gameProfile || (fullProfile && cfg.show_network);
        const bool showRam = gameProfile || (fullProfile && cfg.show_ram);

        if (fullProfile && cfg.show_time) {
            SYSTEMTIME st; GetLocalTime(&st);
            swprintf_s(buf, L"Time: %02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
            lines.push_back(buf);
        }
        if (fullProfile && cfg.show_session_time) {
            swprintf_s(buf, L"Session: %s", session.GetElapsedString().c_str());
            lines.push_back(buf);
        }

        const FpsStats& fs = fpsCached;
        if (showFps) {
            swprintf_s(buf, L"FPS: %.0f", fs.valid ? fs.fps_avg : 0.0);
            lines.push_back(buf);
        }
        if (fullProfile && cfg.show_1_low) {
            swprintf_s(buf, L"1%% Low: %.0f", fs.valid ? fs.fps_1low : 0.0);
            lines.push_back(buf);
        }
        if (fullProfile && cfg.show_01_low) {
            swprintf_s(buf, L"0.1%% Low: %.0f", fs.valid ? fs.fps_01low : 0.0);
            lines.push_back(buf);
        }
        if (fullProfile && cfg.show_cpu) {
            swprintf_s(buf, L"CPU: %.0f%%", cpuUsageCached);
            lines.push_back(buf);
        }
        if (fullProfile && cfg.show_gpu && gpuOk) {
            swprintf_s(buf, L"GPU: %.0f%%  VRAM: %.0f MB", gpuUsageCached, gpuVramCachedMB);
            lines.push_back(buf);
        }
        if (showPing) {
            const auto& ns = network.Stats();
            if (ns.ping_valid) swprintf_s(buf, L"Ping: %.0f ms", ns.ping_ms);
            else swprintf_s(buf, L"Ping: N/A (%s)", cfg.ping_host.c_str());
            lines.push_back(buf);
        }
        if (showNetwork) {
            const auto& ns = network.Stats();
            swprintf_s(buf, L"NET  ↓ %.2f Mbps  ↑ %.2f Mbps", ns.download_mbps, ns.upload_mbps);
            lines.push_back(buf);
        }
        if (showRam) {
            MEMORYSTATUSEX mem{}; mem.dwLength = sizeof(mem);
            if (GlobalMemoryStatusEx(&mem)) {
                swprintf_s(buf, L"RAM: %.1f / %.1f GB (%u%%)",
                    (double)(mem.ullTotalPhys - mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0),
                    (double)mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0), mem.dwMemoryLoad);
                lines.push_back(buf);
            }
        }
        if (fullProfile && cfg.show_frequencies) {
            double cpuMhz = cpuClockCachedMHz;
            if (cpuMhz > 0) swprintf_s(buf, L"CPU Clock: %.0f MHz", cpuMhz);
            else swprintf_s(buf, L"CPU Clock: N/A");
            lines.push_back(buf);
            // Vendor GPU clock APIs are not universally exposed by Windows; show N/A without a vendor SDK.
            lines.push_back(L"GPU Clock: N/A");
        }
        if (fullProfile && cfg.show_temps) {
            if (gpuTempValid) {
                swprintf_s(buf, L"GPU Temp: %.0f C", gpuTempCached);
            } else {
                swprintf_s(buf, L"GPU Temp: N/A");
            }
            lines.push_back(buf);

            if (cpuTempValid) {
                swprintf_s(buf, L"CPU Temp: %.0f C", cpuTempCached);
            } else {
                swprintf_s(buf, L"CPU Temp: N/A");
            }
            lines.push_back(buf);
        }

        lines.insert(lines.begin(), L"PROFILE: " + cfg.profile + L"  (Ctrl+Alt+F10)");
        win.SetLines(lines);
        win.Render();
    };

    const int overlayFps = (std::max)(1, (std::min)(240, cfg.overlay_fps));
    SetTimer(win.Handle(), 1, (UINT)(1000 / overlayFps), nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    cfg.Save(cfgPath);
    fps.Stop();
    gpu.Shutdown();
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring cfgPath(exePath);
    const auto slash = cfgPath.find_last_of(L"\\/");
    cfgPath = (slash == std::wstring::npos ? std::wstring(L"") : cfgPath.substr(0, slash + 1)) + L"config.ini";
    AppConfig cfg = AppConfig::LoadOrCreate(cfgPath);

    // The exe always opens into the full-screen "game menu" first, unless
    // the user turned that off in Settings - matches the requested
    // "launches like a game" behavior. From there, Start enters the actual
    // overlay and Settings is reachable at any time without leaving the exe.
    bool wantOverlay = !cfg.show_launcher_on_start;

    if (cfg.show_launcher_on_start) {
        LauncherWindow launcher;
        LauncherAction action = launcher.Run(hInst, cfg, cfgPath);
        cfg.Save(cfgPath);
        wantOverlay = (action == LauncherAction::Start);
    }

    if (wantOverlay) {
        RunOverlay(hInst, cfg, cfgPath);
    }

    CoUninitialize();
    return 0;
}
