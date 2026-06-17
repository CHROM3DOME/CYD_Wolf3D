#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>
#include <math.h>

#include "board_config.h"

TFT_eSPI tft;
SPIClass touchSPI(VSPI);
SPIClass sdSPI(VSPI);

struct Button { int16_t x, y, w, h; const char *label; char key; };
Button buttons[] = {
  {8, 54, 96, 45, "DISPLAY", 'd'}, {112, 54, 96, 45, "TOUCH", 't'}, {216, 54, 96, 45, "SD CARD", 's'},
  {8, 107, 96, 45, "AUDIO", 'a'}, {112, 107, 96, 45, "BACKLIGHT", 'b'}, {216, 107, 96, 45, "SYSTEM", 'm'},
  {8, 160, 96, 45, "WI-FI", 'w'}, {112, 160, 200, 45, "RUN ALL", 'r'}
};

bool touchReady = false;

void beginTouchBus() {
  touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  pinMode(TOUCH_IRQ, INPUT);
  touchReady = true;
}

void endSDBusAndRestoreTouch() {
  SD.end();
  sdSPI.end();
  beginTouchBus();
}

void header(const char *title, uint16_t color = TFT_CYAN) {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, tft.width(), 38, color);
  tft.setTextColor(TFT_BLACK, color);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(title, tft.width() / 2, 19, 4);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

void line(const String &text, uint16_t color = TFT_WHITE) {
  static int16_t y = 44;
  if (text == "\f") { y = 44; return; }
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(text, 8, y, 2);
  Serial.println(text);
  y += 20;
  if (y > tft.height() - 18) y = 44;
}

void waitBrief(uint32_t ms = 1000) {
  uint32_t start = millis();
  while (millis() - start < ms) delay(5);
}

void drawButton(const Button &b, bool pressed = false) {
  uint16_t fill = pressed ? TFT_YELLOW : TFT_DARKGREY;
  uint16_t border = pressed ? TFT_WHITE : TFT_LIGHTGREY;
  uint16_t text = pressed ? TFT_BLACK : TFT_WHITE;
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 5, fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 5, border);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(text, fill);
  tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2, 2);
  tft.setTextDatum(TL_DATUM);
}

int buttonAt(int16_t x, int16_t y) {
  for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
    const Button &b = buttons[i];
    if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return (int)i;
  }
  return -1;
}

bool readTouch(int16_t &x, int16_t &y) {
  if (!touchReady || digitalRead(TOUCH_IRQ) != LOW) return false;
  uint32_t sx = 0, sy = 0;
  touchSPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(TOUCH_CS, LOW);
  for (int i = 0; i < 4; ++i) {
    touchSPI.transfer(0xD0); // X position, 12-bit result
    sx += ((touchSPI.transfer(0) << 8) | touchSPI.transfer(0)) >> 3;
    touchSPI.transfer(0x90); // Y position, 12-bit result
    sy += ((touchSPI.transfer(0) << 8) | touchSPI.transfer(0)) >> 3;
  }
  digitalWrite(TOUCH_CS, HIGH);
  touchSPI.endTransaction();
  int32_t rx = sx / 4, ry = sy / 4;
#if TOUCH_SWAP_XY
  int32_t tmp = rx; rx = ry; ry = tmp;
#endif
  x = map(rx, TOUCH_MIN_X, TOUCH_MAX_X, 0, tft.width() - 1);
  y = map(ry, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, tft.height() - 1);
#if TOUCH_INVERT_X
  x = tft.width() - 1 - x;
#endif
#if TOUCH_INVERT_Y
  y = tft.height() - 1 - y;
#endif
  x = constrain(x, 0, tft.width() - 1);
  y = constrain(y, 0, tft.height() - 1);
  return true;
}

void showMenu() {
  header("CYD FUNCTION TEST", TFT_YELLOW);
  for (auto &b : buttons) drawButton(b);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("Touch a test or use Serial: d t s a b m w r", 8, 220, 2);
}

