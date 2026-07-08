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

#ifndef CYD_WOLF_DEMO_MODE
#define CYD_WOLF_DEMO_MODE 0
#endif

#ifndef CYD_WOLF_DEMO_NUMBER
#define CYD_WOLF_DEMO_NUMBER 0
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
extern "C" void cyd_ca_preallocate(void);
extern "C" void cyd_hw_sound_preallocate(void);

extern int viewscreenx;
extern int viewscreeny;
extern int viewwidth;
extern int viewheight;

TFT_eSPI wolfTft;
#ifdef LCDWIKI_ES3C28P
SPIClass wolfSdSpi(VSPI);
SPIClass wolfLcdSpi(HSPI);
#else
SPIClass wolfSdSpi(VSPI);
#endif

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

#ifdef LCDWIKI_ES3C28P
constexpr int kLcdCs = 10;
constexpr int kLcdDc = 46;
constexpr int kLcdSck = 12;
constexpr int kLcdMosi = 11;
constexpr uint32_t kLcdSpiFrequency = 20000000;

void lcdwikiWriteByte(uint8_t value) {
  wolfLcdSpi.transfer(value);
}

void lcdwikiWriteBytes(const uint8_t *data, size_t length) {
  wolfLcdSpi.writeBytes(data, length);
}

void lcdwikiCommand(uint8_t command) {
  wolfLcdSpi.beginTransaction(SPISettings(kLcdSpiFrequency, MSBFIRST, SPI_MODE0));
  digitalWrite(kLcdDc, LOW);
  digitalWrite(kLcdCs, LOW);
  lcdwikiWriteByte(command);
  digitalWrite(kLcdCs, HIGH);
  wolfLcdSpi.endTransaction();
}

void lcdwikiData(uint8_t data) {
  wolfLcdSpi.beginTransaction(SPISettings(kLcdSpiFrequency, MSBFIRST, SPI_MODE0));
  digitalWrite(kLcdDc, HIGH);
  digitalWrite(kLcdCs, LOW);
  lcdwikiWriteByte(data);
  digitalWrite(kLcdCs, HIGH);
  wolfLcdSpi.endTransaction();
}

void lcdwikiData16(uint16_t data) {
  wolfLcdSpi.beginTransaction(SPISettings(kLcdSpiFrequency, MSBFIRST, SPI_MODE0));
  digitalWrite(kLcdDc, HIGH);
  digitalWrite(kLcdCs, LOW);
  lcdwikiWriteByte(data >> 8);
  lcdwikiWriteByte(data & 0xff);
  digitalWrite(kLcdCs, HIGH);
  wolfLcdSpi.endTransaction();
}

void lcdwikiSetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  lcdwikiCommand(0x2a);
  lcdwikiData16(x0);
  lcdwikiData16(x1);
  lcdwikiCommand(0x2b);
  lcdwikiData16(y0);
  lcdwikiData16(y1);
  lcdwikiCommand(0x2c);
}

void lcdwikiFill(uint16_t color) {
  lcdwikiSetWindow(0, 0, 319, 239);
  const uint8_t pattern[] = {static_cast<uint8_t>(color >> 8), static_cast<uint8_t>(color & 0xff)};
  uint8_t chunk[256];
  for (size_t i = 0; i < sizeof(chunk); i += 2) {
    chunk[i] = pattern[0];
    chunk[i + 1] = pattern[1];
  }
  wolfLcdSpi.beginTransaction(SPISettings(kLcdSpiFrequency, MSBFIRST, SPI_MODE0));
  digitalWrite(kLcdDc, HIGH);
  digitalWrite(kLcdCs, LOW);
  size_t bytesRemaining = 320UL * 240UL * 2UL;
  while (bytesRemaining) {
    const size_t bytes = min(bytesRemaining, sizeof(chunk));
    lcdwikiWriteBytes(chunk, bytes);
    bytesRemaining -= bytes;
  }
  digitalWrite(kLcdCs, HIGH);
  wolfLcdSpi.endTransaction();
}

