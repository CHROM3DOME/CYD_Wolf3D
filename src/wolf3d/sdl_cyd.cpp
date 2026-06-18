#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <SDL.h>

#undef read
#undef write
#undef open
#undef close
#undef lseek
#undef stat
#undef unlink
#undef mkdir

extern SDL_Surface *screenBuffer;
extern "C" void cyd_set_palette(const uint8_t *rgb, int first, int count);
extern "C" void cyd_present_indexed(const uint8_t *pixels, int width, int height, int pitch);

struct SDL_mutex { SemaphoreHandle_t handle; };
struct SDL_Window {};
struct SDL_Renderer {};
struct SDL_Texture {};
struct SDL_Joystick {};

static SDL_Window windowObject;
static SDL_Renderer rendererObject;
static SDL_Texture textureObject;

namespace {
constexpr int eventQueueSize = 16;
SDL_Event eventQueue[eventQueueSize];
int eventHead = 0;
int eventTail = 0;
bool heldKeys[256] = {};

bool queueIsEmpty() {
  return eventHead == eventTail;
}

bool queueIsFull() {
  return ((eventTail + 1) % eventQueueSize) == eventHead;
}

bool popEvent(SDL_Event *event) {
  if (queueIsEmpty()) return false;
  if (event) *event = eventQueue[eventHead];
  eventHead = (eventHead + 1) % eventQueueSize;
  return true;
}

void pushKeyEvent(uint32_t type, int32_t key) {
  if (queueIsFull()) return;
  SDL_Event event = {};
  event.type = type;
  event.key.type = type;
  event.key.timestamp = millis();
  event.key.windowID = 1;
  event.key.keysym.sym = key;
  event.key.keysym.scancode = key;
  eventQueue[eventTail] = event;
  eventTail = (eventTail + 1) % eventQueueSize;
}

void setHeldKey(int32_t key, bool down) {
  if (key == SDLK_UNKNOWN) return;
  uint8_t heldIndex = static_cast<uint8_t>(key & 0xff);
  if (heldKeys[heldIndex] == down) return;
  pushKeyEvent(down ? SDL_KEYDOWN : SDL_KEYUP, key);
  heldKeys[heldIndex] = down;
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

void pumpSerialEvents() {
  while (Serial.available() > 0 && !queueIsFull()) {
    int ch = Serial.read();
    int32_t key = serialCharToKey(ch);
    if (key == SDLK_UNKNOWN) continue;
    const bool release = isReleaseChar(ch);
    setHeldKey(key, !release);
  }
}
}

extern "C" void cyd_sdl_set_key(int32_t key, bool down) {
  setHeldKey(key, down);
}

extern "C" void furi_log_print_format(int, const char *, const char *format, ...) {
  va_list args;
  va_start(args, format);
  char buffer[160];
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Serial.println(buffer);
}

extern "C" int SDL_Init(uint32_t) { return 0; }
extern "C" void SDL_Quit(void) {}
extern "C" const char *SDL_GetError(void) { return "CYD SDL shim"; }
extern "C" uint32_t SDL_GetTicks(void) { return millis(); }
extern "C" void SDL_Delay(uint32_t ms) { delay(ms); }

extern "C" SDL_mutex *SDL_CreateMutex(void) {
  SDL_mutex *mutex = static_cast<SDL_mutex *>(malloc(sizeof(SDL_mutex)));
  if (mutex) mutex->handle = xSemaphoreCreateMutex();
  return mutex;
}
extern "C" void SDL_DestroyMutex(SDL_mutex *mutex) { if (mutex) { vSemaphoreDelete(mutex->handle); free(mutex); } }
extern "C" int SDL_LockMutex(SDL_mutex *mutex) { return mutex && xSemaphoreTake(mutex->handle, portMAX_DELAY) ? 0 : -1; }
extern "C" int SDL_UnlockMutex(SDL_mutex *mutex) { return mutex && xSemaphoreGive(mutex->handle) ? 0 : -1; }

extern "C" SDL_Window *SDL_CreateWindow(const char *, int, int, int, int, uint32_t) { return &windowObject; }
extern "C" SDL_Renderer *SDL_CreateRenderer(SDL_Window *, int, uint32_t) { return &rendererObject; }
extern "C" SDL_Texture *SDL_CreateTexture(SDL_Renderer *, uint32_t, int, int, int) { return &textureObject; }
extern "C" int SDL_SetWindowMinimumSize(SDL_Window *, int, int) { return 0; }
extern "C" int SDL_SetWindowSize(SDL_Window *, int, int) { return 0; }
extern "C" int SDL_SetWindowFullscreen(SDL_Window *, uint32_t) { return 0; }
extern "C" uint32_t SDL_GetWindowID(SDL_Window *) { return 1; }
extern "C" void SDL_GetWindowSize(SDL_Window *, int *w, int *h) { if (w) *w = 320; if (h) *h = 200; }
extern "C" int SDL_GetWindowWMInfo(SDL_Window *, SDL_SysWMinfo *) { return SDL_FALSE; }
extern "C" uint32_t SDL_GetWindowPixelFormat(SDL_Window *) { return 1; }
extern "C" int SDL_RenderSetLogicalSize(SDL_Renderer *, int, int) { return 0; }
extern "C" int SDL_SetHint(const char *, const char *) { return SDL_TRUE; }
extern "C" int SDL_RenderClear(SDL_Renderer *) { return 0; }
extern "C" int SDL_RenderCopy(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *) { return 0; }
extern "C" int SDL_UpdateTexture(SDL_Texture *, const SDL_Rect *, const void *, int) { return 0; }
extern "C" void SDL_RenderPresent(SDL_Renderer *) {
  if (screenBuffer && screenBuffer->pixels)
    cyd_present_indexed(static_cast<uint8_t *>(screenBuffer->pixels), screenBuffer->w, screenBuffer->h, screenBuffer->pitch);
}
extern "C" int SDL_PixelFormatEnumToMasks(uint32_t, int *bpp, unsigned int *r, unsigned int *g, unsigned int *b, unsigned int *a) {
  if (bpp) *bpp = 8; if (r) *r = 0; if (g) *g = 0; if (b) *b = 0; if (a) *a = 0; return SDL_TRUE;
}

static SDL_PixelFormat *makeFormat() {
  SDL_PixelFormat *format = static_cast<SDL_PixelFormat *>(calloc(1, sizeof(SDL_PixelFormat)));
  if (!format) return nullptr;
  format->BitsPerPixel = 8;
  format->BytesPerPixel = 1;
  return format;
}

extern "C" SDL_Surface *SDL_CreateRGBSurface(uint32_t, int w, int h, int, uint32_t, uint32_t, uint32_t, uint32_t) {
  Serial.printf("SDL surface %dx%d, heap before: %u\n", w, h, ESP.getFreeHeap());
  SDL_Surface *surface = static_cast<SDL_Surface *>(calloc(1, sizeof(SDL_Surface)));
  if (!surface) return nullptr;
  surface->format = makeFormat();
  surface->pixels = calloc(static_cast<size_t>(w), h);
  if (!surface->format || !surface->pixels) {
    Serial.printf("SDL surface allocation failed, heap after: %u\n", ESP.getFreeHeap());
    free(surface->pixels); free(surface->format); free(surface); return nullptr;
  }
  surface->w = w; surface->h = h; surface->pitch = w; surface->refcount = 1;
  surface->clip_rect = {0, 0, w, h};
  Serial.printf("SDL surface OK, heap after: %u\n", ESP.getFreeHeap());
  return surface;
}
extern "C" void SDL_FreeSurface(SDL_Surface *surface) {
  if (!surface) return;
  free(surface->pixels);
  if (surface->format) { if (surface->format->palette && --surface->format->palette->refcount <= 0) free(surface->format->palette); free(surface->format); }
  free(surface);
}
extern "C" int SDL_LockSurface(SDL_Surface *) { return 0; }
extern "C" void SDL_UnlockSurface(SDL_Surface *) {}
extern "C" int SDL_BlitSurface(SDL_Surface *source, const SDL_Rect *sr, SDL_Surface *dest, SDL_Rect *dr) {
  if (!source || !dest || !source->pixels || !dest->pixels) return -1;
  if (source == dest && !sr && !dr) return 0;
  int sx = sr ? sr->x : 0, sy = sr ? sr->y : 0, w = sr ? sr->w : source->w, h = sr ? sr->h : source->h;
  int dx = dr ? dr->x : 0, dy = dr ? dr->y : 0;
  if (dx < 0) { sx -= dx; w += dx; dx = 0; } if (dy < 0) { sy -= dy; h += dy; dy = 0; }
  w = min(w, dest->w - dx); h = min(h, dest->h - dy);
  for (int row = 0; row < h; ++row)
    memcpy(static_cast<uint8_t *>(dest->pixels) + (dy + row) * dest->pitch + dx,
           static_cast<uint8_t *>(source->pixels) + (sy + row) * source->pitch + sx, w);
  return 0;
}
extern "C" int SDL_FillRect(SDL_Surface *dest, const SDL_Rect *rect, uint32_t color) {
  if (!dest || !dest->pixels) return -1;
  int x = rect ? rect->x : 0, y = rect ? rect->y : 0, w = rect ? rect->w : dest->w, h = rect ? rect->h : dest->h;
  x = max(0, x); y = max(0, y); w = min(w, dest->w - x); h = min(h, dest->h - y);
  for (int row = 0; row < h; ++row) memset(static_cast<uint8_t *>(dest->pixels) + (y + row) * dest->pitch + x, color, w);
  return 0;
}
extern "C" int SDL_SaveBMP(SDL_Surface *, const char *) { return -1; }

extern "C" SDL_Palette *SDL_AllocPalette(int count) {
  SDL_Palette *palette = static_cast<SDL_Palette *>(calloc(1, sizeof(SDL_Palette)));
  if (palette) { palette->ncolors = min(count, 256); palette->refcount = 1; }
  return palette;
}
extern "C" void SDL_FreePalette(SDL_Palette *palette) { if (palette && --palette->refcount <= 0) free(palette); }
extern "C" int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int first, int count) {
  if (!palette || !colors) return -1;
  count = min(count, 256 - first);
  memcpy(&palette->colors[first], colors, count * sizeof(SDL_Color));
  uint8_t rgb[256 * 3];
  for (int i = 0; i < count; ++i) { rgb[i*3] = colors[i].r; rgb[i*3+1] = colors[i].g; rgb[i*3+2] = colors[i].b; }
  cyd_set_palette(rgb, first, count);
  return 0;
}
extern "C" int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette) {
  if (!surface || !surface->format) return -1;
  surface->format->palette = palette; if (palette) ++palette->refcount; return 0;
}
extern "C" uint32_t SDL_MapRGB(SDL_PixelFormat *format, uint8_t r, uint8_t g, uint8_t b) {
  if (!format || !format->palette) return 0;
  int best = 0, distance = INT_MAX;
  for (int i = 0; i < format->palette->ncolors; ++i) {
    int dr = format->palette->colors[i].r-r, dg = format->palette->colors[i].g-g, db = format->palette->colors[i].b-b;
    int candidate = dr*dr + dg*dg + db*db; if (candidate < distance) { distance = candidate; best = i; }
  }
  return best;
}

