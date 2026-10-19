#include "config.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <regex>
#include <map>
#include <iterator>
#include <cwctype>
#include <filesystem>

namespace {

std::wstring GetStr(const wchar_t* section, const wchar_t* key,
                     const wchar_t* def, const std::wstring& path) {
    wchar_t buf[512];
    GetPrivateProfileStringW(section, key, def, buf, 512, path.c_str());
    return buf;
}

int GetIntV(const wchar_t* section, const wchar_t* key, int def,
            const std::wstring& path) {
    return (int)GetPrivateProfileIntW(section, key, def, path.c_str());
}

bool GetBoolV(const wchar_t* section, const wchar_t* key, bool def,
              const std::wstring& path) {
    return GetIntV(section, key, def ? 1 : 0, path) != 0;
}

std::wstring JsonPathFor(const std::wstring& iniPath) {
    std::wstring p = iniPath;
    const size_t dot = p.find_last_of(L'.');
    const size_t slash = p.find_last_of(L"\\/");
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) p.replace(dot, std::wstring::npos, L".json");
    else p += L".json";
    return p;
}

std::wstring JsonUnescape(std::wstring value) {
    std::wstring out; out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'\\' && i + 1 < value.size()) {
            wchar_t n = value[++i];
            if (n == L'n') out.push_back(L'\n');
            else if (n == L'r') out.push_back(L'\r');
            else if (n == L't') out.push_back(L'\t');
            else if (n == L'u' && i + 4 < value.size()) {
                unsigned int code = 0; bool valid = true;
                for (int j = 0; j < 4; ++j) {
                    wchar_t h = value[i + 1 + j]; code <<= 4;
                    if (h >= L'0' && h <= L'9') code |= (unsigned int)(h - L'0');
                    else if (h >= L'a' && h <= L'f') code |= (unsigned int)(h - L'a' + 10);
                    else if (h >= L'A' && h <= L'F') code |= (unsigned int)(h - L'A' + 10);
                    else { valid = false; break; }
                }
                if (valid) { out.push_back((wchar_t)code); i += 4; } else out.push_back(n);
            } else out.push_back(n);
        } else out.push_back(value[i]);
    }
    return out;
}

std::wstring JsonEscape(const std::wstring& value) {
    std::wstring out;
    for (wchar_t c : value) {
        if (c == L'\\' || c == L'"') { out.push_back(L'\\'); out.push_back(c); }
        else if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else if (c == L'\t') out += L"\\t";
        else out.push_back(c);
    }
    return out;
}

