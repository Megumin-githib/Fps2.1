# FPS Overlay (Windows)

Запускается как игра: полноэкранное стартовое меню с тёмным
анимированным фоном, из которого вы либо сразу входите в оверлей, либо
открываете настройки. Сам оверлей — прозрачный, перетаскиваемый,
всегда-поверх-всех HUD, показывающий:

- Счётчик времени: сколько привязанное приложение уже открыто (сессия)
- Текущее время
- FPS (среднее), 1% low, 0.1% low
- Загрузку CPU и GPU
- Использование VRAM
- Температуру CPU и GPU (где доступно — см. "Ограничения" ниже)

Это **отдельное окно поверх рабочего стола**, а не хук внутри игры —
поэтому оно надёжно работает с любым приложением без встраивания кода в
чужой процесс.

## Готовый exe

В папке `bin/` уже лежит собранный `FpsOverlay.exe` вместе с
`config.ini` — для запуска ничего дополнительно ставить не нужно, все
зависимости (кроме стандартных системных DLL Windows) зашиты внутрь
статически. Просто скопируйте папку `bin/` куда удобно (или прямо так)
и запустите `FpsOverlay.exe`.

Единственное, что добавляется отдельно по желанию — `PresentMon.exe`
(см. ниже), если нужен показ FPS.

Если хотите пересобрать сами (например, после правок кода) — см.
раздел "Сборка" ниже.

## Как это устроено при запуске

1. **Стартовое меню** (как в игре): открывается сразу при запуске exe —
   тёмный фон с туманом и мерцающими частицами, три кнопки:
   - **Запустить** — переходит к оверлею
   - **Настройки** — открывает окно настроек поверх меню
   - **Выход**
2. **Настройки** — открываются и из стартового меню, и настраивают:
   - **Автоподключение** — оверлей сам следит за активным (переднего
     плана) окном и переключается на него автоматически
   - или **конкретное приложение** — выбор из списка запущенных
     процессов, либо кнопка "Обзор..." чтобы указать .exe вручную,
     если приложение ещё не запущено
   - какие строки показывать в оверлее (время, FPS, Low, CPU, GPU,
     температуры, счётчик сессии)
   - click-through (прозрачность для кликов мыши)
   - показывать ли стартовое меню при следующем запуске
   - **запуск вместе с Windows** (добавляет/убирает ярлык автозапуска в
     реестре текущего пользователя, `HKCU\...\Run`)
3. **Оверлей** — после нажатия "Запустить" показывается HUD с
   выбранными метриками, включая счётчик "Session: ЧЧ:ММ:СС" — время,
   которое привязанное приложение уже открыто (берётся из реального
   времени запуска процесса, а не с момента старта оверлея).

Все настройки сохраняются в `config.ini` и применяются при следующем
запуске.

## Источники метрик

