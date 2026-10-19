#include "fps_monitor.h"
#include <sstream>
#include <algorithm>
#include <cctype>

FpsMonitor::~FpsMonitor() { Stop(); }

bool FpsMonitor::Start(const std::wstring& presentmonPath, const std::wstring& argsTemplate,
                        const std::wstring& targetProcess) {
    Stop();

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE stdoutRead = nullptr, stdoutWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0)) return false;
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

    std::wstring args = argsTemplate;
    size_t pos = args.find(L"{proc}");
    if (pos != std::wstring::npos) args.replace(pos, 6, targetProcess);

    std::wstring cmdLine = L"\"" + presentmonPath + L"\" " + args;
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = stdoutWrite;
    si.hStdError = stdoutWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(stdoutWrite);
    if (!ok) {
        CloseHandle(stdoutRead);
        return false;
    }

    m_pi = pi;
    m_stdoutRead = stdoutRead;
    m_frameTimeColumnIndex = -1;
    m_header.clear();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameTimesMs.clear();
    }
    m_running = true;
    m_thread = CreateThread(nullptr, 0, ReaderThreadProc, this, 0, nullptr);
    return true;
}

void FpsMonitor::Stop() {
    m_running = false;
    if (m_pi.hProcess) {
        TerminateProcess(m_pi.hProcess, 0);
        CloseHandle(m_pi.hProcess);
        CloseHandle(m_pi.hThread);
        m_pi = {};
    }
    if (m_stdoutRead) {
        CloseHandle(m_stdoutRead);
        m_stdoutRead = nullptr;
    }
    if (m_thread) {
        WaitForSingleObject(m_thread, 2000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
}

DWORD WINAPI FpsMonitor::ReaderThreadProc(LPVOID param) {
    static_cast<FpsMonitor*>(param)->ReaderLoop();
    return 0;
}

namespace {
std::vector<std::string> SplitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(item);
    return out;
}
}

void FpsMonitor::ReaderLoop() {
    std::string pending;
    char buf[4096];
    DWORD bytesRead = 0;

    while (m_running) {
        if (!ReadFile(m_stdoutRead, buf, sizeof(buf), &bytesRead, nullptr) || bytesRead == 0)
            break;
        pending.append(buf, bytesRead);

        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            auto fields = SplitCsv(line);

            if (m_header.empty()) {
                // First line is the CSV header. PresentMon's exact column
                // names have varied slightly across versions, so match
                // loosely on "msbetweenpresents" (the frame-time column).
                m_header = fields;
                for (size_t i = 0; i < fields.size(); ++i) {
                    std::string lower = fields[i];
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                                    [](unsigned char c) { return (char)std::tolower(c); });
                    if (lower.find("msbetweenpresents") != std::string::npos) {
                        m_frameTimeColumnIndex = (int)i;
                        break;
                    }
                }
                continue;
            }

            if (m_frameTimeColumnIndex < 0 || m_frameTimeColumnIndex >= (int)fields.size())
                continue;

            try {
                double ms = std::stod(fields[m_frameTimeColumnIndex]);
                if (ms > 0.0 && ms < 10000.0) { // sanity bounds
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_frameTimesMs.push_back(ms);
                    while (m_frameTimesMs.size() > kWindowSize)
                        m_frameTimesMs.pop_front();
                }
            } catch (...) {
                // malformed/partial line - ignore
            }
        }
    }
}

FpsStats FpsMonitor::GetStats() {
    FpsStats s;
    std::vector<double> times;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_frameTimesMs.empty()) return s;
        times.assign(m_frameTimesMs.begin(), m_frameTimesMs.end());
    }

    double sum = 0;
    for (double t : times) sum += t;
    double avgMs = sum / times.size();
    s.fps_avg = avgMs > 0 ? 1000.0 / avgMs : 0.0;

    std::sort(times.begin(), times.end()); // ascending - slowest frames at the end

    // "X% low" = FPS computed from the average frame time of the slowest
    // X% of frames in the window (a common definition; some tools instead
    // use a percentile of frame time - results will be close but not
    // bit-identical to e.g. AMD/NVIDIA's own overlays).
    auto lowAvg = [&](double fraction) {
        size_t count = (size_t)(std::max)(1.0, times.size() * fraction);
        double sum2 = 0;
        for (size_t i = times.size() - count; i < times.size(); ++i) sum2 += times[i];
        double avg = sum2 / count;
        return avg > 0 ? 1000.0 / avg : 0.0;
    };

    s.fps_1low = lowAvg(0.01);
    s.fps_01low = lowAvg(0.001);
    s.valid = true;
    return s;
}
