#include <stdint.h>

#include <SDL.h>

#undef read
#undef write
#undef open
#undef close
#undef lseek
#undef stat
#undef unlink
#undef mkdir

#include "wl_def.h"

#if CYD_MCP23017_ENABLE
#include <Wire.h>
#endif

#ifndef CYD_BOOT_BUTTON_PIN
#define CYD_BOOT_BUTTON_PIN 0
#endif

class HardwareSerial {
public:
  int available(void);
  int read(void);
};

extern HardwareSerial Serial;
extern "C" int digitalRead(uint8_t pin);
extern "C" void pinMode(uint8_t pin, uint8_t mode);
extern "C" void cyd_native_set_key(int32_t key, bool down);
extern "C" void cyd_poll_touch_controls(void);

namespace {
bool heldKeys[256] = {};
bool bootButtonReady = false;

#ifndef INPUT_PULLUP
#define INPUT_PULLUP 5
#endif

uint8_t heldIndexFor(int32_t key) {
  return static_cast<uint8_t>(key & 0xff);
}

char asciiForKey(int32_t key) {
  switch (key) {
    case SDLK_RETURN:
      return '\r';
    case SDLK_SPACE:
      return ' ';
    default:
      if (key >= SDLK_a && key <= SDLK_z) return static_cast<char>(key);
      if (key >= SDLK_0 && key <= SDLK_9) return static_cast<char>(key);
      return 0;
  }
}

int32_t serialCharToKey(int ch) {
  switch (ch) {
    case '\r':
    case '\n':
      return SDLK_RETURN;
    case ' ':
    case 'e':
    case 'E':
      return SDLK_SPACE;
    case 'q':
    case 'Q':
      return SDLK_UNKNOWN;
    case 'w':
    case 'W':
      return SDLK_UP;
    case 's':
    case 'S':
      return SDLK_DOWN;
    case 'a':
    case 'A':
      return SDLK_LEFT;
    case 'd':
    case 'D':
      return SDLK_RIGHT;
    case 'f':
    case 'F':
      return SDLK_LCTRL;
    case 'r':
    case 'R':
      return SDLK_LALT;
    default:
      return SDLK_UNKNOWN;
  }
}

bool isReleaseChar(int ch) {
  return ch >= 'A' && ch <= 'Z';
}

void pumpSerialControls() {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    const int32_t key = serialCharToKey(ch);
    if (key == SDLK_UNKNOWN) continue;
    const bool release = isReleaseChar(ch);
    cyd_native_set_key(key, !release);
  }
}

void pollBootButton() {
  if (!bootButtonReady) {
    pinMode(CYD_BOOT_BUTTON_PIN, INPUT_PULLUP);
    bootButtonReady = true;
  }
  cyd_native_set_key(SDLK_ESCAPE, digitalRead(CYD_BOOT_BUTTON_PIN) == 0);
}
}

extern "C" void cyd_native_set_key(int32_t key, bool down) {
  if (key == SDLK_UNKNOWN) return;

  const uint8_t index = heldIndexFor(key);
  if (heldKeys[index] == down) return;
  heldKeys[index] = down;

  Keyboard[key] = down ? true : false;
  if (down) {
    LastScan = key;
    printf("cyd_native_set_key: key %d down, LastScan=%d\n", (int)key, (int)LastScan);
    const char ascii = asciiForKey(key);
    if (ascii) LastASCII = ascii;
    if (key == SDLK_PAUSE) Paused = true;
  } else if (LastScan == key) {
    LastScan = sc_None;
  }
}

extern "C" void cyd_native_clear_keys(void) {
  for (int i = 0; i < 256; ++i) heldKeys[i] = false;
}

#if CYD_MCP23017_ENABLE
namespace {
bool mcpInitialized = false;

void initMCP23017() {
  if (mcpInitialized) return;
  Wire.begin(CYD_MCP23017_SDA, CYD_MCP23017_SCL);
  
  // Set all Port A pins as inputs
  Wire.beginTransmission(0x20);
  Wire.write(0x00); // IODIRA
  Wire.write(0xFF);
  Wire.endTransmission();
  
  // Set all Port B pins as inputs
  Wire.beginTransmission(0x20);
  Wire.write(0x01); // IODIRB
  Wire.write(0xFF);
  Wire.endTransmission();
  
  // Enable pull-ups on Port A
  Wire.beginTransmission(0x20);
  Wire.write(0x0C); // GPPUA
  Wire.write(0xFF);
  Wire.endTransmission();
  
  // Enable pull-ups on Port B
  Wire.beginTransmission(0x20);
  Wire.write(0x0D); // GPPUB
  Wire.write(0xFF);
  Wire.endTransmission();
  
  mcpInitialized = true;
}

void cyd_poll_mcp23017_buttons() {
  initMCP23017();
  
  Wire.beginTransmission(0x20);
  Wire.write(0x12); // Start reading from GPIOA (0x12)
  if (Wire.endTransmission() != 0) {
    return; // I2C communication error, skip this frame
  }
  
  Wire.requestFrom(0x20, 2);
  if (Wire.available() < 2) return;
  
  uint8_t gpioa = Wire.read();
  uint8_t gpiob = Wire.read();
  
  // Active-low buttons: invert bits so 1 = pressed, 0 = released
  uint16_t pins = ~(gpioa | (gpiob << 8));
  static uint32_t lastPrintTime = 0;
  if (millis() - lastPrintTime > 1000 && pins != 0) {
    printf("I2C read: A=0x%02X B=0x%02X, pins=0x%04X\n", gpioa, gpiob, pins);
    lastPrintTime = millis();
  }
  
  // Map Port A pins
  cyd_native_set_key(SDLK_UP,     (pins & (1 << 0)) != 0);   // PA0 -> Up
  cyd_native_set_key(SDLK_DOWN,   (pins & (1 << 1)) != 0);   // PA1 -> Down
  cyd_native_set_key(SDLK_LEFT,   (pins & (1 << 2)) != 0);   // PA2 -> Left
  cyd_native_set_key(SDLK_RIGHT,  (pins & (1 << 3)) != 0);   // PA3 -> Right
  // PA4 -> Unused
  cyd_native_set_key(SDLK_PAUSE,  (pins & (1 << 5)) != 0);   // PA5 -> Select (Pause)
  // PA6 (L1) -> Unused
  // PA7 (L2) -> Unused
  
  // Map Port B pins
  // PB0 (R2) -> Unused
  // PB1 (R1) -> Unused
  cyd_native_set_key(SDLK_RETURN, (pins & (1 << 10)) != 0);  // PB2 -> Enter (keep)
  cyd_native_set_key(SDLK_ESCAPE, (pins & (1 << 11)) != 0);  // PB3 -> Start (Escape)
  cyd_native_set_key(SDLK_LALT,   (pins & (1 << 12)) != 0);  // PB4 -> Upper (Strafe)
  cyd_native_set_key(SDLK_LSHIFT, (pins & (1 << 13)) != 0);  // PB5 -> Lower (Run)
  // Temporarily comment out PB6 to isolate stuck input
  // cyd_native_set_key(SDLK_SPACE,  (pins & (1 << 14)) != 0);  // PB6 -> Right (Open/Use)
  cyd_native_set_key(SDLK_LCTRL,  (pins & (1 << 15)) != 0);  // PB7 -> Left (Fire)
}
}
#endif

extern "C" void cyd_native_poll_input(void) {
  pumpSerialControls();
  pollBootButton();
  cyd_poll_touch_controls();
#if CYD_MCP23017_ENABLE
  cyd_poll_mcp23017_buttons();
#endif
}
