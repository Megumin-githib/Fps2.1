#include "gpu_monitor.h"
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "pdh.lib")

#ifdef HAVE_NVAPI
// This is the real, official NVIDIA header from the NVAPI SDK
// (https://developer.nvidia.com/nvapi). Build with -DUSE_NVAPI=ON and
// -DNVAPI_DIR=<path to the SDK> (see CMakeLists.txt / README.md).
#include <nvapi.h>
#pragma comment(lib, "nvapi64.lib")
#endif

// ---------------------------------------------------------------------
// Adapter selection (DXGI) - vendor-agnostic.
// ---------------------------------------------------------------------
bool GpuMonitor::PickAdapter() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

    IDXGIAdapter1* best = nullptr;
    DXGI_ADAPTER_DESC1 bestDesc = {};
    SIZE_T bestVram = 0;

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        // Skip the software/basic-render adapter and keep the one with the
        // most dedicated VRAM as a simple heuristic for "the main GPU".
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && desc.DedicatedVideoMemory > bestVram) {
            if (best) best->Release();
            best = adapter;
            bestDesc = desc;
            bestVram = desc.DedicatedVideoMemory;
        } else {
            adapter->Release();
        }
    }

    if (!best) {
        factory->Release();
        return false;
    }

    m_info.name = bestDesc.Description;
    m_info.luid = bestDesc.AdapterLuid;
    m_info.vram_total = bestDesc.DedicatedVideoMemory;

    switch (bestDesc.VendorId) {
        case 0x10DE: m_info.vendor = GpuVendor::Nvidia; break;
        case 0x1002:
        case 0x1022: m_info.vendor = GpuVendor::AMD; break;
        case 0x8086: m_info.vendor = GpuVendor::Intel; break;
        default: m_info.vendor = GpuVendor::Unknown; break;
    }

    IDXGIAdapter3* adapter3 = nullptr;
    if (SUCCEEDED(best->QueryInterface(IID_PPV_ARGS(&adapter3)))) {
        m_dxgiAdapter3 = adapter3; // kept alive for VRAM queries
    }

    best->Release();
    factory->Release();
    return true;
}

unsigned long long GpuMonitor::GetVramUsedBytes() {
    if (!m_dxgiAdapter3) return 0;
    DXGI_QUERY_VIDEO_MEMORY_INFO info;
    if (FAILED(((IDXGIAdapter3*)m_dxgiAdapter3)->QueryVideoMemoryInfo(
            0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
        return 0;
    return info.CurrentUsage;
}

// ---------------------------------------------------------------------
// GPU usage via the built-in "GPU Engine" performance counters
// (Windows 10 1703+). This is the same data source the Task Manager GPU
// graph uses, works for any vendor, and needs no driver/SDK.
// ---------------------------------------------------------------------
bool GpuMonitor::InitPdhGpuEngine() {
    PDH_HQUERY query;
    if (PdhOpenQuery(nullptr, 0, &query) != ERROR_SUCCESS) return false;

    PDH_HCOUNTER counter;
    if (PdhAddCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return false;
    }

    m_pdhQuery = query;
    m_pdhCounter = counter;
    PdhCollectQueryData(query);
    return true;
}

double GpuMonitor::GetUsagePercent() {
    if (!m_pdhQuery) return 0.0;

    PdhCollectQueryData((PDH_HQUERY)m_pdhQuery);

    DWORD bufSize = 0, itemCount = 0;
    PdhGetFormattedCounterArrayW((PDH_HCOUNTER)m_pdhCounter, PDH_FMT_DOUBLE, &bufSize, &itemCount, nullptr);
    if (bufSize == 0) return 0.0;

    std::vector<BYTE> buffer(bufSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW((PDH_HCOUNTER)m_pdhCounter, PDH_FMT_DOUBLE, &bufSize, &itemCount, items) != ERROR_SUCCESS)
        return 0.0;

    // Instance names look like:
    //   pid_1234_luid_0x00000000_0x0000ABCD_phys_0_eng_0_engtype_3D
    // Sum the 3D-engine utilization for instances that belong to our
    // chosen adapter's LUID (approximate, same idea as Task Manager).
    wchar_t luidTag[64];
    swprintf_s(luidTag, L"luid_0x%08X_0x%08X",
               (unsigned)m_info.luid.HighPart, (unsigned)m_info.luid.LowPart);

    double total = 0.0;
    for (DWORD i = 0; i < itemCount; ++i) {
        std::wstring name = items[i].szName;
        if (name.find(L"engtype_3D") != std::wstring::npos &&
            name.find(luidTag) != std::wstring::npos) {
            total += items[i].FmtValue.doubleValue;
        }
    }
    if (total > 100.0) total = 100.0;
    if (total < 0.0) total = 0.0;
    return total;
}

// ---------------------------------------------------------------------
// Temperature. Windows has no built-in, vendor-agnostic GPU temperature
// API, so this is necessarily vendor-SDK-gated:
//   - NVIDIA: implemented below via NVAPI (optional, see CMakeLists.txt).
//   - AMD:    integrate AMD's ADLX SDK here
//             (IADLXGPUMetrics::GetTemperatureHotspot / GetTemperatureEdge).
//   - Intel:  integrate Intel's Graphics Control Library (igcl) here.
// Without the relevant SDK this simply returns false, and the overlay
// shows "N/A" - that is expected, not a bug.
// ---------------------------------------------------------------------
bool GpuMonitor::InitNvidia() {
#ifdef HAVE_NVAPI
    if (NvAPI_Initialize() != NVAPI_OK) return false;

    NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS];
    NvU32 count = 0;
    if (NvAPI_EnumPhysicalGPUs(handles, &count) != NVAPI_OK || count == 0) return false;

    // TODO: on multi-GPU NVIDIA systems, match the handle to m_info.luid
    // instead of always taking the first one.
    m_nvGpuHandle = handles[0];
    m_nvInited = true;
    return true;
#else
    return false;
#endif
}

bool GpuMonitor::GetTemperatureC(double& outC) {
#ifdef HAVE_NVAPI
    if (m_info.vendor == GpuVendor::Nvidia && m_nvInited) {
        NV_GPU_THERMAL_SETTINGS settings = {};
        settings.version = NV_GPU_THERMAL_SETTINGS_VER;
        if (NvAPI_GPU_GetThermalSettings((NvPhysicalGpuHandle)m_nvGpuHandle,
                                          0, &settings) == NVAPI_OK &&
            settings.count > 0) {
            outC = (double)settings.sensor[0].currentTemp;
            return true;
        }
    }
#endif
    // AMD / Intel extension points - see comment above.
    (void)outC;
    return false;
}

// ---------------------------------------------------------------------
bool GpuMonitor::Init() {
    if (!PickAdapter()) return false;
    InitPdhGpuEngine(); // non-fatal if it fails on very old Windows builds
    if (m_info.vendor == GpuVendor::Nvidia) InitNvidia();
    return true;
}

void GpuMonitor::Shutdown() {
    if (m_pdhQuery) PdhCloseQuery((PDH_HQUERY)m_pdhQuery);
    if (m_dxgiAdapter3) ((IDXGIAdapter3*)m_dxgiAdapter3)->Release();
#ifdef HAVE_NVAPI
    if (m_nvInited) NvAPI_Unload();
#endif
}
