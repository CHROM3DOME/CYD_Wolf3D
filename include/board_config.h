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
#define CYD_WOLF_VIEW_SIZE 10

// Raw XPT2046 endpoints. Run the Touch test and adjust if taps are mirrored
// or do not reach the screen edges.
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3900
#define TOUCH_MIN_Y 200
#define TOUCH_MAX_Y 3900
#define TOUCH_SWAP_XY 1
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 0
