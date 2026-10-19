@echo off
setlocal
cd /d "%~dp0"
where cmake >nul 2>nul || (echo CMake not found. Install CMake and MinGW-w64 ^(or w64devkit^) and add them to PATH. & pause & exit /b 1)
where g++ >nul 2>nul || (echo g++ not found. Install MinGW-w64 ^(or w64devkit^) and add it to PATH. & pause & exit /b 1)
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release || (pause & exit /b 1)
cmake --build build || (pause & exit /b 1)
if not exist bin mkdir bin
copy /y build\FpsOverlay.exe bin\FpsOverlay.exe >nul
if not exist bin\config.ini copy /y config.ini bin\config.ini >nul
if not exist bin\config.json copy /y config.json bin\config.json >nul
copy /y assets_overlay.png bin\assets_overlay.png >nul
echo.
echo Done: bin\FpsOverlay.exe
pause
