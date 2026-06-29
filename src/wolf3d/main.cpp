#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include "esp_wifi.h"
#include "esp_bt.h"

#include "board_config.h"

#ifndef CYD_WOLF_PALETTE_BRIGHTNESS_PERCENT
#define CYD_WOLF_PALETTE_BRIGHTNESS_PERCENT 100
#endif

#ifndef CYD_S2_FACE_ENABLE
#define CYD_S2_FACE_ENABLE 1
#endif

#ifndef CYD_WOLF_USE_DMA_PRESENT
#define CYD_WOLF_USE_DMA_PRESENT 0
#endif

#ifndef CYD_WOLF_DMA_STRIPE_ROWS
#define CYD_WOLF_DMA_STRIPE_ROWS 8
#endif

#if CYD_WOLF_DMA_STRIPE_ROWS < 1
#undef CYD_WOLF_DMA_STRIPE_ROWS
#define CYD_WOLF_DMA_STRIPE_ROWS 1
#endif

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
#if CYD_WOLF_USE_DMA_PRESENT
constexpr int kPresentStripeBuffers = 2;
#else
constexpr int kPresentStripeBuffers = 1;
#endif
uint16_t stripe[kPresentStripeBuffers][320 * CYD_WOLF_DMA_STRIPE_ROWS];
bool tftDmaReady = false;
bool tftDmaWriteOpen = false;

const char *requiredBaseNames[] = {
  "AUDIOHED", "AUDIOT", "GAMEMAPS", "MAPHEAD",
  "VGADICT", "VGAGRAPH", "VGAHEAD", "VSWAP",
};

void finishDmaPresent() {
#if CYD_WOLF_USE_DMA_PRESENT
  if (tftDmaWriteOpen) {
    wolfTft.dmaWait();
    wolfTft.endWrite();
    tftDmaWriteOpen = false;
  }
#endif
}

void fatalScreen(const String &message) {
  finishDmaPresent();
  wolfTft.fillScreen(TFT_BLACK);
  wolfTft.setTextColor(TFT_RED, TFT_BLACK);
  wolfTft.drawString("WOLF3D PORT", 12, 12, 4);
  
  wolfTft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  int16_t startX = 12;
  int16_t startY = 58;
  int16_t currentX = startX;
  int16_t currentY = startY;
  int16_t maxX = wolfTft.width() - 12;
  int16_t lineHeight = 18;
  uint8_t font = 2;
  
  String currentLine = "";
  int index = 0;
  
  while (index < message.length()) {
    int nextSpace = message.indexOf(' ', index);
    int nextNewline = message.indexOf('\n', index);
    
    int nextCut = -1;
    bool isNewline = false;
    if (nextSpace == -1 && nextNewline == -1) {
      nextCut = message.length();
    } else if (nextSpace == -1) {
      nextCut = nextNewline;
      isNewline = true;
    } else if (nextNewline == -1) {
      nextCut = nextSpace;
    } else {
      if (nextSpace < nextNewline) {
        nextCut = nextSpace;
      } else {
        nextCut = nextNewline;
        isNewline = true;
      }
    }
    
    String word = message.substring(index, nextCut);
    String testLine = currentLine.length() > 0 ? (currentLine + " " + word) : word;
    
    if (currentLine.length() > 0 && currentX + wolfTft.textWidth(testLine, font) > maxX) {
      wolfTft.drawString(currentLine, currentX, currentY, font);
      currentY += lineHeight;
      currentLine = word;
    } else {
      currentLine = testLine;
    }
    
    if (isNewline) {
      wolfTft.drawString(currentLine, currentX, currentY, font);
      currentY += lineHeight;
      currentLine = "";
    }
    
    index = nextCut + 1;
  }
  
  if (currentLine.length() > 0) {
    wolfTft.drawString(currentLine, currentX, currentY, font);
  }
}

