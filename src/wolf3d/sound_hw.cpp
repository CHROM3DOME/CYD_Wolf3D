#include <Arduino.h>
#include <driver/dac.h>
#include "board_config.h"

#ifndef AUDIO_DAC_PIN
#define AUDIO_DAC_PIN 26
#endif

#ifndef CYD_WOLF_SOUND_DUTY
#define CYD_WOLF_SOUND_DUTY 48
#endif

namespace {
constexpr int kToneChannel = 7;
constexpr int kToneResolutionBits = 10;
bool ready = false;
hw_timer_t *pcmTimer = nullptr;
portMUX_TYPE pcmMux = portMUX_INITIALIZER_UNLOCKED;
volatile const uint8_t *pcmData = nullptr;
volatile uint32_t pcmLength = 0;
volatile uint32_t pcmPos = 0;
volatile bool pcmPlaying = false;

void IRAM_ATTR onPcmTimer() {
  portENTER_CRITICAL_ISR(&pcmMux);
  if (!pcmPlaying || !pcmData || pcmPos >= pcmLength) {
    pcmPlaying = false;
    portEXIT_CRITICAL_ISR(&pcmMux);
    dac_output_voltage(DAC_CHANNEL_2, 128);
    return;
  }
  uint8_t sample = pcmData[pcmPos++];
  // Slight attenuation around center; the CYD amp is not shy.
  sample = 128 + ((static_cast<int>(sample) - 128) * 3) / 4;
  portEXIT_CRITICAL_ISR(&pcmMux);
  dac_output_voltage(DAC_CHANNEL_2, sample);
}

void ensurePcmTimer() {
  if (pcmTimer) return;
  dac_output_enable(DAC_CHANNEL_2);
  dac_output_voltage(DAC_CHANNEL_2, 128);
  pcmTimer = timerBegin(0, 80, true); // 1 MHz tick
  timerAttachInterrupt(pcmTimer, &onPcmTimer, true);
  timerAlarmWrite(pcmTimer, 143, true); // ~6993 Hz, close to Wolf's 7000 Hz PCM
}
}

extern "C" void cyd_hw_tone_init(void) {
  if (ready) return;
  ledcSetup(kToneChannel, 2000, kToneResolutionBits);
  ledcAttachPin(AUDIO_DAC_PIN, kToneChannel);
  ledcWriteTone(kToneChannel, 0);
  ledcWrite(kToneChannel, 0);
  ready = true;
}

extern "C" void cyd_hw_tone(uint16_t frequency) {
  cyd_hw_tone_init();
  if (!frequency) {
    ledcWriteTone(kToneChannel, 0);
    ledcWrite(kToneChannel, 0);
    return;
  }
  ledcWriteTone(kToneChannel, frequency);
  ledcWrite(kToneChannel, CYD_WOLF_SOUND_DUTY);
}

extern "C" uint32_t cyd_hw_millis(void) {
  return millis();
}

extern "C" void cyd_hw_pcm_play(const uint8_t *data, uint32_t length) {
  if (!data || !length) return;
  ensurePcmTimer();
  ledcWriteTone(kToneChannel, 0);
  ledcWrite(kToneChannel, 0);
  portENTER_CRITICAL(&pcmMux);
  pcmData = data;
  pcmLength = length;
  pcmPos = 0;
  pcmPlaying = true;
  portEXIT_CRITICAL(&pcmMux);
  timerAlarmEnable(pcmTimer);
}

extern "C" bool cyd_hw_pcm_active(void) {
  return pcmPlaying;
}

extern "C" void cyd_hw_pcm_stop(void) {
  if (!pcmTimer) return;
  portENTER_CRITICAL(&pcmMux);
  pcmPlaying = false;
  pcmData = nullptr;
  pcmLength = 0;
  pcmPos = 0;
  portEXIT_CRITICAL(&pcmMux);
  dac_output_voltage(DAC_CHANNEL_2, 128);
}
