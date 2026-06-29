# ESP32-CYD Wolf3D

Wolfenstein 3D running on the classic ESP32 Cheap Yellow Display / ESP32-2432S028R style board.

This project started as a hardware tester for CYD clones and grew into a purpose-built ESP32 port of the Wolf4SDL engine. The active target is the full commercial Wolfenstein 3D data set (`.WL6`) on an SD card.

---

## Status

The current Wolf3D build boots directly into gameplay on the CYD and includes:

*   **Landscape ILI9341 display output**
*   **Touch/input scaffolding** (left half of touch screen controls movement, top right opens doors, bottom right fires)
*   **SD-card loading** of Wolf3D assets
*   **Simplified renderer** tuned specifically for ESP32 RAM and CPU limits
*   **Flat-color walls** with selected low-resolution cached texture support
*   **Static decoration impostors** and cache pools
*   **Visible weapon/HUD build stamp**
*   **Basic DAC/PCM sound output** through the common GPIO 26 audio path
*   **Runtime page-cache** and sprite optimizations

---

## Hardware Targets & Pinouts

The project targets two hardware platforms: the **Cheap Yellow Display (CYD)** as the primary game console and an optional **WEMOS Lolin S2 Mini** to drive a secondary circular status face.

### 1. ESP32 Cheap Yellow Display (ESP32-2432S028R)

The primary board uses an ESP32-WROOM module connected to a 320x240 ILI9341 TFT display, an XPT2046 resistive touch controller, a microSD slot, and a GPIO 26 audio path.

#### TFT Display (SPI)
| Function | GPIO Pin | Description |
|---|---|---|
| **TFT_MISO** | GPIO 12 | SPI Master In Slave Out |
| **TFT_MOSI** | GPIO 13 | SPI Master Out Slave In |
| **TFT_SCLK** | GPIO 14 | SPI Clock |
| **TFT_CS** | GPIO 15 | Chip Select (Active Low) |
| **TFT_DC** | GPIO 2 | Data / Command Select |
| **TFT_RST** | *None* (-1) | Connected to EN hardware reset line |
| **TFT_BL** | GPIO 21 | Backlight control (Active High) |

#### Resistive Touch Controller (XPT2046 SPI)
| Function | GPIO Pin | Description |
|---|---|---|
| **TOUCH_MOSI** | GPIO 32 | Touch SPI MOSI |
| **TOUCH_MISO** | GPIO 39 | Touch SPI MISO |
| **TOUCH_SCLK** | GPIO 25 | Touch SPI Clock |
| **TOUCH_CS** | GPIO 33 | Touch Chip Select |
| **TOUCH_IRQ** | GPIO 36 | Touch Interrupt Request |

#### microSD Card Slot (SPI)
| Function | GPIO Pin | Description |
|---|---|---|
| **SD_MOSI** | GPIO 23 | SD SPI MOSI |
| **SD_MISO** | GPIO 19 | SD SPI MISO |
| **SD_SCLK** | GPIO 18 | SD SPI Clock |
| **SD_CS** | GPIO 5 | SD Chip Select |

#### Audio & Onboard Peripherals
| Function | GPIO Pin | Description |
|---|---|---|
| **AUDIO_DAC** | GPIO 26 | Mono audio DAC / PCM output channel |
| **RGB Red** | GPIO 4 | Onboard RGB LED - Red channel (Active Low) |
| **RGB Green**| GPIO 16 | Onboard RGB LED - Green channel (Active Low) |
| **RGB Blue** | GPIO 17 | Onboard RGB LED - Blue channel (Active Low) |

#### I2C & Serial Expansions
| Function | GPIO Pin | Description |
|---|---|---|
| **I2C SDA** | GPIO 27 | I2C Data (MCP23017 port expander) |
| **I2C SCL** | GPIO 22 | I2C Clock (MCP23017 port expander) |
| **CYD_S2_TX** | GPIO 1 | Serial TX to S2 Face Display (Primary Serial TX0 on programming port P1) |
| **CYD_S2_RX** | -1 (None) | Unused (transmit-only stream) |