void displayTest() {
  header("DISPLAY + ANIMATION");
  const uint16_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK};
  const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
  for (int i = 0; i < 5; ++i) {
    tft.fillScreen(colors[i]);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(i == 3 ? TFT_BLACK : TFT_WHITE, colors[i]);
    tft.drawString(names[i], tft.width()/2, tft.height()/2, 4);
    waitBrief(350);
  }
  uint32_t start = millis();
  uint32_t frames = 0;
  while (millis() - start < 2500) {
    int x = (frames * 7) % (tft.width() - 42);
    int y = 45 + (frames * 5) % (tft.height() - 90);
    tft.fillScreen(TFT_BLACK);
    tft.fillCircle(x + 21, y, 20, tft.color565((frames*9)%255, (frames*5)%255, (frames*13)%255));
    ++frames;
  }
  header("DISPLAY RESULT"); line("\f");
  line("Color bars: visual check", TFT_GREEN);
  line("Animation: " + String(frames * 1000.0f / (millis() - start), 1) + " FPS", TFT_GREEN);
  waitBrief(1400);
}

void touchTest() {
  header("TOUCH TEST"); line("\f");
  if (!touchReady) { line("FAIL: controller not responding", TFT_RED); waitBrief(1500); return; }
  line("Draw on all four corners.", TFT_YELLOW);
  line("Ends automatically in 8 seconds.");
  uint32_t start = millis(); int hits = 0;
  while (millis() - start < 8000) {
    int16_t x, y;
    if (readTouch(x, y)) {
      tft.fillCircle(x, y, 3, TFT_GREEN); ++hits;
      Serial.printf("touch x=%d y=%d\n", x, y);
      delay(12);
    }
  }
  Serial.printf("Touch samples: %d\n", hits);
}

bool beginSD() {
  // Touch and SD use the same ESP32 SPI peripheral with different pins.
  touchSPI.end();
  sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  return SD.begin(SD_CS, sdSPI, 20000000);
}

void sdTest() {
  header("SD CARD TEST"); line("\f");
  if (!beginSD()) { line("FAIL: card not mounted", TFT_RED); endSDBusAndRestoreTouch(); waitBrief(1800); return; }
  uint64_t sizeMB = SD.cardSize() / (1024ULL * 1024ULL);
  line("Mounted: " + String((uint32_t)sizeMB) + " MB", TFT_GREEN);
  SD.remove("/cyd_test.bin");
  File f = SD.open("/cyd_test.bin", FILE_WRITE);
  if (!f) { line("FAIL: cannot create file", TFT_RED); endSDBusAndRestoreTouch(); waitBrief(1800); return; }
  uint8_t block[512]; for (size_t i=0; i<sizeof(block); ++i) block[i] = i & 0xff;
  uint32_t start = millis(); size_t total = 0;
  for (int i=0; i<256; ++i) total += f.write(block, sizeof(block));
  f.close(); uint32_t elapsed = max(1UL, millis() - start);
  line("Write/read file: " + String(total == 131072 ? "PASS" : "FAIL"), total == 131072 ? TFT_GREEN : TFT_RED);
  line("Write: " + String(total / 1024.0f / (elapsed / 1000.0f), 1) + " KB/s");
  f = SD.open("/cyd_test.bin", FILE_READ); size_t checked = 0; bool valid = !!f;
  while (valid && f.available()) { size_t n=f.read(block,sizeof(block)); for(size_t i=0;i<n;++i) if(block[i]!=(i&0xff)){valid=false;break;} checked+=n; }
  if (f) f.close(); SD.remove("/cyd_test.bin");
  line("Verify: " + String(valid && checked == total ? "PASS" : "FAIL"), valid && checked == total ? TFT_GREEN : TFT_RED);
  endSDBusAndRestoreTouch();
  waitBrief(1800);
}

void audioTest() {
  header("AUDIO TEST"); line("\f");
  line("Playing 440 / 660 / 880 Hz", TFT_YELLOW);
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  cfg.sample_rate = 22050; cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  cfg.intr_alloc_flags = 0; cfg.dma_buf_count = 4; cfg.dma_buf_len = 256; cfg.use_apll = false;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) { line("FAIL: I2S/DAC init", TFT_RED); waitBrief(); return; }
  // ESP32 DAC channel 2 / I2S "left" is GPIO 26.
  i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
  uint16_t samples[256];
  for (int freq : {440, 660, 880}) {
    for (int chunk=0; chunk<35; ++chunk) {
      for (int i=0;i<256;++i) { uint8_t v=128 + 70*sin(2*PI*freq*(chunk*256+i)/22050.0); samples[i]=((uint16_t)v)<<8; }
      size_t written; i2s_write(I2S_NUM_0, samples, sizeof(samples), &written, portMAX_DELAY);
    }
  }
  i2s_zero_dma_buffer(I2S_NUM_0); i2s_driver_uninstall(I2S_NUM_0);
  line("Tone sent to GPIO " + String(AUDIO_DAC_PIN), TFT_GREEN);
  line("PASS requires audible tones."); waitBrief(1300);
}

