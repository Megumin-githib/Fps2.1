#pragma once
#include <string>
#include <map>

// Virtual-key codes used as defaults below (avoids pulling <windows.h> into
// every translation unit that includes this header):
//   VK_INSERT = 0x2D (45)
//   VK_END    = 0x23 (35)

struct GameSettings {
    int pos_x = 20;
    int pos_y = 20;
    std::wstring profile = L"Full";
};

struct AppConfig {
    // ---- Window / appearance ----
    int pos_x = 20;
    int pos_y = 20;
    int opacity = 220;                 // background alpha, 0-255
    int font_size = 16;                // pixels
    std::wstring font_name = L"Consolas";
    unsigned int text_color = 0x00FFFFFF; // 0x00RRGGBB
    unsigned int bg_color   = 0x00101010; // 0x00RRGGBB
    bool click_through = false;
    int update_interval_ms = 500;          // overlay metrics refresh interval
    int launcher_fps = 60;                 // independent launcher animation cap: 1..240
    int overlay_fps = 30;                  // independent overlay refresh cap: 1..240
    std::wstring overlay_image_path = L"assets_overlay.png";
    std::wstring overlay_image_path2 = L"";
    int image_rotate_seconds = 0;
    int image_opacity = 60;                // percent
    std::wstring profile = L"Full";       // Minimum / Game / Full
    std::wstring ping_host = L"1.1.1.1";
    std::map<std::wstring, GameSettings> per_game;
    unsigned int hotkey_cycle_profile = 0x79; // VK_F10; Ctrl+Alt modifiers are fixed

    // ---- Which metrics to show ----
    bool show_fps = true;
    bool show_1_low = true;
    bool show_01_low = true;
    bool show_time = true;
    bool show_cpu = true;
    bool show_gpu = true;
    bool show_temps = true;
    bool show_ping = true;
    bool show_network = true;
    bool show_ram = true;
    bool show_frequencies = true;
    int warning_fps = 30;
    int warning_temp_c = 85;
    int warning_ram_percent = 90;

    // ---- FPS source (PresentMon) ----
    // PresentMon's CLI flags differ between versions - check
    // `PresentMon.exe --help` for the build you downloaded and adjust
    // PresentMonArgs in config.ini if needed. {proc} is replaced with the
    // target process executable name (e.g. "game.exe").
    std::wstring presentmon_path = L"PresentMon.exe";
    std::wstring presentmon_args =
        L"-stop_existing_session -no_top -no_console_stats -output_stdout -process_name {proc}";

    // ---- What the counter is attached to ----
    // When auto_connect is true, the overlay follows whatever app currently
    // has focus (re-checked periodically) - no setup needed. When false, it
    // stays locked onto target_process (an exe name, e.g. "game.exe"),
    // picked from the Settings screen's running-process list or via
    // "Browse...". target_process is also used as the last-known value when
    // auto_connect is re-enabled later.
    bool auto_connect = true;
    std::wstring target_process = L"";

    // ---- Session ("how long has it been open") counter ----
    bool show_session_time = true;

    // ---- Launcher (the game-like fullscreen start menu) ----
    bool show_launcher_on_start = true; // if false, skip straight to the overlay
    bool run_at_startup = false;        // add/remove a HKCU...\Run entry

    // ---- Hotkeys (virtual-key codes) ----
    unsigned int hotkey_toggle_visible = 0x2D;       // VK_INSERT
    unsigned int hotkey_toggle_clickthrough = 0x23;  // VK_END

    static AppConfig LoadOrCreate(const std::wstring& path);
    void Save(const std::wstring& path) const;
};
