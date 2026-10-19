#include <windows.h>
#include <objbase.h>
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
#include <atomic>
#include <mutex>
#include <thread>

namespace {

struct HardwareSnapshot {
    double cpuUsage = 0.0;
    double gpuUsage = 0.0;
    double gpuVramMB = 0.0;
    double cpuClockMHz = 0.0;
    double cpuTempC = 0.0;
    double gpuTempC = 0.0;
    bool gpuAvailable = false;
    bool cpuTempValid = false;
    bool gpuTempValid = false;
};

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

    std::atomic<bool> stopHardware{ false };
    std::mutex hardwareMutex;
    HardwareSnapshot hardwareSnapshot{};
    std::thread hardwareThread([&]() {
        // Keep PDH, DXGI/NVAPI and WMI calls away from the window/message thread.
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool ownsCom = SUCCEEDED(comResult);

        CpuMonitor cpu;
        cpu.Init();
        GpuMonitor gpu;
        const bool gpuOk = gpu.Init();

        HardwareSnapshot sample{};
        sample.gpuAvailable = gpuOk;
        ULONGLONG lastUsageSample = 0;
        ULONGLONG lastTemperatureSample = GetTickCount64();

        while (!stopHardware.load(std::memory_order_relaxed)) {
            const ULONGLONG now = GetTickCount64();
            if (lastUsageSample == 0 || now - lastUsageSample >= 250) {
                sample.cpuUsage = cpu.GetUsagePercent();
                sample.cpuClockMHz = cpu.GetFrequencyMHz();
                if (gpuOk) {
                    sample.gpuUsage = gpu.GetUsagePercent();
                    sample.gpuVramMB = gpu.GetVramUsedBytes() / (1024.0 * 1024.0);
                }
                lastUsageSample = now;
            }
            if (lastTemperatureSample == 0 || now - lastTemperatureSample >= 1500) {
                double temperature = 0.0;
                sample.gpuTempValid = gpuOk && gpu.GetTemperatureC(temperature);
                if (sample.gpuTempValid) sample.gpuTempC = temperature;
                sample.cpuTempValid = GetCpuTemperatureC(temperature);
                if (sample.cpuTempValid) sample.cpuTempC = temperature;
                lastTemperatureSample = GetTickCount64();
            }
            {
                std::lock_guard<std::mutex> lock(hardwareMutex);
                hardwareSnapshot = sample;
            }
            for (int i = 0; i < 10 && !stopHardware.load(std::memory_order_relaxed); ++i)
                Sleep(10);
        }

        gpu.Shutdown();
        if (ownsCom) CoUninitialize();
    });

    ULONGLONG lastFpsSample = 0;
    ULONGLONG lastForegroundCheck = 0;
    FpsStats fpsCached{};

    win.OnTick = [&]() {
        const ULONGLONG nowMs = GetTickCount64();
        // Throttle discovery by wall-clock time; render FPS must not decide
        // how often the app opens and queries foreground process handles.
        if (cfg.auto_connect && (lastForegroundCheck == 0 || nowMs - lastForegroundCheck >= 400)) {
            lastForegroundCheck = nowMs;
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

        HardwareSnapshot hw{};
        {
            std::lock_guard<std::mutex> lock(hardwareMutex);
            hw = hardwareSnapshot;
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
            swprintf_s(buf, L"CPU: %.0f%%", hw.cpuUsage);
            lines.push_back(buf);
        }
        if (fullProfile && cfg.show_gpu && hw.gpuAvailable) {
            swprintf_s(buf, L"GPU: %.0f%%  VRAM: %.0f MB", hw.gpuUsage, hw.gpuVramMB);
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
            double cpuMhz = hw.cpuClockMHz;
            if (cpuMhz > 0) swprintf_s(buf, L"CPU Clock: %.0f MHz", cpuMhz);
            else swprintf_s(buf, L"CPU Clock: N/A");
            lines.push_back(buf);
            // Vendor GPU clock APIs are not universally exposed by Windows; show N/A without a vendor SDK.
            lines.push_back(L"GPU Clock: N/A");
        }
        if (fullProfile && cfg.show_temps) {
            if (hw.gpuTempValid) {
                swprintf_s(buf, L"GPU Temp: %.0f C", hw.gpuTempC);
            } else {
                swprintf_s(buf, L"GPU Temp: N/A");
            }
            lines.push_back(buf);

            if (hw.cpuTempValid) {
                swprintf_s(buf, L"CPU Temp: %.0f C", hw.cpuTempC);
            } else {
                swprintf_s(buf, L"CPU Temp: N/A");
            }
            lines.push_back(buf);
        }

        lines.insert(lines.begin(), L"PROFILE: " + cfg.profile + L"  (Ctrl+Alt+F10)");
        win.SetLines(lines);
        win.Render();
    };

    // The initial fade needs ~60 Hz even if the steady-state HUD update rate is lower.
    SetTimer(win.Handle(), 1, 16, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    stopHardware.store(true, std::memory_order_relaxed);
    if (hardwareThread.joinable()) hardwareThread.join();

    cfg.Save(cfgPath);
    fps.Stop();
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
