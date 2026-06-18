#include <Arduino.h>

#include <SDL.h>

#undef read
#undef write
#undef open
#undef close
#undef lseek
#undef stat
#undef unlink
#undef mkdir

#include "board_config.h"

extern "C" void cyd_native_set_key(int32_t key, bool down);

namespace {
bool initialized = false;
bool lastUp = false;
bool lastDown = false;
bool lastLeft = false;
bool lastRight = false;
bool lastUse = false;
bool lastFire = false;

void touchClockPulse() {
  digitalWrite(TOUCH_SCLK, HIGH);
  delayMicroseconds(1);
  digitalWrite(TOUCH_SCLK, LOW);
  delayMicroseconds(1);
}

void touchWriteByte(uint8_t value) {
  for (int bit = 7; bit >= 0; --bit) {
    digitalWrite(TOUCH_MOSI, (value & (1 << bit)) ? HIGH : LOW);
    touchClockPulse();
  }
}

uint16_t touchReadWord() {
  uint16_t value = 0;
  for (int bit = 15; bit >= 0; --bit) {
    digitalWrite(TOUCH_SCLK, HIGH);
    delayMicroseconds(1);
    if (digitalRead(TOUCH_MISO)) value |= (1U << bit);
    digitalWrite(TOUCH_SCLK, LOW);
    delayMicroseconds(1);
  }
  return value;
}

uint16_t readTouchAxis(uint8_t command) {
  touchWriteByte(command);
  uint16_t value = touchReadWord();
  return value >> 3;
}

uint16_t readAverageAxis(uint8_t command) {
  uint32_t sum = 0;
  for (int i = 0; i < 2; ++i) sum += readTouchAxis(command);
  return static_cast<uint16_t>(sum / 2);
}

void beginTouch() {
  if (initialized) return;
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  pinMode(TOUCH_SCLK, OUTPUT);
  digitalWrite(TOUCH_SCLK, LOW);
  pinMode(TOUCH_MOSI, OUTPUT);
  digitalWrite(TOUCH_MOSI, LOW);
  pinMode(TOUCH_MISO, INPUT);
  pinMode(TOUCH_IRQ, INPUT);
  initialized = true;
}

bool readTouchPoint(int16_t &screenX, int16_t &screenY) {
  beginTouch();
  if (digitalRead(TOUCH_IRQ) != LOW) {
    return false;
  }

  digitalWrite(TOUCH_CS, LOW);
  delayMicroseconds(5);

  uint16_t rawX = readAverageAxis(0xD0);
  uint16_t rawY = readAverageAxis(0x90);

  digitalWrite(TOUCH_CS, HIGH);

#if TOUCH_SWAP_XY
  uint16_t swapped = rawX;
  rawX = rawY;
  rawY = swapped;
#endif

  long mappedX = map(rawX, TOUCH_MIN_X, TOUCH_MAX_X, 0, 319);
  long mappedY = map(rawY, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, 239);

#if TOUCH_INVERT_X
  mappedX = 319 - mappedX;
#endif
#if TOUCH_INVERT_Y
  mappedY = 239 - mappedY;
#endif

  screenX = static_cast<int16_t>(constrain(mappedX, 0L, 319L));
  screenY = static_cast<int16_t>(constrain(mappedY, 0L, 239L));
  return true;
}

void setTouchKeys(bool up, bool down, bool left, bool right, bool use, bool fire) {
  if (up != lastUp) cyd_native_set_key(SDLK_UP, up);
  if (down != lastDown) cyd_native_set_key(SDLK_DOWN, down);
  if (left != lastLeft) cyd_native_set_key(SDLK_LEFT, left);
  if (right != lastRight) cyd_native_set_key(SDLK_RIGHT, right);
  if (use != lastUse) cyd_native_set_key(SDLK_SPACE, use);
  if (fire != lastFire) cyd_native_set_key(SDLK_LCTRL, fire);

  lastUp = up;
  lastDown = down;
  lastLeft = left;
  lastRight = right;
  lastUse = use;
  lastFire = fire;
}
}

extern "C" void cyd_poll_touch_controls(void) {
  int16_t x = 0;
  int16_t y = 0;
  if (!readTouchPoint(x, y)) {
    setTouchKeys(false, false, false, false, false, false);
    return;
  }

  bool up = false;
  bool down = false;
  bool left = false;
  bool right = false;
  bool use = false;
  bool fire = false;

  if (x < 160) {
    if (y < 80) {
      up = true;
    } else if (y > 160) {
      down = true;
    } else if (x < 80) {
      left = true;
    } else {
      right = true;
    }
  } else {
    if (y < 120) {
      use = true;
    } else {
      fire = true;
    }
  }

  setTouchKeys(up, down, left, right, use, fire);
}
