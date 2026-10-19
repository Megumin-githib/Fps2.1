#pragma once

// Must come before <windows.h>: without it, windows.h silently pulls in the
// legacy <winsock.h>, which then collides with <winsock2.h> (included in
// network_monitor.cpp) - that's what caused the "redefinition" / "missing
// type specifier" cascade of errors in network_monitor.cpp.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>

struct NetworkStats {
    double ping_ms = -1.0;
    double download_mbps = 0.0;
    double upload_mbps = 0.0;
    bool ping_valid = false;
};

// Adapter counters are sampled at most once/sec. ICMP runs on a worker thread,
// so packet loss or a slow response never stalls the overlay's render loop.
class NetworkMonitor {
public:
    explicit NetworkMonitor(std::wstring host = L"1.1.1.1");
    ~NetworkMonitor();
    void Update();
    NetworkStats Stats() const;
    void SetHost(const std::wstring& host);
private:
    void PingLoop();
    void AdapterLoop();
    std::wstring m_host;
    NetworkStats m_stats;
    mutable std::mutex m_mutex;
    std::atomic<bool> m_running{true};
    // State used by AdapterLoop is constructed before either worker starts.
    ULONGLONG m_lastSample = 0;
    ULONGLONG m_prevIn = 0, m_prevOut = 0;
    bool m_haveBaseline = false;
    std::thread m_pingThread;
    std::thread m_adapterThread;
};