| Метрика         | Источник                                                                 |
|------------------|---------------------------------------------------------------------------|
| Время сессии     | `GetProcessTimes` привязанного процесса (реальное время его запуска)     |
| FPS / 1% / 0.1% | [PresentMon](https://github.com/GameTechDev/PresentMon) (ETW, системно, без инъекции в игру) |
| Загрузка CPU     | PDH, счётчик `% Processor Utility`                                       |
| Загрузка GPU     | PDH, счётчик `GPU Engine` (тот же источник, что у Диспетчера задач — работает для NVIDIA/AMD/Intel без SDK) |
| VRAM             | DXGI `IDXGIAdapter3::QueryVideoMemoryInfo`                               |
| Температура GPU  | NVIDIA — через официальный NVAPI SDK (опционально). AMD/Intel — точки расширения в `gpu_monitor.cpp` под их SDK (ADLX / Intel igcl) |
| Температура CPU  | WMI `MSAcpi_ThermalZoneTemperature` (best-effort)                        |

## Сборка

Требуется Windows 10 (1703+), Visual Studio 2022 (компонент "Desktop
development with C++") и CMake 3.20+.

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Исполняемый файл появится в `build/Release/FpsOverlay.exe`, рядом с
`config.ini`.

(Этот же проект можно собрать и кросс-компилятором MinGW-w64 на
Linux/macOS — именно так собран `FpsOverlay.exe`, уже лежащий в
архиве: `cmake -S . -B build -DCMAKE_SYSTEM_NAME=Windows
-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++` и `cmake --build build`.)

### Поддержка FPS (PresentMon)

1. Скачайте готовый релиз PresentMon:
   https://github.com/GameTechDev/PresentMon/releases
2. Положите `PresentMon.exe` рядом с `FpsOverlay.exe`, либо укажите
   полный путь в `config.ini` → `[Fps] PresentMonPath`.
3. **Важно:** набор флагов командной строки PresentMon менялся от версии
   к версии. Запустите `PresentMon.exe --help` для вашей версии и при
   необходимости поправьте `PresentMonArgs` в `config.ini`
   (`{proc}` подставляется автоматически — имя процесса для отслеживания).
4. По умолчанию включено автоподключение — оверлей сам отслеживает
   активное (переднего плана) окно и перезапускает PresentMon при
   переключении между приложениями/играми. В настройках можно задать
   конкретный процесс явно вместо автоподключения.
5. Запуск PresentMon обычно требует прав администратора (ETW-трассировка
   системных событий) — запускайте `FpsOverlay.exe` от администратора,
   если FPS не отображается.

### Температура NVIDIA GPU (опционально)

1. Скачайте NVAPI SDK: https://developer.nvidia.com/nvapi (бесплатная
   регистрация).
2. Соберите с флагами:
   ```bat
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
       -DUSE_NVAPI=ON -DNVAPI_DIR="C:/путь/до/R5xx-Pxxx"
   ```
   Без `USE_NVAPI=ON` всё остальное собирается и работает, просто
   температура NVIDIA GPU будет показываться как `N/A`.

### AMD / Intel GPU температура

Загрузка GPU и VRAM работают "из коробки" для любого вендора, но для
температуры нет единого системного API. В `gpu_monitor.cpp`, в функции
`GetTemperatureC()`, оставлены явные точки расширения — туда нужно
добавить вызовы AMD ADLX SDK или Intel Graphics Control Library (igcl),
если это актуально.

## Горячие клавиши и настройка

Основная настройка — через окно "Настройки" (доступно из стартового
меню). Также всё редактируется напрямую в `config.ini` или
перетаскиванием окна оверлея мышью:

- `Insert` — показать/скрыть оверлей
- `End` — включить/выключить режим "прозрачности для кликов" (click-through)
- Перетаскивание левой кнопкой мыши меняет позицию (сохраняется при выходе)
- `[Window]` — позиция, прозрачность, шрифт, цвета, интервал обновления
- `[Display]` — какие строки показывать, включая `ShowSessionTime`
- `[Fps]` — `AutoConnect` (1/0) и `BoundApp` (имя .exe), вместо старого `TargetProcess=auto`
- `[Launcher]` — `ShowOnStart` (показывать стартовое меню) и `RunAtStartup` (автозапуск с Windows)
- `[Hotkeys]` — коды клавиш (десятичные VK-коды, по умолчанию 45=Insert, 35=End)

## Ограничения (честно)

- **Температура CPU** через WMI на многих современных системах
  недоступна — производители ноутбуков/материнских плат часто отключают
  этот ACPI-датчик. Полноценный обходной путь (как у HWiNFO /
  LibreHardwareMonitor) требует собственного kernel-драйвера для чтения
  MSR-регистров — это отдельный, существенно более сложный проект.
- **Загрузка GPU** через счётчик `GPU Engine` — приближённая величина
  (суммируется по 3D-engine инстансам выбранного адаптера), это тот же
  метод, что использует Диспетчер задач, но не 100% идентичен
  показаниям GPU-Z/вендорских утилит.
- **1%/0.1% low** считаются как "среднее FPS по самым медленным X% кадров
  в скользящем окне" — распространённое определение, но не единственное;
  точные цифры могут немного отличаться от RTSS/CapFrameX, которые
  считают иначе.
- **Счётчик времени сессии** считает с момента реального запуска
  привязанного процесса (`GetProcessTimes`); если привязанное
  приложение ещё не запущено, счётчик показывает "--:--:--", пока оно
  не появится в системе.
- Некоторые игры с жёсткими системами анти-чита **могут** рассматривать
  любой сторонний оверлей как постороннее ПО, даже если он не внедряет
  код в процесс игры. Если это важно — проверьте политику конкретной
  игры/анти-чита перед использованием в онлайн-матчах.


---

## FPS Overlay: extended build and packaging

This source tree includes the extended HUD, image backdrop, INI/JSON configuration, Windows x64 build workflow, and NSIS installer recipe.

### Requirements and current behavior

- Targets Windows 10/11 x64.
- Launcher and overlay frame caps are independent (`Performance.LauncherFps` and `Performance.OverlayFps`, each 1–240).
- The supplied `assets_overlay.png` is drawn as a cropped HUD background at 60% image opacity by default, with a neon cyan/purple frame. Set `Appearance.ImagePath2` and `Appearance.ImageRotateSeconds` to optionally rotate between two images.
- `Minimum`, `Game`, and `Full` profiles can be selected in Settings. `Ctrl+Alt+F10` cycles profiles; the key's virtual-key code is configurable through `Hotkeys.CycleProfile` (or the numeric field in Settings).
- The app remembers overlay coordinates and the selected profile per process name in `[Game.<process>.exe]` INI sections / equivalent flattened JSON keys.
- Ping targets are configured in Settings or `Network.PingHost`. The current lightweight implementation accepts an IPv4 literal (default `1.1.1.1`); DNS hostnames are not resolved yet. ICMP sampling runs on a worker thread.
- Adapter download/upload rates use Windows interface byte counters and refresh about once per second.
- CPU clock is best-effort through Windows performance counters. CPU/GPU temperatures depend on sensor exposure by the hardware/firmware. GPU clock currently displays `N/A` in the standard build; vendor SDK integration is required for a reliable reading across GPU models.
- Warning colors are shown when FPS, RAM usage, or a reported temperature cross the thresholds in `[Warnings]`.
- Closing the HUD hides it to the system tray; use the tray menu to show it again or exit.
- No CSV history or screenshot capture is implemented. PresentMon writes its CSV stream to stdout only; FPS Overlay does not create session-history files.

### Configuration files

`config.ini` and `config.json` are both read and written. When both exist, `config.json` takes precedence on startup; application changes keep the files synchronized. JSON uses flat dotted keys (for example `Performance.OverlayFps`) so the file can be edited without a separate JSON library. Invalid numeric values are clamped to supported ranges.

### Build locally

Install CMake and a MinGW-w64 x64 toolchain, then run `build.bat` on Windows. `PresentMon.exe` is required for FPS capture and must be next to `FpsOverlay.exe`.

### GitHub Actions packaging

`.github/workflows/build.yml` builds on Windows x64, downloads the official PresentMon release runtime, and publishes three artifacts:

- `FPSOverlay_Source.zip` — source project and build workflow.
- `FPSOverlay_Portable.zip` — portable app and runtime dependencies.
- `FPSOverlay_Setup.exe` — NSIS installer.

Pushing a version tag such as `v1.0.0` also creates a GitHub Release with those files. The workflow must be run in a GitHub repository; files in a local ZIP do not trigger GitHub Actions by themselves.

      
Это приложение создано полностью с помощью ИИ, и ничего не было изменено в коде человеком.
This application was created entirely using AI, and no part of the code has been altered by a human.
