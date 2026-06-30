# ESP32-CYD Wolf3D

Wolfenstein 3D running on the ESP32 Cheap Yellow Display, also sold as the
ESP32-2432S028R style board.

This is a small-hardware port built around Wolf4SDL and the CYD's 320x240
ILI9341 display, microSD slot, touch controller, and GPIO 26 DAC output. The
default build targets the commercial `.WL6` data set, with a `.WL1` shareware
build available as a separate PlatformIO environment.

This repository contains source code only. It does not include Wolfenstein 3D
game data, sprites, sounds, maps, or other commercial assets.

## Current Features

- Landscape ILI9341 display output on the CYD.
- SD-card loading from `/wolf3d/`.
- Playable renderer profile with DMA presentation, low-resolution wall texture
  cache, actor sprites, static decoration handling, downsampled weapon graphics,
  and streamed face graphics.
- Touch controls for movement, use, and fire.
- Optional MCP23017 I2C button controller.
- GPIO 26 sound effects output using the ESP32 DAC.
- Optional WEMOS Lolin S2 Mini + GC9A01 circular display for the status face.
- Diagnostic CYD hardware tester firmware.

## Hardware

### Primary Console

- ESP32 Cheap Yellow Display / ESP32-2432S028R style board.
- FAT32 microSD card.
- Speaker or amplifier connected to the board's GPIO 26 DAC audio path.
- Optional MCP23017-based controller on I2C.

### Optional Face Display

- WEMOS Lolin S2 Mini.
- 240x240 GC9A01 circular LCD.
- Serial connection from the CYD TX pin to the S2 Mini RX pin.

## Game Data

Copy legally owned Wolfenstein 3D data files to a folder named `wolf3d` at the
root of a FAT32-formatted microSD card.

For the commercial `.WL6` build:

```text
SD card root:
wolf3d/
  AUDIOHED.WL6
  AUDIOT.WL6
  GAMEMAPS.WL6
  MAPHEAD.WL6
  VGADICT.WL6
  VGAGRAPH.WL6
  VGAHEAD.WL6
  VSWAP.WL6
```

For the shareware build, use the same file names with the `.WL1` extension and
build the `cyd_wolf3d_wl1` environment.

## Build And Flash

Install PlatformIO with either the VS Code extension or the PlatformIO Core CLI.
From the repository root, build and upload the default CYD game firmware:

```powershell
pio run -e cyd_wolf3d
pio run -e cyd_wolf3d -t upload
```

If PlatformIO does not pick the correct serial port automatically, pass it
explicitly:

```powershell
pio run -e cyd_wolf3d -t upload --upload-port COMx
```

Replace `COMx` with the port for your board.

Useful environments:

| Environment | Purpose |
| --- | --- |
| `cyd_wolf3d` | Main CYD Wolf3D firmware for `.WL6` data. |
| `cyd_wolf3d_wl1` | CYD Wolf3D firmware for `.WL1` shareware data. |
| `cyd_esp32` | CYD hardware tester firmware. |
| `lolin_s2_mini` | Optional S2 Mini status-face display firmware. |

Serial monitor examples:

```powershell
pio device monitor -b 460800
pio device monitor -e lolin_s2_mini -b 115200
```

## Controls

### Touch Screen

The default touch layout is split by screen region:

| Region | Action |
| --- | --- |
| Left half, upper third | Move forward |
| Left half, lower third | Move backward |
| Left half, center-left | Turn left |
| Left half, center-right | Turn right |
| Right half, upper half | Open / use |
| Right half, lower half | Fire |

### MCP23017 Controller

The optional MCP23017 controller uses active-low buttons with pull-ups enabled.
Set `CYD_MCP23017_ENABLE` in [include/board_config.h](include/board_config.h) to
enable or disable this input path.

| MCP23017 pin | Game action |
| --- | --- |
| PA0 | Move forward |
| PA1 | Move backward |
| PA2 | Turn left |
| PA3 | Turn right |
| PA5 | Pause |
| PB2 | Enter / menu confirm |
| PB3 | Escape / menu |
| PB4 | Strafe |
| PB5 | Run |
| PB6 | Open / use |
| PB7 | Fire |

## Pin Reference

### CYD Display

| Function | GPIO |
| --- | --- |
| TFT_MISO | 12 |
| TFT_MOSI | 13 |
| TFT_SCLK | 14 |
| TFT_CS | 15 |
| TFT_DC | 2 |
| TFT_RST | -1 / board reset |
| TFT_BL | 21 |

### CYD Touch

