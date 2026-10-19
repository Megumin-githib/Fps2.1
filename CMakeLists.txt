cmake_minimum_required(VERSION 3.20)
project(FpsOverlay LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "This project targets Windows only (Win32 API, DXGI, PDH).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optional: enable real NVIDIA GPU temperature readout.
# Download the NVAPI SDK from https://developer.nvidia.com/nvapi and point
# NVAPI_DIR at its root (the folder containing nvapi.h and amd64/nvapi64.lib).
option(USE_NVAPI "Enable NVIDIA GPU temperature via NVAPI SDK" OFF)
set(NVAPI_DIR "" CACHE PATH "Path to NVAPI SDK root")

add_executable(FpsOverlay WIN32
    src/main.cpp
    src/config.cpp
    src/cpu_monitor.cpp
    src/gpu_monitor.cpp
    src/fps_monitor.cpp
    src/overlay_window.cpp
    src/process_utils.cpp
    src/session_monitor.cpp
    src/particle_background.cpp
    src/launcher_window.cpp
    src/settings_window.cpp
    src/network_monitor.cpp
)

target_include_directories(FpsOverlay PRIVATE include)
target_compile_definitions(FpsOverlay PRIVATE
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE
    # WIN32_LEAN_AND_MEAN stops <windows.h> from pulling in the legacy
    # <winsock.h>, which otherwise conflicts with <winsock2.h>
    # (network_monitor.cpp/.h). NOMINMAX stops windows.h's min()/max()
    # macros from breaking the std::min/std::max calls used elsewhere.
    WIN32_LEAN_AND_MEAN NOMINMAX
)

if (MINGW)
    # MinGW's linker needs an explicit nudge to use the wWinMainCRTStartup
    # entry point (we use the wide-string wWinMain, not WinMain), and we
    # statically link the GCC/C++ runtime so the exe doesn't need
    # libstdc++-6.dll / libgcc_s_seh-1.dll installed on the target machine.
    target_link_options(FpsOverlay PRIVATE -municode -static-libgcc -static-libstdc++ -static)
endif()

target_link_libraries(FpsOverlay PRIVATE
    user32 gdi32 gdiplus msimg32 pdh dxgi ole32 oleaut32 wbemuuid advapi32
    comdlg32 comctl32 iphlpapi ws2_32 shell32
)


if (USE_NVAPI)
    if (NOT NVAPI_DIR)
        message(FATAL_ERROR "Set -DNVAPI_DIR=<path to NVAPI SDK> when USE_NVAPI=ON")
    endif()
    target_compile_definitions(FpsOverlay PRIVATE HAVE_NVAPI)
    target_include_directories(FpsOverlay PRIVATE "${NVAPI_DIR}")
    target_link_directories(FpsOverlay PRIVATE "${NVAPI_DIR}/amd64")
endif()

# Copy the default config next to the executable so a first run has one.
configure_file(${CMAKE_SOURCE_DIR}/config.ini ${CMAKE_BINARY_DIR}/config.ini COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/assets_overlay.png ${CMAKE_BINARY_DIR}/assets_overlay.png COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/config.json ${CMAKE_BINARY_DIR}/config.json COPYONLY)
