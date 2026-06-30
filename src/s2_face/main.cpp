#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft;

// Serial RX pin on Lolin S2 Mini (connect to CYD GPIO 1 / P1 TX)
#define RX_PIN 18
#define TX_PIN 17

// Max buffer size: BJ face is typical 24x32 or 32x32. Let's support up to 64x64.
#define MAX_FACE_WIDTH 64
#define MAX_FACE_HEIGHT 64
uint16_t pixelBuffer[MAX_FACE_WIDTH * MAX_FACE_HEIGHT];

// Nearest-neighbor vertical scaling and horizontal scaling to center on 240x240 LCD.
void drawScaledFace(uint8_t w, uint8_t h) {
  // Determine scale factor. Round LCD is 240x240.
  // 24x32 scaled 5x is 120x160.
  // 24x32 scaled 6x is 144x192. Let's use 5x scale for a nicely sized face.
  int scale = 5;
  int targetW = w * scale;
  int targetH = h * scale;
  int startX = (240 - targetW) / 2;
  int startY = (240 - targetH) / 2;

  tft.startWrite();
  for (int y = 0; y < h; ++y) {
    uint16_t rowBuffer[240];
    for (int x = 0; x < w; ++x) {
      uint16_t color = pixelBuffer[y * w + x];
      for (int s = 0; s < scale; ++s) {
        rowBuffer[x * scale + s] = color;
      }
    }
    for (int s = 0; s < scale; ++s) {
      tft.pushImage(startX, startY + y * scale + s, targetW, 1, rowBuffer);
    }
  }
  tft.endWrite();
}

bool firstFrameReceived = false;

enum ParseState {
  STATE_WAIT_AA,
  STATE_WAIT_55,
  STATE_WAIT_W,
  STATE_WAIT_H,
  STATE_READ_PIXELS
};

ParseState parserState = STATE_WAIT_AA;
uint8_t faceWidth = 0;
uint8_t faceHeight = 0;
int pixelsReadBytes = 0;
uint32_t lastByteTime = 0;

void setup() {
  // Initialize USB Serial (for debugging)
  Serial.begin(115200);

  // Initialize Hardware Serial1 for receiving CYD data with larger buffer to prevent overflow
  Serial1.setRxBufferSize(2048);
  Serial1.begin(460800, SERIAL_8N1, RX_PIN, TX_PIN);

  // Turn on the backlight (GPIO 13) explicitly
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);

  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(false);

  // Force BGR color order (GC9A01 default)
  tft.writecommand(0x36); // MADCTL
  tft.writedata(0x08);    // Set BGR bit (0x08) to force BGR

  // Startup RGB color test flash to verify TFT wiring
  tft.fillScreen(TFT_RED);
  delay(200);
  tft.fillScreen(TFT_GREEN);
  delay(200);
  tft.fillScreen(TFT_BLUE);
  delay(200);

  tft.fillScreen(TFT_BLACK);

  // Draw a sleek background ring to wow the user
  tft.drawCircle(120, 120, 118, TFT_DARKGREY);
  tft.drawCircle(120, 120, 117, TFT_BLUE);

  // Display visual build and diagnostic info on the screen
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawCentreString("S2 FACE B24", 120, 40, 2);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("Pins:", 120, 70, 2);
  tft.drawCentreString("MOSI=11 SCLK=12 CS=10", 120, 90, 2);
  tft.drawCentreString("DC=14 RST=15 BL=13", 120, 110, 2);
  tft.drawCentreString("RX=18 TX=17 (460800 baud)", 120, 130, 2);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawCentreString("Waiting for CYD...", 120, 170, 2);

  Serial.println("Lolin S2 Mini GC9A01 Face display B24 initialized.");
}

void loop() {
  uint32_t now = millis();

  // If we are mid-frame and no data arrives for 150ms, timeout and reset state
  if (parserState != STATE_WAIT_AA && (now - lastByteTime > 150)) {
    Serial.printf("[S2] Parser timeout in state %d! Resetting to wait for AA...\n", parserState);
    // Flush any pending data in serial buffer to clear garbage
    while (Serial1.available() > 0) {
      Serial1.read();
    }
    parserState = STATE_WAIT_AA;
  }

  while (Serial1.available() > 0) {
    int b = Serial1.read();
    if (b < 0) break; // Should not happen

    lastByteTime = now = millis(); // Refresh last byte arrival time

    switch (parserState) {
      case STATE_WAIT_AA:
        if (b == 0xAA) {
          parserState = STATE_WAIT_55;
        }
        break;

      case STATE_WAIT_55:
        if (b == 0x55) {
          parserState = STATE_WAIT_W;
        } else if (b == 0xAA) {
          // Stay in STATE_WAIT_55 if we got another 0xAA (e.g. 0xAA 0xAA 0x55)
          parserState = STATE_WAIT_55;
        } else {
          parserState = STATE_WAIT_AA;
        }
        break;

      case STATE_WAIT_W:
        faceWidth = b;
        parserState = STATE_WAIT_H;
        break;

      case STATE_WAIT_H:
        faceHeight = b;
        if (faceWidth > 0 && faceWidth <= MAX_FACE_WIDTH && faceHeight > 0 && faceHeight <= MAX_FACE_HEIGHT) {
          parserState = STATE_READ_PIXELS;
          pixelsReadBytes = 0;
        } else {
          Serial.printf("[S2] Invalid dimensions parsed: %dx%d. Resetting to wait for AA.\n", faceWidth, faceHeight);
          parserState = STATE_WAIT_AA;
        }
        break;

      case STATE_READ_PIXELS:
        {
          uint8_t *ptr = (uint8_t *)pixelBuffer;
          ptr[pixelsReadBytes++] = b;

          int totalBytes = faceWidth * faceHeight * 2;
          if (pixelsReadBytes >= totalBytes) {
            Serial.printf("[S2] Received complete face frame: %dx%d (%d bytes)\n", faceWidth, faceHeight, totalBytes);
            uint16_t bgColor = pixelBuffer[0];
            static uint16_t lastBgColor = 0;
            bool bgChanged = (bgColor != lastBgColor);
            if (!firstFrameReceived || bgChanged) {
              tft.fillScreen(bgColor);
              tft.drawCircle(120, 120, 118, TFT_DARKGREY);
              tft.drawCircle(120, 120, 117, TFT_BLUE);
              firstFrameReceived = true;
              lastBgColor = bgColor;
            }
            drawScaledFace(faceWidth, faceHeight);
            parserState = STATE_WAIT_AA;
          }
        }
        break;
    }
  }
}
