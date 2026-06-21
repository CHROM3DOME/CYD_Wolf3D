# ESP32-CYD Wolf3D

Wolfenstein 3D running on the classic ESP32 Cheap Yellow Display / ESP32-2432S028R style board.

This project started as a hardware tester for CYD clones and grew into a purpose-built ESP32 port of the Wolf4SDL engine. The active target is the full commercial Wolfenstein 3D data set (`.WL6`) on an SD card.

## Status

The current Wolf3D build boots directly into gameplay on the CYD and includes:

- landscape ILI9341 display output
- touch/input scaffolding for CYD-style controls (left half of touch screen controls movement, top right opens doors, bottom right fires)
- SD-card loading of Wolf3D data
- simplified renderer tuned for ESP32 RAM and CPU limits
- flat-color walls with selected low-resolution cached texture support
- static decoration impostors and caches
- visible weapon/HUD build stamp
- basic DAC/PCM sound output through the common GPIO 26 audio path
- runtime page-cache and sprite optimizations

This is still experimental. The port is optimized for the classic CYD board first, not for compatibility across every ESP32 display module.

## Hardware target

Default wiring targets the common ESP32-2432S028R Cheap Yellow Display layout:

- ESP32-WROOM module
- 320x240 ILI9341 TFT
- XPT2046 resistive touch
- microSD slot
- GPIO 26 DAC audio path
- optional RGB LED on GPIO 4/16/17

CYD clones vary. Board-level settings live in [include/board_config.h](include/board_config.h). TFT bus pins are in [platformio.ini](platformio.ini).

## Game data

Game data is not included.

Copy your legally owned Wolfenstein 3D commercial data files to `/wolf3d/` on a FAT32 SD card:

```text
AUDIOHED.WL6
AUDIOT.WL6
GAMEMAPS.WL6
MAPHEAD.WL6
VGADICT.WL6
VGAGRAPH.WL6
VGAHEAD.WL6
VSWAP.WL6
```

The code still has some fallback support for `.WL1`, but this port is being tuned for the commercial `.WL6` data set.

Do not commit or redistribute Wolfenstein 3D game data, extracted art, extracted sounds, or generated asset caches based on those commercial files.

## Build requirements

Install:

- [Visual Studio Code](https://code.visualstudio.com/) or another editor
- [PlatformIO](https://platformio.org/)
- a USB data cable for the ESP32 board

From the repository root:

```powershell
pio run
```

Upload:

```powershell
pio run -t upload
```

Open the serial monitor:

```powershell
pio device monitor -b 115200
```

If you use a local wrapper such as `pio.ps1`, keep it outside the repo or leave it ignored.

## Firmware targets

```text
cyd_wolf3d      Active Wolf3D CYD port
cyd_esp32       CYD hardware tester
cyd_wolf3d_wl1  Shareware-data fallback target
```

The default PlatformIO environment is `cyd_wolf3d`. Select `cyd_esp32` explicitly when building the hardware tester.

## Build stamp

The Wolf3D HUD displays a four-digit build stamp in the lower-right corner. Change it in [include/board_config.h](include/board_config.h):

```c
#define CYD_WOLF_BUILD_NUMBER 20
```

Build `20` appears as `0020`.

## Diagnostics

Two diagnostic systems are available:

- `CYD_WOLF_ENABLE_PERF_LOGS`
- `CYD_WOLF_RESOURCE_TRACE`

Both are disabled by default for release-style builds. Enable them in [include/board_config.h](include/board_config.h) when profiling frame timing, page-cache misses, sprite activity, and sound requests.

## Hardware tester

The `cyd_esp32` target provides a basic CYD checkout menu for:

- TFT colors and animation
- touch input
- SD mount/write verification
- GPIO 26 audio check
- PWM backlight fade
- chip/flash/heap info
- Wi-Fi scan
- MJPEG/WAV media tests

Build it with:

```powershell
pio run -e cyd_esp32
```

Upload it with:

```powershell
pio run -e cyd_esp32 -t upload
```

The optional media test uses the sample files in [sdcard/](sdcard/). Regenerate them with:

```powershell
python tools/generate_test_media.py
```

## Licensing

This repository contains modified Wolf4SDL/id Software source code and keeps the upstream license files in [lib/Wolf4SDL/src](lib/Wolf4SDL/src):

- [license-gpl.txt](lib/Wolf4SDL/src/license-gpl.txt)
- [license-id.txt](lib/Wolf4SDL/src/license-id.txt)

The engine/source code is distributed under the applicable upstream licenses. Game data is not included and remains the property of its rights holders.

If you distribute a modified build, keep the corresponding source available and do not redistribute commercial Wolfenstein 3D data.

## Development notes

The port currently favors a playable ESP32 profile over PC accuracy:

- reduced memory reserves
- direct SD-backed VSWAP page cache
- tiny hot-page cache for repeated small pages
- static decoration impostors
- fast weapon and sprite scaling paths
- skipped PC hardware setup screens
- skipped intermission screen to avoid memory pressure

Near-term optimization targets:

- dynamic actor impostors
- cleaner physical controls
- optional in-game settings
- improved sound event coverage
- display pipeline tuning