std::map<std::wstring, std::wstring> ReadJsonFlat(const std::wstring& path) {
    std::map<std::wstring, std::wstring> values;
    std::ifstream file(std::filesystem::path(path.c_str()), std::ios::binary);
    if (!file) return values;
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return values;
    int wideCount = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (wideCount <= 0) return values;
    std::wstring contents((size_t)wideCount, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), &contents[0], wideCount);
    const std::wregex pairRe(LR"json("((?:\\.|[^"\\])*)"\s*:\s*("(?:\\.|[^"\\])*"|-?[0-9]+(?:\.[0-9]+)?|true|false|null))json");
    for (std::wsregex_iterator it(contents.begin(), contents.end(), pairRe), end; it != end; ++it) {
        std::wstring key = JsonUnescape((*it)[1].str());
        std::wstring raw = (*it)[2].str();
        if (raw.size() >= 2 && raw.front() == L'"') raw = JsonUnescape(raw.substr(1, raw.size() - 2));
        values[key] = raw;
    }
    return values;
}

void ApplyJsonOverrides(AppConfig& c, const std::wstring& path) {
    const auto v = ReadJsonFlat(path);
    auto str = [&](const wchar_t* k, std::wstring& target) { auto i=v.find(k); if(i!=v.end()) target=i->second; };
    auto num = [&](const wchar_t* k, int& target, int lo, int hi) { auto i=v.find(k); if(i!=v.end()) { try { target=(std::max)(lo,(std::min)(hi,std::stoi(i->second))); } catch(...) {} } };
    auto boolean = [&](const wchar_t* k, bool& target) { auto i=v.find(k); if(i!=v.end()) target=(i->second==L"true" || i->second==L"1"); };
    num(L"Window.PosX", c.pos_x, -32768, 32768); num(L"Window.PosY", c.pos_y, -32768, 32768);
    num(L"Window.Opacity", c.opacity, 0, 255); num(L"Window.FontSize", c.font_size, 8, 48); str(L"Window.FontName", c.font_name);
    num(L"Window.TextColor", (int&)c.text_color, 0, 0x7fffffff); num(L"Window.BgColor", (int&)c.bg_color, 0, 0x7fffffff);
    boolean(L"Window.ClickThrough", c.click_through); num(L"Window.UpdateIntervalMs", c.update_interval_ms, 1, 10000);
    num(L"Performance.LauncherFps", c.launcher_fps, 1, 240); num(L"Performance.OverlayFps", c.overlay_fps, 1, 240);
    str(L"Appearance.ImagePath", c.overlay_image_path); str(L"Appearance.ImagePath2", c.overlay_image_path2); num(L"Appearance.ImageRotateSeconds", c.image_rotate_seconds, 0, 86400); num(L"Appearance.ImageOpacity", c.image_opacity, 0, 100);
    str(L"Profiles.Active", c.profile); str(L"Network.PingHost", c.ping_host);
    boolean(L"Display.ShowFps", c.show_fps); boolean(L"Display.Show1Low", c.show_1_low); boolean(L"Display.Show01Low", c.show_01_low);
    boolean(L"Display.ShowTime", c.show_time); boolean(L"Display.ShowCpu", c.show_cpu); boolean(L"Display.ShowGpu", c.show_gpu);
    boolean(L"Display.ShowTemps", c.show_temps); boolean(L"Display.ShowPing", c.show_ping); boolean(L"Display.ShowNetwork", c.show_network);
    boolean(L"Display.ShowRam", c.show_ram); boolean(L"Display.ShowFrequencies", c.show_frequencies); boolean(L"Display.ShowSessionTime", c.show_session_time);
    num(L"Warnings.Fps", c.warning_fps, 1, 240); num(L"Warnings.TemperatureC", c.warning_temp_c, 30, 120); num(L"Warnings.RamPercent", c.warning_ram_percent, 50, 100);
    str(L"Fps.PresentMonPath", c.presentmon_path); str(L"Fps.PresentMonArgs", c.presentmon_args); str(L"Fps.BoundApp", c.target_process);
    boolean(L"Fps.AutoConnect", c.auto_connect); boolean(L"Launcher.ShowOnStart", c.show_launcher_on_start); boolean(L"Launcher.RunAtStartup", c.run_at_startup);
    num(L"Hotkeys.ToggleVisible", (int&)c.hotkey_toggle_visible, 1, 65535); num(L"Hotkeys.ToggleClickThrough", (int&)c.hotkey_toggle_clickthrough, 1, 65535); num(L"Hotkeys.CycleProfile", (int&)c.hotkey_cycle_profile, 1, 65535);
    for (const auto& kv : v) {
        const std::wstring prefix = L"Game.";
        if (kv.first.rfind(prefix, 0) != 0) continue;
        size_t sep = kv.first.rfind(L'.'); if (sep == std::wstring::npos || sep < prefix.size()) continue;
        std::wstring exe = kv.first.substr(prefix.size(), sep - prefix.size());
        std::wstring field = kv.first.substr(sep + 1);
        auto& gs = c.per_game[exe];
        try { if(field==L"PosX") gs.pos_x=(std::max)(-32768,(std::min)(32768,std::stoi(kv.second))); else if(field==L"PosY") gs.pos_y=(std::max)(-32768,(std::min)(32768,std::stoi(kv.second))); else if(field==L"Profile") gs.profile=kv.second; } catch(...) {}
    }
}

} // namespace

