# ESP32-CYD-Tester (Alpha)

A simple hardware checkout program for the Elegoo/CYD-style ESP32 display board. It provides a landscape touch menu and matching Serial commands for:

- TFT colors and animation speed
- XPT2046 touch drawing
- SD mount, capacity, write speed, and byte-for-byte verification
- three-tone audio check through the common GPIO 26 DAC output
- PWM backlight fade
- chip, flash, heap, and PSRAM information
- Wi-Fi network scan
- SD-based MJPEG video decode and WAV playback
- automatic run of every test

## Build and upload

Install [PlatformIO Core](https://platformio.org/install/cli), connect the board, then run:

```text
pio run -t upload
pio device monitor
```

The display also works without a computer after programming. Tap a test in the menu. In the Serial monitor, use `d`, `t`, `s`, `a`, `b`, `m`, `w`, `v` (media), or `r` (run all).

## SD media test

Copy `sdcard/test.mjpeg` and `sdcard/test.wav` to the root of a FAT32 SD card, then tap **MEDIA**. The test renders 50 JPEG frames, reports decode FPS and dropped frames, and plays a 22.05 kHz mono PCM WAV through GPIO 26 and the onboard amplifier.

Regenerate both files with:

```text
python tools/generate_test_media.py
```

## Important: confirm the board revision

The defaults target the common **ESP32-2432S028R** wiring. CYD clones are not fully standardized. All touch, SD, audio, and backlight settings are in `include/board_config.h`; TFT bus pins are build flags in `platformio.ini`. The TFT uses HSPI; touch and SD safely time-share VSPI because their pin sets differ.

If the display is blank, compare the silkscreen/model number and schematic with the pin table before changing code. If touch is mirrored or offset, adjust `TOUCH_SWAP_XY`, `TOUCH_INVERT_X/Y`, and the four raw endpoints in `board_config.h`.

The audio test emits analog audio on GPIO 26. It is only audible when that pin is connected to the board's amplifier/speaker path. Do not drive a low-impedance speaker directly from the ESP32 pin.