void backlightTest() {
  header("BACKLIGHT TEST"); line("\f"); line("Fading backlight...", TFT_YELLOW);
  ledcSetup(0, 5000, 8); ledcAttachPin(TFT_BACKLIGHT_PIN, 0);
  for(int v=255;v>=10;v-=5){ledcWrite(0,v);delay(18);} for(int v=10;v<=255;v+=5){ledcWrite(0,v);delay(18);}
  ledcWrite(0,255); line("Visual check complete", TFT_GREEN); waitBrief(900);
}

void systemTest() {
  header("SYSTEM TEST"); line("\f");
  line("Chip: " + String(ESP.getChipModel()), TFT_GREEN);
  line("CPU: " + String(ESP.getCpuFreqMHz()) + " MHz");
  line("Flash: " + String(ESP.getFlashChipSize()/1048576) + " MB");
  line("Heap free: " + String(ESP.getFreeHeap()/1024) + " KB");
  line("PSRAM: " + String(ESP.getPsramSize()/1048576) + " MB");
  line("Uptime: " + String(millis()/1000) + " sec"); waitBrief(1800);
}

void wifiTest() {
  header("WI-FI SCAN"); line("\f"); line("Scanning...", TFT_YELLOW);
  WiFi.mode(WIFI_STA); WiFi.disconnect(true); delay(100);
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) line("FAIL: scan error", TFT_RED);
  else { line(String(n) + " networks found", TFT_GREEN); for(int i=0;i<min(n,6);++i) line(String(WiFi.RSSI(i)) + " dBm  " + WiFi.SSID(i)); }
  WiFi.scanDelete(); WiFi.mode(WIFI_OFF); waitBrief(1800);
}

void runTest(char key) {
  Serial.printf("\n--- test %c ---\n", key);
  switch(key) {
    case 'd': displayTest(); break; case 't': touchTest(); break; case 's': sdTest(); break;
    case 'a': audioTest(); break; case 'b': backlightTest(); break; case 'm': systemTest(); break;
    case 'w': wifiTest(); break;
    case 'r': displayTest(); touchTest(); sdTest(); audioTest(); backlightTest(); systemTest(); wifiTest(); break;
    default: return;
  }
  showMenu();
}

void setup() {
  Serial.begin(115200); delay(250);
  pinMode(TFT_BACKLIGHT_PIN, OUTPUT); digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
  tft.init(); tft.setRotation(DISPLAY_ROTATION); tft.setTextSize(1);
  beginTouchBus();
  Serial.println("CYD basic function tester ready");
  Serial.printf("Touch controller: %s\n", touchReady ? "ready" : "not detected");
  showMenu();
}

void loop() {
  if (Serial.available()) runTest(tolower(Serial.read()));
  int16_t x, y;
  if (readTouch(x, y)) {
    const int initialButton = buttonAt(x, y);
    int highlightedButton = initialButton;
    if (highlightedButton >= 0) drawButton(buttons[highlightedButton], true);

    // A touch is only accepted on stylus-up. Moving outside the button
    // cancels the highlight and prevents an accidental test launch.
    while (digitalRead(TOUCH_IRQ) == LOW) {
      int16_t currentX, currentY;
      if (readTouch(currentX, currentY)) {
        int currentButton = buttonAt(currentX, currentY);
        int wantedHighlight = currentButton == initialButton ? initialButton : -1;
        if (wantedHighlight != highlightedButton) {
          if (highlightedButton >= 0) drawButton(buttons[highlightedButton], false);
          if (wantedHighlight >= 0) drawButton(buttons[wantedHighlight], true);
          highlightedButton = wantedHighlight;
        }
      }
      delay(12);
    }

    if (highlightedButton >= 0) {
      char key = buttons[highlightedButton].key;
      drawButton(buttons[highlightedButton], false);
      delay(40);
      runTest(key);
    }
  }
  delay(10);
}
