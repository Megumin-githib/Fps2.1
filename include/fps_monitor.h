#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>

struct FpsStats {
    double fps_avg = 0;
    double fps_1low = 0;
    double fps_01low = 0;
    bool valid = false;
};

// Measures FPS / 1% low / 0.1% low WITHOUT touching the target game's
// process: it launches PresentMon (https://github.com/GameTechDev/PresentMon,
// MIT licensed) as a child process, which uses ETW (Event Tracing for
// Windows) to observe Present calls system-wide, and reads its real-time
// CSV output over a pipe. This is the same non-invasive approach used by
// tools like CapFrameX.
class FpsMonitor {
public:
    ~FpsMonitor();

    // presentmonPath: path to PresentMon.exe
    // argsTemplate:   command-line args, "{proc}" is replaced with targetProcess
    // targetProcess:  executable name to track, e.g. L"game.exe"
    bool Start(const std::wstring& presentmonPath, const std::wstring& argsTemplate,
               const std::wstring& targetProcess);
    void Stop();

    // Computed from a rolling window of the most recent frames.
    FpsStats GetStats();

private:
    static DWORD WINAPI ReaderThreadProc(LPVOID param);
    void ReaderLoop();

    PROCESS_INFORMATION m_pi{};
    HANDLE m_stdoutRead = nullptr;
    HANDLE m_thread = nullptr;
    volatile bool m_running = false;

    std::mutex m_mutex;
    std::deque<double> m_frameTimesMs; // milliseconds between presents
    static constexpr size_t kWindowSize = 2000; // rolling window length

    int m_frameTimeColumnIndex = -1;
    std::vector<std::string> m_header;
};