> [!IMPORTANT]
> **Solder-Free Conflict Resolution:**
> To avoid pin conflicts with the **MCP23017** port expander on I2C pins GPIO 27 & 22, the face display uses the primary serial **TX0 (GPIO 1)** pin:
> * Connect the **Lolin S2 Mini's RX pin (GPIO 18)** wire to the **TX pin (GPIO 1)** of the **programming port (P1)** JST header.
> * Both console logs and binary face packets share this TX line at **460800 baud** without interfering with each other.

#### MCP23017 Port Expander Button Mappings
When `CYD_MCP23017_ENABLE` is set to `1` in `board_config.h`, the buttons are mapped as follows:

| MCP23017 Pin | Game Function | SDL Key Code | Notes |
|---|---|---|---|
| **PA0** | Move Up | `SDLK_UP` | Directional Pad |
| **PA1** | Move Down | `SDLK_DOWN` | Directional Pad |
| **PA2** | Turn Left | `SDLK_LEFT` | Directional Pad |
| **PA3** | Turn Right | `SDLK_RIGHT` | Directional Pad |
| **PA4** | Unused | - | |
| **PA5** | Select | `SDLK_PAUSE` | Binds to Game Pause |
| **PA6** | L1 | - | Unused |
| **PA7** | L2 | - | Unused |
| **PB0** | R2 | - | Unused |
| **PB1** | R1 | - | Unused |
| **PB2** | Enter | `SDLK_RETURN` | Menu confirmation |
| **PB3** | Start | `SDLK_ESCAPE` | Binds to Menu/Escape |
| **PB4** | Upper Action | `SDLK_LALT` | Strafe / Side-step |
| **PB5** | Lower Action | `SDLK_LSHIFT`| Run / Speed |
| **PB6** | Right Action | `SDLK_SPACE`  | Open / Use door |
| **PB7** | Left Action | `SDLK_LCTRL`  | Fire / Shoot |

---

### 2. WEMOS Lolin S2 Mini (Secondary Status Face Display)

This optional sub-system runs on a WEMOS Lolin S2 Mini connected to a circular GC9A01 LCD. It offloads B.J. Blazkowicz's status-bar face from the primary CYD to maximize rendering performance.

#### GC9A01 Circular LCD Pinout (SPI)
| Function | GPIO Pin | Description |
|---|---|---|
| **TFT_MISO** | *None* (-1) | Unused for display write path |
| **TFT_MOSI** | GPIO 11 | SPI MOSI |
| **TFT_SCLK** | GPIO 12 | SPI Clock |
| **TFT_CS** | GPIO 10 | Chip Select |
| **TFT_DC** | GPIO 14 | Data / Command Select |
| **TFT_RST** | GPIO 15 | Hardware Reset |
| **TFT_BL** | GPIO 13 | Backlight control (Active High) |

#### Serial Interface (to CYD CN1 Header)
| Function | GPIO Pin | Connection on CYD Board |
|---|---|---|
| **RX_PIN** | GPIO 18 | Connects to CYD TX (GPIO 22, or GPIO 17 if reconfigured) |
| **TX_PIN** | GPIO 17 | Connects to CYD RX (GPIO 27) |
| **5V / VIN** | 5V | Connects to CYD 5V/VIN |
| **GND** | GND | Connects to CYD GND |

---

## Build & Upload Instructions

