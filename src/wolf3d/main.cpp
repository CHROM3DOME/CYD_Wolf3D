#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>

#include "board_config.h"

extern "C" int wolf_main(int argc, char *argv[]);
extern "C" int wolf3d_is_ingame(void);

extern int viewscreenx;
extern int viewscreeny;
extern int viewwidth;
extern int viewheight;

TFT_eSPI wolfTft;
SPIClass wolfSdSpi(VSPI);

namespace {
uint16_t palette565[256] = {};
uint16_t stripe[320 * 8];

const char *requiredBaseNames[] = {
  "AUDIOHED", "AUDIOT", "GAMEMAPS", "MAPHEAD",
  "VGADICT", "VGAGRAPH", "VGAHEAD", "VSWAP",
};

void fatalScreen(const String &message) {
  wolfTft.fillScreen(TFT_BLACK);
  wolfTft.setTextColor(TFT_RED, TFT_BLACK);
  wolfTft.drawString("WOLF3D PORT", 12, 12, 4);
  wolfTft.setTextColor(TFT_WHITE, TFT_BLACK);
  wolfTft.drawString(message, 12, 58, 2);
}

void statusScreen(const String &message) {
  wolfTft.fillRect(0, 86, 320, 40, TFT_BLACK);
  wolfTft.setTextColor(TFT_GREEN, TFT_BLACK);
  wolfTft.drawString(message, 12, 96, 2);
}

String uppercasePath(String path) {
  path.toUpperCase();
  return path;
}

bool sdExistsCaseFriendly(const String &path) {
  return SD.exists(path) || SD.exists(uppercasePath(path));
}

bool hasCompleteDataSet(const char *extension, String *missingPath = nullptr) {
  for (const char *base : requiredBaseNames) {
    String path = String("/wolf3d/") + base + "." + extension;
    if (!sdExistsCaseFriendly(path)) {
      if (missingPath) *missingPath = path;
      return false;
    }
  }
  return true;
}

String detectGameData() {
#ifdef UPLOAD
  const char *extensions[] = {"WL1"};
#else
  const char *extensions[] = {"WL6", "WL1"};
#endif
  String firstMissing;
  for (const char *extension : extensions) {
    String missing;
    if (hasCompleteDataSet(extension, &missing)) return String(extension);
    if (!firstMissing.length()) firstMissing = missing;
  }
  fatalScreen("Missing " + firstMissing);
  Serial.printf("Missing game data: %s\n", firstMissing.c_str());
  return "";
}
}

extern "C" void cyd_wolf3d_status(const char *message) {
  if (!message) return;
  Serial.println(message);
  statusScreen(message);
}

extern "C" void cyd_wolf3d_fatal(const char *message) {
  String text = message && *message ? String(message) : String("Wolf3D stopped");
  Serial.println(text);
  fatalScreen(text);
}

extern "C" void cyd_set_palette(const uint8_t *rgb, int first, int count) {
  for (int i = 0; i < count && first + i < 256; ++i) {
    const uint8_t *p = rgb + i * 3;
    palette565[first + i] = wolfTft.color565(p[0], p[1], p[2]);
  }
}

void presentIndexedRect(const uint8_t *pixels, int width, int height, int pitch,
                        int sourceX, int sourceY, int rectWidth, int rectHeight) {
  if (!pixels || width <= 0 || height <= 0 || rectWidth <= 0 || rectHeight <= 0) return;
  sourceX = constrain(sourceX, 0, width - 1);
  sourceY = constrain(sourceY, 0, height - 1);
  rectWidth = min(rectWidth, width - sourceX);
  rectHeight = min(rectHeight, height - sourceY);
  rectWidth = min(rectWidth, 320 - sourceX);
  if (rectWidth <= 0 || rectHeight <= 0) return;

  const int top = max(0, (240 - height) / 2);
  wolfTft.startWrite();
  for (int y0 = 0; y0 < rectHeight; y0 += 8) {
    const int rows = min(8, rectHeight - y0);
    for (int row = 0; row < rows; ++row) {
      const uint8_t *source = pixels + (sourceY + y0 + row) * pitch + sourceX;
      uint16_t *target = stripe + row * rectWidth;
      for (int x = 0; x < rectWidth; ++x) target[x] = palette565[source[x]];
    }
    wolfTft.pushImage(sourceX, top + sourceY + y0, rectWidth, rows, stripe);
  }
  wolfTft.endWrite();
}

extern "C" void cyd_present_indexed(const uint8_t *pixels, int width, int height, int pitch) {
  static uint32_t frameCounter = 0;
  if (!pixels || width <= 0 || height <= 0) return;

  const bool canCrop = wolf3d_is_ingame() &&
                       viewwidth > 0 && viewheight > 0 &&
                       viewscreenx >= 0 && viewscreeny >= 0 &&
                       viewscreenx + viewwidth <= width &&
                       viewscreeny + viewheight <= height;

  if (!canCrop) {
    frameCounter = 0;
    presentIndexedRect(pixels, width, height, pitch, 0, 0, min(width, 320), height);
    return;
  }

  ++frameCounter;
  if (frameCounter <= 4 || (frameCounter % 60) == 0) {
    presentIndexedRect(pixels, width, height, pitch, 0, 0, min(width, 320), height);
    return;
  }

  presentIndexedRect(pixels, width, height, pitch, viewscreenx, viewscreeny, viewwidth, viewheight);

  if ((frameCounter % 12) == 0) {
    const int hudY = max(0, height - 40);
    presentIndexedRect(pixels, width, height, pitch, 0, hudY, min(width, 320), height - hudY);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
  wolfTft.init();
  wolfTft.setRotation(DISPLAY_ROTATION);
  wolfTft.setSwapBytes(true);
  fatalScreen("Mounting SD card...");

  wolfSdSpi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, wolfSdSpi, 20000000)) {
    fatalScreen("SD mount failed");
    return;
  }

  String detectedExtension = detectGameData();
  if (!detectedExtension.length()) return;

  wolfTft.fillScreen(TFT_BLACK);
  statusScreen("Wolf3D data: ." + detectedExtension);
  Serial.printf("Wolf3D starting with .%s data, free heap: %u bytes\n",
                detectedExtension.c_str(), ESP.getFreeHeap());
  char arg0[] = "wolf3d";
  char arg1[] = "--tedlevel";
  char arg2[] = "0";
  char arg3[] = "--baby";
  char *argv[] = {arg0, arg1, arg2, arg3, nullptr};
  wolf_main(4, argv);
  fatalScreen("Wolf3D exited");
}

void loop() {
  delay(1000);
}
