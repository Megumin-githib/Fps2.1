#include "cpu_monitor.h"
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#pragma comment(lib, "pdh.lib")

CpuMonitor::CpuMonitor() {}

CpuMonitor::~CpuMonitor() {
    if (m_query) PdhCloseQuery((PDH_HQUERY)m_query);
}

bool CpuMonitor::Init() {
    PDH_HQUERY query;
    if (PdhOpenQuery(nullptr, 0, &query) != ERROR_SUCCESS) return false;

    PDH_HCOUNTER counter;
    // "% Processor Utility" accounts for turbo/frequency scaling and is the
    // more accurate modern counter; fall back to the classic one if it's
    // not available (older Windows versions).
    if (PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\% Processor Utility",
                               0, &counter) != ERROR_SUCCESS) {
        if (PdhAddEnglishCounterW(query, L"\\Processor(_Total)\\% Processor Time",
                                   0, &counter) != ERROR_SUCCESS) {
            PdhCloseQuery(query);
            return false;
        }
    }

    m_query = query;
    m_counter = counter;
    PDH_HCOUNTER frequencyCounter = nullptr;
    if (PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\Processor Frequency", 0, &frequencyCounter) == ERROR_SUCCESS)
        m_frequencyCounter = frequencyCounter;
    PdhCollectQueryData(query); // prime the counter (first sample is meaningless)
    return true;
}

double CpuMonitor::GetFrequencyMHz() {
    if (!m_query || !m_frequencyCounter) return 0.0;
    PDH_FMT_COUNTERVALUE val{};
    if (PdhGetFormattedCounterValue((PDH_HCOUNTER)m_frequencyCounter, PDH_FMT_DOUBLE, nullptr, &val) != ERROR_SUCCESS || val.doubleValue <= 0)
        return 0.0;
    return val.doubleValue;
}

double CpuMonitor::GetUsagePercent() {
    if (!m_query) return 0.0;

    PdhCollectQueryData((PDH_HQUERY)m_query);

    PDH_FMT_COUNTERVALUE val;
    if (PdhGetFormattedCounterValue((PDH_HCOUNTER)m_counter, PDH_FMT_DOUBLE, nullptr, &val) != ERROR_SUCCESS)
        return 0.0;

    double v = val.doubleValue;
    if (v < 0) v = 0;
    if (v > 100) v = 100; // "% Processor Utility" can briefly exceed 100 with turbo boost
    return v;
}

// ---------------------------------------------------------------------
// CPU temperature via WMI. Requires COM to already be initialized
// (CoInitializeEx) on the calling thread - main.cpp does this at startup.
// ---------------------------------------------------------------------
#include <comdef.h>
#include <wbemidl.h>
#pragma comment(lib, "wbemuuid.lib")

bool GetCpuTemperatureC(double& outC) {
    bool ok = false;

    IWbemLocator* loc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_IWbemLocator, (LPVOID*)&loc)))
        return false;

    IWbemServices* svc = nullptr;
    HRESULT hr = loc->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, 0, 0, 0, 0, &svc);
    if (FAILED(hr)) {
        loc->Release();
        return false;
    }

    CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                       RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    IEnumWbemClassObject* enumerator = nullptr;
    hr = svc->ExecQuery(_bstr_t(L"WQL"),
                         _bstr_t(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
                         WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);

    if (SUCCEEDED(hr) && enumerator) {
        IWbemClassObject* obj = nullptr;
        ULONG returned = 0;
        if (enumerator->Next(WBEM_INFINITE, 1, &obj, &returned) == S_OK && returned > 0) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(obj->Get(L"CurrentTemperature", 0, &v, nullptr, nullptr)) &&
                v.vt == VT_I4) {
                // Value is in tenths of a degree Kelvin.
                double kelvinTenths = (double)v.lVal;
                outC = (kelvinTenths / 10.0) - 273.15;
                ok = true;
            }
            VariantClear(&v);
            obj->Release();
        }
        enumerator->Release();
    }

    svc->Release();
    loc->Release();
    return ok;
}