### Prerequisites
1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Install the [PlatformIO](https://platformio.org/) extension.
3. Obtain a micro-USB (for CYD) or USB-C (for Lolin S2 Mini) data cable.

### Firmware Targets (Environments)
Select or build specific targets using PlatformIO:

| Target Name | Description |
|---|---|
| `cyd_wolf3d` | **Default.** Primary Wolf3D game binary for Cheap Yellow Display. |
| `cyd_wolf3d_wl1`| Shareware fallback target (using `.WL1` data files). |
| `cyd_esp32` | Diagnostic hardware tester firmware for Cheap Yellow Display. |
| `lolin_s2_mini` | Status face animation firmware for Lolin S2 Mini + GC9A01. |

### Compilation and Flashing Commands

Open a terminal at the repository root and use the following PlatformIO Core CLI commands:

#### Build default target (`cyd_wolf3d`):
```powershell
.\pio.ps1 run
```

#### Upload default target to CYD:
```powershell
.\pio.ps1 run -t upload
```

#### Build and Upload a specific target:
```powershell
# Flash the diagnostic Hardware Tester to the CYD
.\pio.ps1 run -e cyd_esp32 -t upload

# Flash the secondary status face firmware to the Lolin S2 Mini
.\pio.ps1 run -e lolin_s2_mini -t upload
```

#### Launch Serial Monitor:
```powershell
.\pio.ps1 device monitor -b 460800
```

---

## Game Data Setup

Copy legally owned commercial Wolfenstein 3D data files into a folder named `/wolf3d/` at the root of a FAT32-formatted microSD card:

```text
SD CARD ROOT
└── wolf3d
    ├── AUDIOHED.WL6
    ├── AUDIOT.WL6
    ├── GAMEMAPS.WL6
    ├── MAPHEAD.WL6
    ├── VGADICT.WL6
    ├── VGAGRAPH.WL6
    ├── VGAHEAD.WL6
    └── VSWAP.WL6
```

---

## Major Compiler & Configuration Flags

Configure performance adjustments and features in [include/board_config.h](include/board_config.h) or [platformio.ini](platformio.ini).

### Core Engine & Display settings
*   `WOLF3D_CYD_PORT`: Flags the build environment to target CYD hardware routines.
*   `CYD_WOLF_BUILD_NUMBER`: Changes the version number printed in the bottom right corner of the HUD (e.g. `24` prints as `0024`).
*   `CYD_WOLF_VIEW_SIZE`: Adjusts rendering window scale (default: `20`).
*   `CYD_WOLF_SKIP_BOOT_SCREENS`: Bypasses long intro animations to load directly to gameplay (default: `1`).

### Renderer & Cache Tweaks
*   `CYD_WOLF_FLAT_WALLS`: Disables vertical texturing on walls, rendering solid colors to keep frame rates playable (default: `1`).
*   `CYD_WOLF_WALL_TEXTURE_CACHE`: Caches specific wall textures dynamically in RAM (default: `1`).
*   `CYD_WOLF_DRAW_SPRITES`: Enables/disables rendering enemies, hazards, and pick-ups (default: `1`).
*   `CYD_WOLF_DRAW_WEAPON`: Enables/disables rendering of B.J.'s weapon animations (default: `1`).
*   `CYD_WOLF_DRAW_STATUSBAR_ART`: Enables/disables local status bar drawing (default: `0` to optimize memory and offload B.J.'s face to the Lolin S2 Mini).
*   `CYD_WOLF_FAST_SPRITES`: Employs an optimized sprite rendering routine (default: `1`).
*   `CYD_WOLF_STATIC_DECOR_IMPOSTORS`: Replaces distant 3D static sprites with basic flat impostors to reduce processor load (default: `1`).
*   `CYD_WOLF_HOT_PAGE_CACHE`: Caches active memory sectors from the SD card to prevent micro-stuttering (default: `1`).

### Port Expander & Peripherals
*   `CYD_RGB_LED_ENABLE`: Enables RGB LED status displays during gameplay (default: `1`).
*   `CYD_MCP23017_ENABLE`: Enables I2C connectivity for an MCP23017 port expander to map physical controller buttons (default: `1`).
*   `CYD_WOLF_BASIC_SOUND`: Enables basic low-overhead sound effects through GPIO 26 (default: `1`).

### Debugging & Profiling (Disabled by default)
*   `CYD_WOLF_ENABLE_PERF_LOGS`: Emits frame time statistics to the serial terminal.
*   `CYD_WOLF_RESOURCE_TRACE`: Emits memory reports and SD read latency checks.

---

## Licensing

This repository contains modified Wolf4SDL and id Software source code. Original licenses remain in [lib/Wolf4SDL/src](lib/Wolf4SDL/src):
*   [license-gpl.txt](lib/Wolf4SDL/src/license-gpl.txt)
*   [license-id.txt](lib/Wolf4SDL/src/license-id.txt)

*Note: Wolfenstein 3D commercial assets are the property of their respective owners. Do not commit or distribute game data, extracted sound effects, or sprite caches.*