void lcdwikiPushRgb565(int x, int y, int w, int h, const uint16_t *pixels) {
  if (!pixels || w <= 0 || h <= 0) return;
  lcdwikiSetWindow(x, y, x + w - 1, y + h - 1);
  wolfLcdSpi.beginTransaction(SPISettings(kLcdSpiFrequency, MSBFIRST, SPI_MODE0));
  digitalWrite(kLcdDc, HIGH);
  digitalWrite(kLcdCs, LOW);
  lcdwikiWriteBytes(reinterpret_cast<const uint8_t *>(pixels), w * h * 2);
  digitalWrite(kLcdCs, HIGH);
  wolfLcdSpi.endTransaction();
}

uint16_t lcdwikiColor565(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t color = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (color >> 8) | (color << 8);
}

void lcdwikiPanelInit() {
  pinMode(kLcdCs, OUTPUT);
  pinMode(kLcdDc, OUTPUT);
  pinMode(kLcdSck, OUTPUT);
  pinMode(kLcdMosi, OUTPUT);
  digitalWrite(kLcdCs, HIGH);
  digitalWrite(kLcdDc, HIGH);
  wolfLcdSpi.begin(kLcdSck, -1, kLcdMosi, kLcdCs);

  lcdwikiCommand(0x01);
  delay(150);
  lcdwikiCommand(0x28);
  lcdwikiCommand(0xcf);
  lcdwikiData(0x00);
  lcdwikiData(0x83);
  lcdwikiData(0x30);
  lcdwikiCommand(0xed);
  lcdwikiData(0x64);
  lcdwikiData(0x03);
  lcdwikiData(0x12);
  lcdwikiData(0x81);
  lcdwikiCommand(0xe8);
  lcdwikiData(0x85);
  lcdwikiData(0x01);
  lcdwikiData(0x79);
  lcdwikiCommand(0xcb);
  lcdwikiData(0x39);
  lcdwikiData(0x2c);
  lcdwikiData(0x00);
  lcdwikiData(0x34);
  lcdwikiData(0x02);
  lcdwikiCommand(0xf7);
  lcdwikiData(0x20);
  lcdwikiCommand(0xea);
  lcdwikiData(0x00);
  lcdwikiData(0x00);
  lcdwikiCommand(0xc0);
  lcdwikiData(0x26);
  lcdwikiCommand(0xc1);
  lcdwikiData(0x11);
  lcdwikiCommand(0xc5);
  lcdwikiData(0x35);
  lcdwikiData(0x3e);
  lcdwikiCommand(0xc7);
  lcdwikiData(0xbe);
  lcdwikiCommand(0x36);
  lcdwikiData(0x28);
  lcdwikiCommand(0x3a);
  lcdwikiData(0x55);
  lcdwikiCommand(0x21);
  lcdwikiCommand(0xb1);
  lcdwikiData(0x00);
  lcdwikiData(0x1b);
  lcdwikiCommand(0xf2);
  lcdwikiData(0x08);
  lcdwikiCommand(0x26);
  lcdwikiData(0x01);

  const uint8_t gammaP[] = {0x1f, 0x1a, 0x18, 0x0a, 0x0f, 0x06, 0x45, 0x87, 0x32, 0x0a, 0x07, 0x02, 0x07, 0x05, 0x00};
  lcdwikiCommand(0xe0);
  for (uint8_t value : gammaP) lcdwikiData(value);
  const uint8_t gammaN[] = {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3a, 0x78, 0x4d, 0x05, 0x18, 0x0d, 0x38, 0x3a, 0x1f};
  lcdwikiCommand(0xe1);
  for (uint8_t value : gammaN) lcdwikiData(value);

  lcdwikiCommand(0x11);
  delay(150);
  lcdwikiCommand(0x29);
  delay(50);
}
#endif

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
#ifdef LCDWIKI_ES3C28P
  if (message.indexOf("Mounting") >= 0) return;
  if (message.indexOf("SD mount failed") >= 0) {
    lcdwikiFill(0xf800);
  } else if (message.indexOf("Missing") >= 0) {
    lcdwikiFill(0xffe0);
  } else {
    lcdwikiFill(0x001f);
  }
  return;