AppConfig AppConfig::LoadOrCreate(const std::wstring& path) {
    AppConfig cfg;

    DWORD attrs = GetFileAttributesW(path.c_str());
    const std::wstring jsonPath = JsonPathFor(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD jsonAttrs = GetFileAttributesW(jsonPath.c_str());
        if (jsonAttrs == INVALID_FILE_ATTRIBUTES) {
            // No config yet - write defaults so the user has something to edit.
            cfg.Save(path);
            return cfg;
        }
        ApplyJsonOverrides(cfg, jsonPath);
        cfg.Save(path); // create INI from the JSON settings, keeping both formats available
        return cfg;
    }

    cfg.pos_x = GetIntV(L"Window", L"PosX", cfg.pos_x, path);
    cfg.pos_y = GetIntV(L"Window", L"PosY", cfg.pos_y, path);
    cfg.opacity = (std::max)(0, (std::min)(255, GetIntV(L"Window", L"Opacity", cfg.opacity, path)));
    cfg.font_size = (std::max)(8, (std::min)(48, GetIntV(L"Window", L"FontSize", cfg.font_size, path)));
    cfg.font_name = GetStr(L"Window", L"FontName", cfg.font_name.c_str(), path);
    cfg.text_color = (unsigned int)GetIntV(L"Window", L"TextColor", (int)cfg.text_color, path);
    cfg.bg_color = (unsigned int)GetIntV(L"Window", L"BgColor", (int)cfg.bg_color, path);
    cfg.click_through = GetBoolV(L"Window", L"ClickThrough", cfg.click_through, path);
    cfg.update_interval_ms = GetIntV(L"Window", L"UpdateIntervalMs", cfg.update_interval_ms, path);
    cfg.launcher_fps = (std::max)(1, (std::min)(240, GetIntV(L"Performance", L"LauncherFps", cfg.launcher_fps, path)));
    cfg.overlay_fps = (std::max)(1, (std::min)(240, GetIntV(L"Performance", L"OverlayFps", cfg.overlay_fps, path)));
    cfg.overlay_image_path = GetStr(L"Appearance", L"ImagePath", cfg.overlay_image_path.c_str(), path);
    cfg.overlay_image_path2 = GetStr(L"Appearance", L"ImagePath2", cfg.overlay_image_path2.c_str(), path);
    cfg.image_rotate_seconds = (std::max)(0, (std::min)(86400, GetIntV(L"Appearance", L"ImageRotateSeconds", cfg.image_rotate_seconds, path)));
    cfg.image_opacity = (std::max)(0, (std::min)(100, GetIntV(L"Appearance", L"ImageOpacity", cfg.image_opacity, path)));
    cfg.profile = GetStr(L"Profiles", L"Active", cfg.profile.c_str(), path);
    cfg.ping_host = GetStr(L"Network", L"PingHost", cfg.ping_host.c_str(), path);
    cfg.hotkey_cycle_profile = (unsigned int)GetIntV(L"Hotkeys", L"CycleProfile", (int)cfg.hotkey_cycle_profile, path);

    cfg.show_fps = GetBoolV(L"Display", L"ShowFps", cfg.show_fps, path);
    cfg.show_1_low = GetBoolV(L"Display", L"Show1Low", cfg.show_1_low, path);
    cfg.show_01_low = GetBoolV(L"Display", L"Show01Low", cfg.show_01_low, path);
    cfg.show_time = GetBoolV(L"Display", L"ShowTime", cfg.show_time, path);
    cfg.show_cpu = GetBoolV(L"Display", L"ShowCpu", cfg.show_cpu, path);
    cfg.show_gpu = GetBoolV(L"Display", L"ShowGpu", cfg.show_gpu, path);
    cfg.show_temps = GetBoolV(L"Display", L"ShowTemps", cfg.show_temps, path);
    cfg.show_ping = GetBoolV(L"Display", L"ShowPing", cfg.show_ping, path);
    cfg.show_network = GetBoolV(L"Display", L"ShowNetwork", cfg.show_network, path);
    cfg.show_ram = GetBoolV(L"Display", L"ShowRam", cfg.show_ram, path);
    cfg.show_frequencies = GetBoolV(L"Display", L"ShowFrequencies", cfg.show_frequencies, path);
    cfg.warning_fps = (std::max)(1, (std::min)(240, GetIntV(L"Warnings", L"Fps", cfg.warning_fps, path)));
    cfg.warning_temp_c = (std::max)(30, (std::min)(120, GetIntV(L"Warnings", L"TemperatureC", cfg.warning_temp_c, path)));
    cfg.warning_ram_percent = (std::max)(50, (std::min)(100, GetIntV(L"Warnings", L"RamPercent", cfg.warning_ram_percent, path)));

    cfg.presentmon_path = GetStr(L"Fps", L"PresentMonPath", cfg.presentmon_path.c_str(), path);
    cfg.presentmon_args = GetStr(L"Fps", L"PresentMonArgs", cfg.presentmon_args.c_str(), path);

    // Backward compatibility with older config.ini files that only had
    // TargetProcess=auto|<exe name> and no separate AutoConnect flag.
    std::wstring legacyTarget = GetStr(L"Fps", L"TargetProcess", L"auto", path);
    cfg.auto_connect = GetBoolV(L"Fps", L"AutoConnect", legacyTarget == L"auto", path);
    cfg.target_process = (legacyTarget == L"auto") ? L"" : legacyTarget;
    cfg.target_process = GetStr(L"Fps", L"BoundApp", cfg.target_process.c_str(), path);

    cfg.show_session_time = GetBoolV(L"Display", L"ShowSessionTime", cfg.show_session_time, path);
    cfg.show_launcher_on_start = GetBoolV(L"Launcher", L"ShowOnStart", cfg.show_launcher_on_start, path);
    cfg.run_at_startup = GetBoolV(L"Launcher", L"RunAtStartup", cfg.run_at_startup, path);

    cfg.hotkey_toggle_visible = (unsigned int)GetIntV(L"Hotkeys", L"ToggleVisible", (int)cfg.hotkey_toggle_visible, path);
    cfg.hotkey_toggle_clickthrough = (unsigned int)GetIntV(L"Hotkeys", L"ToggleClickThrough", (int)cfg.hotkey_toggle_clickthrough, path);

    // Per-game profile and overlay position sections: [Game.<exe name>].
    wchar_t sectionNames[32768]{};
    DWORD chars = GetPrivateProfileSectionNamesW(sectionNames, (DWORD)(sizeof(sectionNames) / sizeof(sectionNames[0])), path.c_str());
    for (const wchar_t* sec = sectionNames; *sec && (DWORD)(sec - sectionNames) < chars; sec += wcslen(sec) + 1) {
        const std::wstring section(sec);
        if (section.rfind(L"Game.", 0) != 0 || section.size() <= 5) continue;
        GameSettings gs;
        gs.profile = GetStr(section.c_str(), L"Profile", L"Full", path);
        gs.pos_x = GetIntV(section.c_str(), L"PosX", cfg.pos_x, path);
        gs.pos_y = GetIntV(section.c_str(), L"PosY", cfg.pos_y, path);
        cfg.per_game[section.substr(5)] = gs;
    }
    // If both formats exist, JSON is authoritative; Save() keeps them synchronized.
    if (GetFileAttributesW(jsonPath.c_str()) != INVALID_FILE_ATTRIBUTES) ApplyJsonOverrides(cfg, jsonPath);
    return cfg;
}

