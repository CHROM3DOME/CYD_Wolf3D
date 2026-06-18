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

class HardwareSerial {
public:
  int available(void);
  int read(void);
};

extern HardwareSerial Serial;
extern "C" void cyd_native_set_key(int32_t key, bool down);
extern "C" void cyd_poll_touch_controls(void);

namespace {
bool heldKeys[256] = {};

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
}

extern "C" void cyd_native_set_key(int32_t key, bool down) {
  if (key == SDLK_UNKNOWN) return;

  const uint8_t index = heldIndexFor(key);
  if (heldKeys[index] == down) return;
  heldKeys[index] = down;

  Keyboard[key] = down ? true : false;
  if (down) {
    LastScan = key;
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

extern "C" void cyd_native_poll_input(void) {
  pumpSerialControls();
  cyd_poll_touch_controls();
}
