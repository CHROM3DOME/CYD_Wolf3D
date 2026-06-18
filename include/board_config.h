#pragma once

// Defaults for the common ESP32-2432S028R ("Cheap Yellow Display") layout.
// Elegoo revisions may differ: change only this file and the matching TFT pins
// in platformio.ini when adapting another board revision.

#define DISPLAY_ROTATION 1
#define TFT_BACKLIGHT_PIN 21

#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_SCLK 25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32

#define SD_CS 5
#define SD_SCLK 18
#define SD_MISO 19
#define SD_MOSI 23

// The usual CYD audio connection is ESP32 DAC channel 2 on GPIO 26.
#define AUDIO_DAC_PIN 26

// Wolf3D performance profile. 20 is the classic large play view with HUD;
// smaller values draw fewer wall columns and are much faster on the ESP32.
#define CYD_WOLF_VIEW_SIZE 20

// Skip Wolf's PC hardware/config sign-on screens for appliance-style boot.
#define CYD_WOLF_SKIP_BOOT_SCREENS 1

// Performance toggles. Sprites should stay on for playable builds; the weapon
// overlay is expensive and cosmetic, so keep it off on the ESP32.
#define CYD_WOLF_DRAW_SPRITES 1
#define CYD_WOLF_DRAW_WEAPON 0

// Flat walls trade texture art for speed. This is the current CYD speed mode.
#define CYD_WOLF_FLAT_WALLS 1

// Large sprites are expensive. Draw them at half horizontal detail and
// duplicate columns to keep silhouettes readable.
#define CYD_WOLF_FAST_SPRITES 1
#define CYD_WOLF_FAST_SPRITE_MIN_HEIGHT 96
#define CYD_WOLF_SPRITE_BUDGET_US 0

// Quiet field build by default. Set these to 1 when collecting serial diagnostics.
#define CYD_WOLF_ENABLE_PERF_LOGS 0
#define CYD_WOLF_ENABLE_FRAME_HEARTBEAT 0

// Raw XPT2046 endpoints. Run the Touch test and adjust if taps are mirrored
// or do not reach the screen edges.
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3900
#define TOUCH_MIN_Y 200
#define TOUCH_MAX_Y 3900
#define TOUCH_SWAP_XY 1
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 0