void AppConfig::Save(const std::wstring& path) const {
    auto SetI = [&](const wchar_t* section, const wchar_t* key, int v) {
        wchar_t buf[32];
        swprintf_s(buf, L"%d", v);
        WritePrivateProfileStringW(section, key, buf, path.c_str());
    };
    auto SetS = [&](const wchar_t* section, const wchar_t* key, const std::wstring& v) {
        WritePrivateProfileStringW(section, key, v.c_str(), path.c_str());
    };

    SetI(L"Window", L"PosX", pos_x);
    SetI(L"Window", L"PosY", pos_y);
    SetI(L"Window", L"Opacity", opacity);
    SetI(L"Window", L"FontSize", font_size);
    SetS(L"Window", L"FontName", font_name);
    SetI(L"Window", L"TextColor", (int)text_color);
    SetI(L"Window", L"BgColor", (int)bg_color);
    SetI(L"Window", L"ClickThrough", click_through ? 1 : 0);
    SetI(L"Window", L"UpdateIntervalMs", update_interval_ms);
    SetI(L"Performance", L"LauncherFps", (std::max)(1, (std::min)(240, launcher_fps)));
    SetI(L"Performance", L"OverlayFps", (std::max)(1, (std::min)(240, overlay_fps)));
    SetS(L"Appearance", L"ImagePath", overlay_image_path);
    SetI(L"Appearance", L"ImageOpacity", (std::max)(0, (std::min)(100, image_opacity)));
    SetS(L"Profiles", L"Active", profile);
    SetS(L"Network", L"PingHost", ping_host);

    SetI(L"Display", L"ShowFps", show_fps ? 1 : 0);
    SetI(L"Display", L"Show1Low", show_1_low ? 1 : 0);
    SetI(L"Display", L"Show01Low", show_01_low ? 1 : 0);
    SetI(L"Display", L"ShowTime", show_time ? 1 : 0);
    SetI(L"Display", L"ShowCpu", show_cpu ? 1 : 0);
    SetI(L"Display", L"ShowGpu", show_gpu ? 1 : 0);
    SetI(L"Display", L"ShowTemps", show_temps ? 1 : 0);
    SetI(L"Display", L"ShowPing", show_ping ? 1 : 0);
    SetI(L"Display", L"ShowNetwork", show_network ? 1 : 0);
    SetI(L"Display", L"ShowRam", show_ram ? 1 : 0);
    SetI(L"Display", L"ShowFrequencies", show_frequencies ? 1 : 0);
    SetI(L"Warnings", L"Fps", warning_fps);
    SetI(L"Warnings", L"TemperatureC", warning_temp_c);
    SetI(L"Warnings", L"RamPercent", warning_ram_percent);
    SetI(L"Display", L"ShowSessionTime", show_session_time ? 1 : 0);

    SetS(L"Fps", L"PresentMonPath", presentmon_path);
    SetS(L"Fps", L"PresentMonArgs", presentmon_args);
    // Kept for older readers that still look for TargetProcess=auto.
    SetS(L"Fps", L"TargetProcess", auto_connect ? L"auto" : target_process);
    SetI(L"Fps", L"AutoConnect", auto_connect ? 1 : 0);
    SetS(L"Fps", L"BoundApp", target_process);

    SetI(L"Launcher", L"ShowOnStart", show_launcher_on_start ? 1 : 0);
    SetI(L"Launcher", L"RunAtStartup", run_at_startup ? 1 : 0);

    SetI(L"Hotkeys", L"ToggleVisible", (int)hotkey_toggle_visible);
    SetI(L"Hotkeys", L"ToggleClickThrough", (int)hotkey_toggle_clickthrough);
    SetI(L"Hotkeys", L"CycleProfile", (int)hotkey_cycle_profile);
    for (const auto& entry : per_game) {
        const std::wstring section = L"Game." + entry.first;
        SetI(section.c_str(), L"PosX", entry.second.pos_x);
        SetI(section.c_str(), L"PosY", entry.second.pos_y);
        SetS(section.c_str(), L"Profile", entry.second.profile);
    }


    // Mirror all settings into a flat JSON file. Either format can be edited;
    // JSON wins on load when both files are present.
    const std::wstring jsonPath = JsonPathFor(path);
    std::wostringstream json;
        json << L"{\n";
        bool first = true;
        auto key = [&](const std::wstring& k, const std::wstring& v) { if(!first) json << L",\n"; first=false; json << L"  \"" << JsonEscape(k) << L"\": \"" << JsonEscape(v) << L"\""; };
        auto num = [&](const std::wstring& k, long long v) { if(!first) json << L",\n"; first=false; json << L"  \"" << JsonEscape(k) << L"\": " << v; };
        auto boolean = [&](const std::wstring& k, bool v) { if(!first) json << L",\n"; first=false; json << L"  \"" << JsonEscape(k) << L"\": " << (v ? L"true" : L"false"); };
        num(L"Window.PosX",pos_x); num(L"Window.PosY",pos_y); num(L"Window.Opacity",opacity); num(L"Window.FontSize",font_size); key(L"Window.FontName",font_name); num(L"Window.TextColor",text_color); num(L"Window.BgColor",bg_color); boolean(L"Window.ClickThrough",click_through); num(L"Window.UpdateIntervalMs",update_interval_ms);
        num(L"Performance.LauncherFps",launcher_fps); num(L"Performance.OverlayFps",overlay_fps); key(L"Appearance.ImagePath",overlay_image_path); key(L"Appearance.ImagePath2",overlay_image_path2); num(L"Appearance.ImageRotateSeconds",image_rotate_seconds); num(L"Appearance.ImageOpacity",image_opacity); key(L"Profiles.Active",profile); key(L"Network.PingHost",ping_host);
        boolean(L"Display.ShowFps",show_fps); boolean(L"Display.Show1Low",show_1_low); boolean(L"Display.Show01Low",show_01_low); boolean(L"Display.ShowTime",show_time); boolean(L"Display.ShowCpu",show_cpu); boolean(L"Display.ShowGpu",show_gpu); boolean(L"Display.ShowTemps",show_temps); boolean(L"Display.ShowPing",show_ping); boolean(L"Display.ShowNetwork",show_network); boolean(L"Display.ShowRam",show_ram); boolean(L"Display.ShowFrequencies",show_frequencies); boolean(L"Display.ShowSessionTime",show_session_time);
        num(L"Warnings.Fps",warning_fps); num(L"Warnings.TemperatureC",warning_temp_c); num(L"Warnings.RamPercent",warning_ram_percent); key(L"Fps.PresentMonPath",presentmon_path); key(L"Fps.PresentMonArgs",presentmon_args); key(L"Fps.BoundApp",target_process); boolean(L"Fps.AutoConnect",auto_connect); boolean(L"Launcher.ShowOnStart",show_launcher_on_start); boolean(L"Launcher.RunAtStartup",run_at_startup); num(L"Hotkeys.ToggleVisible",hotkey_toggle_visible); num(L"Hotkeys.ToggleClickThrough",hotkey_toggle_clickthrough); num(L"Hotkeys.CycleProfile",hotkey_cycle_profile);
        for (const auto& entry : per_game) { const std::wstring base=L"Game."+entry.first+L"."; num(base+L"PosX",entry.second.pos_x); num(base+L"PosY",entry.second.pos_y); key(base+L"Profile",entry.second.profile); }
        json << L"\n}\n";
    const std::wstring wideJson = json.str();
    const int byteCount = WideCharToMultiByte(CP_UTF8, 0, wideJson.data(), (int)wideJson.size(), nullptr, 0, nullptr, nullptr);
    if (byteCount > 0) {
        std::string utf8((size_t)byteCount, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideJson.data(), (int)wideJson.size(), &utf8[0], byteCount, nullptr, nullptr);
        std::ofstream out(std::filesystem::path(jsonPath.c_str()), std::ios::binary | std::ios::trunc);
        if (out) out.write(utf8.data(), (std::streamsize)utf8.size());
    }
}