#endif
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
#ifdef LCDWIKI_ES3C28P
  return;
#endif
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
#ifdef LCDWIKI_ES3C28P
    palette565[first + i] = lcdwikiColor565((uint8_t)r, (uint8_t)g, (uint8_t)b);
#else
    palette565[first + i] = wolfTft.color565((uint8_t)r, (uint8_t)g, (uint8_t)b);
#endif
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
#ifdef LCDWIKI_ES3C28P
  for (int y0 = 0; y0 < rectHeight; y0 += CYD_WOLF_DMA_STRIPE_ROWS) {
    const int rows = min(CYD_WOLF_DMA_STRIPE_ROWS, rectHeight - y0);
    uint16_t *targetBase = stripe[0];
    for (int row = 0; row < rows; ++row) {
      const uint8_t *source = pixels + (sourceY + y0 + row) * pitch + sourceX;
      uint16_t *target = targetBase + row * rectWidth;
      for (int x = 0; x < rectWidth; ++x) target[x] = palette565[source[x]];
    }
    lcdwikiPushRgb565(sourceX, top + sourceY + y0, rectWidth, rows, targetBase);
  }
  return;
#endif
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

  Serial.setTxBufferSize(2048);
  Serial.begin(460800);

  pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
#ifdef LCDWIKI_ES3C28P
  lcdwikiPanelInit();
  lcdwikiFill(0x0000);
#else
  wolfTft.init();
  wolfTft.setRotation(DISPLAY_ROTATION);
  wolfTft.setSwapBytes(true);
#endif
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
#ifndef LCDWIKI_ES3C28P
  wolfTft.fillScreen(TFT_BLACK);
  wolfTft.setTextColor(TFT_CYAN, TFT_BLACK);
#endif
  char buildStr[32];
  snprintf(buildStr, sizeof(buildStr), "Build: B%d", CYD_WOLF_BUILD_NUMBER);
#ifndef LCDWIKI_ES3C28P
  wolfTft.drawString(buildStr, 12, 12, 4);
#endif
  fatalScreen("Mounting SD card...");

  wolfSdSpi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, wolfSdSpi, 20000000)) {
    fatalScreen("SD mount failed");
    return;
  }

  String detectedExtension = detectGameData();
  if (!detectedExtension.length()) return;

#ifndef LCDWIKI_ES3C28P
  wolfTft.fillScreen(TFT_BLACK);
#endif
  statusScreen("Wolf3D data: ." + detectedExtension);
  Serial.printf("Wolf3D starting with .%s data, free heap: %u bytes\n",
                detectedExtension.c_str(), ESP.getFreeHeap());

  // Spawn custom game task with a stack of 6144 bytes and run wolf_main there.
  // This allows us to delete loopTask and reclaim its 8192-byte stack.
  static String s_ext;
  s_ext = detectedExtension;
  xTaskCreatePinnedToCore(
    [](void *param) {
      String ext = *(String*)param;
      char arg0[] = "wolf3d";
#if CYD_WOLF_DEMO_MODE
      char arg1[] = "--demotest";
      char arg2[] = "--nowait";
      char *argv[] = {arg0, arg1, arg2, nullptr};
      Serial.printf("CYD demo mode enabled: demo %d, face stream off, free heap: %u bytes\n",
                    CYD_WOLF_DEMO_NUMBER, ESP.getFreeHeap());
      wolf_main(3, argv);
#else
      char *argv[] = {arg0, nullptr};
      wolf_main(1, argv);
#endif
      fatalScreen("Wolf3D exited");
      vTaskDelete(NULL);
    },
    "gameTask",
    6144,
    &s_ext,
    5,
    nullptr,
    1 // Run on Core 1
  );

  // Deleting loopTask reclaims its 8192-byte stack immediately
  vTaskDelete(NULL);
}

void loop() {
}
