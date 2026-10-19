#pragma once
#include <string>
#include <windows.h>

enum class GpuVendor { Unknown, Nvidia, AMD, Intel };

struct GpuInfo {
    std::wstring name;
    GpuVendor vendor = GpuVendor::Unknown;
    LUID luid{};
    unsigned long long vram_total = 0;
};

// Discovers the primary discrete GPU via DXGI and reports usage / VRAM /
// temperature. GPU usage and VRAM use only built-in Windows APIs and work
// for any vendor. Temperature requires a vendor SDK (see gpu_monitor.cpp);
// without one, GetTemperatureC() simply returns false and the overlay
// shows "N/A" for GPU temp.
class GpuMonitor {
public:
    bool Init();
    void Shutdown();

    const GpuInfo& Info() const { return m_info; }

    double GetUsagePercent();              // 3D engine utilization, 0-100 (approximate, like Task Manager)
    unsigned long long GetVramUsedBytes();
    bool GetTemperatureC(double& outC);    // false if unavailable on this system/build

private:
    bool PickAdapter();
    bool InitPdhGpuEngine();
    bool InitNvidia();

    GpuInfo m_info;
    void* m_pdhQuery = nullptr;
    void* m_pdhCounter = nullptr;
    void* m_dxgiAdapter3 = nullptr; // IDXGIAdapter3*
    void* m_nvGpuHandle = nullptr;  // NvPhysicalGpuHandle, only used when built with HAVE_NVAPI
    bool m_nvInited = false;
};
