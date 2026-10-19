// network_monitor.h defines WIN32_LEAN_AND_MEAN and includes <winsock2.h>
// before <windows.h>, so iphlpapi.h/icmpapi.h below see a consistent
// Winsock 2 environment (no legacy <winsock.h> conflict).
#include "network_monitor.h"

#include <iphlpapi.h>
#include <icmpapi.h>

#include <vector>
#include <string>
#include <utility>
#include <mutex>

#pragma comment(lib, "iphlpapi.lib")

NetworkMonitor::NetworkMonitor(std::wstring host)
    : m_host(std::move(host)),
      m_pingThread(&NetworkMonitor::PingLoop, this),
      m_adapterThread(&NetworkMonitor::AdapterLoop, this) {}

NetworkMonitor::~NetworkMonitor() {
    m_running = false;

    if (m_pingThread.joinable())
        m_pingThread.join();
    if (m_adapterThread.joinable())
        m_adapterThread.join();
}

void NetworkMonitor::SetHost(const std::wstring& host) {
    if (host.empty())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_host = host;
}

NetworkStats NetworkMonitor::Stats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

void NetworkMonitor::Update() {
    const ULONGLONG now = GetTickCount64();

    if (m_lastSample != 0 && now - m_lastSample < 1000)
        return;

    ULONG bufferSize = 0;
    DWORD status = GetIfTable(nullptr, &bufferSize, FALSE);

    if (status != ERROR_INSUFFICIENT_BUFFER || bufferSize == 0) {
        m_lastSample = now;
        return;
    }

    std::vector<BYTE> buffer(bufferSize);
    auto* table = reinterpret_cast<MIB_IFTABLE*>(buffer.data());

    status = GetIfTable(table, &bufferSize, FALSE);

    if (status != NO_ERROR) {
        m_lastSample = now;
        return;
    }

    ULONGLONG inBytes = 0;
    ULONGLONG outBytes = 0;

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IFROW& row = table->table[i];

        if (row.dwOperStatus != MIB_IF_OPER_STATUS_OPERATIONAL ||
            row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }

        inBytes += row.dwInOctets;
        outBytes += row.dwOutOctets;
    }

    double down = 0.0;
    double up = 0.0;

    if (m_haveBaseline && now > m_lastSample) {
        const double seconds =
            static_cast<double>(now - m_lastSample) / 1000.0;

        down = (inBytes >= m_prevIn
                    ? static_cast<double>(inBytes - m_prevIn)
                    : 0.0) * 8.0 / seconds / 1000000.0;

        up = (outBytes >= m_prevOut
                  ? static_cast<double>(outBytes - m_prevOut)
                  : 0.0) * 8.0 / seconds / 1000000.0;
    }

    m_prevIn = inBytes;
    m_prevOut = outBytes;
    m_haveBaseline = true;
    m_lastSample = now;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.download_mbps = down;
    m_stats.upload_mbps = up;
}

void NetworkMonitor::AdapterLoop() {
    while (m_running) {
        Update();
        // Short sleep slices make shutdown responsive.
        for (int i = 0; i < 50 && m_running; ++i) Sleep(20);
    }
}

void NetworkMonitor::PingLoop() {
    while (m_running) {
        std::wstring wideHost;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            wideHost = m_host;
        }

        std::string host;
        for (wchar_t ch : wideHost)
            host.push_back(ch < 128 ? static_cast<char>(ch) : '?');

        IN_ADDR address{};
        address.S_un.S_addr = inet_addr(host.c_str());

        bool valid = false;
        double ping = -1.0;

        if (address.S_un.S_addr != INADDR_NONE ||
            host == "255.255.255.255") {
            HANDLE hIcmp = IcmpCreateFile();

            if (hIcmp != INVALID_HANDLE_VALUE) {
                char payload[] = "fps-overlay";
                std::vector<BYTE> reply(
                    sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 32);

                DWORD result = IcmpSendEcho(
                    hIcmp,
                    address.S_un.S_addr,
                    payload,
                    sizeof(payload),
                    nullptr,
                    reply.data(),
                    static_cast<DWORD>(reply.size()),
                    250);

                if (result > 0) {
                    auto* r =
                        reinterpret_cast<ICMP_ECHO_REPLY*>(reply.data());
                    ping = static_cast<double>(r->RoundTripTime);
                    valid = true;
                }

                IcmpCloseHandle(hIcmp);
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stats.ping_valid = valid;
            m_stats.ping_ms = ping;
        }

        for (int i = 0; i < 150 && m_running; ++i)
            Sleep(10);
    }
}