extern "C" int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, uint16_t, uint8_t, int, uint16_t, uint8_t, int) { if (cvt) { cvt->needed=0; cvt->len_mult=1; cvt->len_ratio=1; } return 0; }
extern "C" int SDL_ConvertAudio(SDL_AudioCVT *cvt) { if (cvt) cvt->len_cvt=cvt->len; return 0; }
extern "C" int SDL_PollEvent(SDL_Event *event) {
  pumpSerialEvents();
  return popEvent(event) ? 1 : 0;
}
extern "C" int SDL_WaitEvent(SDL_Event *event) {
  while (true) {
    pumpSerialEvents();
    if (popEvent(event)) return 1;
    delay(10);
  }
}
extern "C" int SDL_PushEvent(SDL_Event *event) {
  if (!event || queueIsFull()) return 0;
  eventQueue[eventTail] = *event;
  eventTail = (eventTail + 1) % eventQueueSize;
  return 1;
}
extern "C" SDL_Keymod SDL_GetModState(void) { return 0; }
extern "C" uint32_t SDL_GetRelativeMouseState(int *x, int *y) { if(x)*x=0; if(y)*y=0; return 0; }
extern "C" int SDL_SetRelativeMouseMode(int) { return 0; }
extern "C" void SDL_WarpMouseInWindow(SDL_Window *, int, int) {}
extern "C" int SDL_NumJoysticks(void) { return 0; }
extern "C" SDL_Joystick *SDL_JoystickOpen(int) { return nullptr; }
extern "C" void SDL_JoystickClose(SDL_Joystick *) {}
extern "C" int SDL_JoystickEventState(int) { return 0; }
extern "C" int SDL_JoystickNumButtons(SDL_Joystick *) { return 0; }
extern "C" int SDL_JoystickNumHats(SDL_Joystick *) { return 0; }
extern "C" int SDL_JoystickGetButton(SDL_Joystick *, int) { return 0; }
extern "C" int16_t SDL_JoystickGetAxis(SDL_Joystick *, int) { return 0; }
extern "C" uint8_t SDL_JoystickGetHat(SDL_Joystick *, int) { return 0; }
extern "C" void SDL_JoystickUpdate(void) {}
