#pragma once

class CpuMonitor {
public:
    CpuMonitor();
    ~CpuMonitor();

    bool Init();
    double GetUsagePercent(); // 0-100, call periodically (not from multiple threads)
    double GetFrequencyMHz(); // best-effort current effective clock

private:
    void* m_query = nullptr;   // PDH_HQUERY
    void* m_counter = nullptr; // PDH_HCOUNTER
    void* m_frequencyCounter = nullptr; // PDH_HCOUNTER
};

// Best-effort CPU package temperature via WMI (root\WMI,
// MSAcpi_ThermalZoneTemperature). Many modern OEM systems disable this
// ACPI thermal zone and the call will simply return false - that is
// expected, not a bug. Returns true and fills outC (Celsius) on success.
bool GetCpuTemperatureC(double& outC);
