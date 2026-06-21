#include <Arduino.h>
#include "board_config.h"

#ifndef CYD_RGB_LED_ENABLE
#define CYD_RGB_LED_ENABLE 0
#endif

#ifndef CYD_RGB_ACTIVE_LOW
#define CYD_RGB_ACTIVE_LOW 1
#endif

#ifndef CYD_RGB_MAX_BRIGHTNESS
#define CYD_RGB_MAX_BRIGHTNESS 64
#endif

namespace {
constexpr int kRedChannel = 3;
constexpr int kGreenChannel = 4;
constexpr int kBlueChannel = 5;
constexpr int kLedResolutionBits = 8;
bool ready = false;
uint8_t lastR = 255;
uint8_t lastG = 255;
uint8_t lastB = 255;

uint8_t clampBrightness(uint8_t value) {
  return value > CYD_RGB_MAX_BRIGHTNESS ? CYD_RGB_MAX_BRIGHTNESS : value;
}

uint8_t driveValue(uint8_t value) {
#if CYD_RGB_ACTIVE_LOW
  return 255 - value;
#else
  return value;
#endif
}

void forceBacklightFull() {
#ifdef TFT_BACKLIGHT_PIN
  pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
#endif
}

void ensureReady() {
#if CYD_RGB_LED_ENABLE
  if (ready) return;
  forceBacklightFull();
  ledcSetup(kRedChannel, 5000, kLedResolutionBits);
  ledcSetup(kGreenChannel, 5000, kLedResolutionBits);
  ledcSetup(kBlueChannel, 5000, kLedResolutionBits);
  ledcAttachPin(CYD_RGB_RED_PIN, kRedChannel);
  ledcAttachPin(CYD_RGB_GREEN_PIN, kGreenChannel);
  ledcAttachPin(CYD_RGB_BLUE_PIN, kBlueChannel);
  ready = true;
#endif
}
}

extern "C" void cyd_hw_rgb_set(uint8_t r, uint8_t g, uint8_t b) {
#if CYD_RGB_LED_ENABLE
  ensureReady();
  forceBacklightFull();
  r = clampBrightness(r);
  g = clampBrightness(g);
  b = clampBrightness(b);
  if (r == lastR && g == lastG && b == lastB) return;
  lastR = r;
  lastG = g;
  lastB = b;
  ledcWrite(kRedChannel, driveValue(r));
  ledcWrite(kGreenChannel, driveValue(g));
  ledcWrite(kBlueChannel, driveValue(b));
#else
  (void)r;
  (void)g;
  (void)b;
#endif
}

extern "C" void cyd_hw_rgb_flash_state(int damageCount, int bonusCount) {
#if CYD_RGB_LED_ENABLE
  if (damageCount > 0) {
    int level = damageCount > 40 ? 255 : 120 + damageCount * 3;
    if (level > 255) level = 255;
    cyd_hw_rgb_set((uint8_t)level, 0, 0);
  } else if (bonusCount > 0) {
    int level = bonusCount > 20 ? 180 : 80 + bonusCount * 5;
    if (level > 200) level = 200;
    cyd_hw_rgb_set((uint8_t)level, (uint8_t)level, 40);
  } else {
    cyd_hw_rgb_set(0, 0, 0);
  }
#else
  (void)damageCount;
  (void)bonusCount;
#endif
}

extern "C" void cyd_hw_rgb_flash_kind(int kind, int level) {
#if CYD_RGB_LED_ENABLE
  if (kind == 3) {
    int red = 120 + level * 4;
    if (red > 255) red = 255;
    cyd_hw_rgb_set((uint8_t)red, 0, 0);
  } else if (kind == 2) {
    int red = 140 + level * 3;
    int green = 90 + level * 2;
    if (red > 255) red = 255;
    if (green > 220) green = 220;
    cyd_hw_rgb_set((uint8_t)red, (uint8_t)green, 0);
  } else if (kind == 1) {
    int white = 90 + level * 3;
    if (white > 200) white = 200;
    cyd_hw_rgb_set((uint8_t)white, (uint8_t)white, (uint8_t)(white / 2));
  } else {
    cyd_hw_rgb_set(0, 0, 0);
  }
#else
  (void)kind;
  (void)level;
#endif
}