| Function | GPIO |
| --- | --- |
| TOUCH_MOSI | 32 |
| TOUCH_MISO | 39 |
| TOUCH_SCLK | 25 |
| TOUCH_CS | 33 |
| TOUCH_IRQ | 36 |

### CYD microSD

| Function | GPIO |
| --- | --- |
| SD_MOSI | 23 |
| SD_MISO | 19 |
| SD_SCLK | 18 |
| SD_CS | 5 |

### CYD Audio And Expansion

| Function | GPIO |
| --- | --- |
| Audio DAC | 26 |
| MCP23017 SDA | 27 |
| MCP23017 SCL | 22 |
| Face display serial TX | 1 |

### Lolin S2 Mini Face Display

| Function | GPIO |
| --- | --- |
| GC9A01 MOSI | 11 |
| GC9A01 SCLK | 12 |
| GC9A01 CS | 10 |
| GC9A01 DC | 14 |
| GC9A01 RST | 15 |
| GC9A01 BL | 13 |
| Serial RX from CYD GPIO 1 | 18 |

The CYD sends face packets over its primary TX0 line at 460800 baud. If you use
the optional face display, connect CYD GPIO 1 / P1 TX to S2 Mini GPIO 18 / RX.

## Configuration

Most firmware options are in [include/board_config.h](include/board_config.h).
PlatformIO build-time flags are in [platformio.ini](platformio.ini).

Common options:

| Option | Purpose |
| --- | --- |
| `CYD_WOLF_BUILD_NUMBER` | Build number shown on the HUD. |
| `CYD_WOLF_VIEW_SIZE` | Wolf3D view size. The current profile keeps this at `20`. |
| `CYD_WOLF_USE_DMA_PRESENT` | Enables DMA-assisted TFT presentation. |
| `CYD_WOLF_LOW_RES_WALL_TEXTURES` | Uses the reduced wall texture cache needed for playable speed. |
| `CYD_WOLF_DRAW_SPRITES` | Enables actors, items, and decorations. |
| `CYD_WOLF_DOWNSAMPLED_WEAPON` | Reduces weapon sprite cost. |
| `CYD_S2_FACE_ENABLE` | Streams the status face to the optional S2 Mini display. |
| `CYD_MCP23017_ENABLE` | Enables the I2C controller expander. |
| `CYD_WOLF_BASIC_SOUND` | Enables low-overhead sound effects. |

## Troubleshooting

### The board says game data is missing

Check that the SD card is FAT32, that the folder is named `wolf3d`, and that all
required files use the same extension as the firmware you built.

### Upload fails

Try a known data-capable USB cable, hold the board's boot button during the
connection phase if needed, and pass `--upload-port` with the correct serial
port.

### The game runs but has no sound

Confirm that your CYD variant exposes the GPIO 26 DAC audio path and that
`CYD_WOLF_BASIC_SOUND` is enabled.

### The face display does not update

Confirm that the S2 Mini is flashed with the `lolin_s2_mini` environment and
that CYD GPIO 1 / P1 TX is connected to S2 Mini GPIO 18 / RX. Both boards need a
common ground.

### Performance is poor

The current renderer is tuned around the default flags in
[platformio.ini](platformio.ini) and [include/board_config.h](include/board_config.h).
Increasing wall texture resolution, sprite quality, or local status-bar art can
quickly overrun the ESP32's RAM and CPU budget.

## Repository Layout

| Path | Contents |
| --- | --- |
| `src/wolf3d/` | ESP32/CYD platform layer for the game firmware. |
| `src/s2_face/` | Optional S2 Mini face-display firmware. |
| `src/main.cpp` | CYD diagnostic hardware tester. |
| `include/` | Board configuration. |
| `lib/Wolf4SDL/` | Modified Wolf4SDL engine source. |
| `sdcard/` | Test media used by the diagnostic firmware. |
| `tools/` | Utility scripts for generated test media. |

## Privacy And Assets

Do not commit local PlatformIO build directories, serial logs, personal helper
scripts, or machine-specific launchers. The repository ignores the usual local
build products, `scratch/`, and local PowerShell wrappers.

Do not commit commercial Wolfenstein 3D data files, extracted sprites, extracted
sound effects, or generated game caches.

## License

This repository contains modified Wolf4SDL and id Software source code. Original
license files remain in [lib/Wolf4SDL/src](lib/Wolf4SDL/src):

- [GPL license](lib/Wolf4SDL/src/license-gpl.txt)
- [id Software license](lib/Wolf4SDL/src/license-id.txt)

Wolfenstein 3D commercial assets are owned by their respective rights holders
and are not distributed here.