void statusScreen(const String &message) {
  finishDmaPresent();
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
    int r = (int)p[0] * CYD_WOLF_PALETTE_BRIGHTNESS_PERCENT / 100;
    int g = (int)p[1] * CYD_WOLF_PALETTE_BRIGHTNESS_PERCENT / 100;
    int b = (int)p[2] * CYD_WOLF_PALETTE_BRIGHTNESS_PERCENT / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    palette565[first + i] = wolfTft.color565((uint8_t)r, (uint8_t)g, (uint8_t)b);
  }
}

extern "C" void cyd_send_face_sprite(const uint8_t *pixels, int width, int height) {
#if CYD_S2_FACE_ENABLE
  if (!pixels || width <= 0 || height <= 0) return;
  
  // Write packet header: Sync (0xAA, 0x55), width, height
  Serial.write(0xAA);
  Serial.write(0x55);
  Serial.write((uint8_t)width);
  Serial.write((uint8_t)height);
  
  // Stream pixels as 16-bit RGB565 big-endian bytes
  int total = width * height;
  for (int i = 0; i < total; ++i) {
    uint16_t color = palette565[pixels[i]];
    Serial.write((uint8_t)(color >> 8));
    Serial.write((uint8_t)(color & 0xFF));
  }
#else
  (void)pixels;
  (void)width;
  (void)height;
#endif
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
  if (!tftDmaWriteOpen) {
    wolfTft.startWrite();
    tftDmaWriteOpen = true;
  }
  int stripeIndex = 0;
  for (int y0 = 0; y0 < rectHeight; y0 += CYD_WOLF_DMA_STRIPE_ROWS) {
    const int rows = min(CYD_WOLF_DMA_STRIPE_ROWS, rectHeight - y0);
    uint16_t *targetBase = stripe[stripeIndex];
    for (int row = 0; row < rows; ++row) {
      const uint8_t *source = pixels + (sourceY + y0 + row) * pitch + sourceX;
      uint16_t *target = targetBase + row * rectWidth;
      for (int x = 0; x < rectWidth; ++x) target[x] = palette565[source[x]];
    }
#if CYD_WOLF_USE_DMA_PRESENT
    if (tftDmaReady) {
      wolfTft.pushImageDMA(sourceX, top + sourceY + y0, rectWidth, rows, targetBase);
      stripeIndex ^= 1;
    } else
#endif
    {
      wolfTft.pushImage(sourceX, top + sourceY + y0, rectWidth, rows, targetBase);
    }
  }
#if !CYD_WOLF_USE_DMA_PRESENT
  wolfTft.endWrite();
  tftDmaWriteOpen = false;
#else
  if (!tftDmaReady) {
    wolfTft.endWrite();
    tftDmaWriteOpen = false;
  }
#endif
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

  if (true) {
    const int hudY = max(0, height - 40);
    presentIndexedRect(pixels, width, height, pitch, 0, hudY, min(width, 320), height - hudY);
  }
}

void setup() {
  // Release Bluetooth controller memory to reclaim SRAM
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
  
  // Power down WiFi radio
  esp_wifi_stop();
  esp_wifi_deinit();

  Serial.setTxBufferSize(2048);
  Serial.begin(460800);

  pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
  wolfTft.init();
  wolfTft.setRotation(DISPLAY_ROTATION);
  wolfTft.setSwapBytes(true);
#if CYD_WOLF_USE_DMA_PRESENT
  tftDmaReady = wolfTft.initDMA();
  Serial.printf("TFT DMA present %s\n", tftDmaReady ? "enabled" : "disabled");
#endif

#if CYD_S2_FACE_ENABLE
  // Send connection test pattern (16x16 red square) to S2 Mini using native color565
  palette565[14] = wolfTft.color565(255, 0, 0); // Red
  uint8_t testPixels[16 * 16];
  memset(testPixels, 14, sizeof(testPixels));
  cyd_send_face_sprite(testPixels, 16, 16);
#endif

  // Display visual build confirmation on CYD boot screen
  wolfTft.fillScreen(TFT_BLACK);
  wolfTft.setTextColor(TFT_CYAN, TFT_BLACK);
  char buildStr[32];
  snprintf(buildStr, sizeof(buildStr), "Build: B%d", CYD_WOLF_BUILD_NUMBER);
  wolfTft.drawString(buildStr, 12, 12, 4);
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
