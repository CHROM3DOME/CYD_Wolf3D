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
constexpr int kPcmMaxSegments = 2;
bool ready = false;
hw_timer_t *pcmTimer = nullptr;
portMUX_TYPE pcmMux = portMUX_INITIALIZER_UNLOCKED;
volatile const uint8_t *pcmData = nullptr;
volatile uint32_t pcmLength = 0;
volatile uint32_t pcmPos = 0;
volatile const uint8_t *pcmSegments[kPcmMaxSegments] = {};
volatile uint32_t pcmSegmentLengths[kPcmMaxSegments] = {};
volatile uint8_t pcmSegmentCount = 0;
volatile uint8_t pcmSegmentIndex = 0;
volatile bool pcmPlaying = false;
volatile uint8_t audioVolume = 192;

void IRAM_ATTR onPcmTimer() {
  portENTER_CRITICAL_ISR(&pcmMux);
  if (!pcmPlaying || !pcmData || pcmPos >= pcmLength) {
    while (pcmPlaying && pcmSegmentCount && pcmSegmentIndex + 1 < pcmSegmentCount && pcmPos >= pcmLength) {
      ++pcmSegmentIndex;
      pcmData = pcmSegments[pcmSegmentIndex];
      pcmLength = pcmSegmentLengths[pcmSegmentIndex];
      pcmPos = 0;
    }
  }
  if (!pcmPlaying || !pcmData || pcmPos >= pcmLength) {
    pcmPlaying = false;
    portEXIT_CRITICAL_ISR(&pcmMux);
    dac_output_voltage(DAC_CHANNEL_2, 128);
    return;
  }
  uint8_t sample = pcmData[pcmPos++];
  sample = 128 + ((static_cast<int>(sample) - 128) * audioVolume) / 256;
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

extern "C" void cyd_hw_audio_volume(uint8_t volume) {
  if (volume > 224) volume = 224;
  portENTER_CRITICAL(&pcmMux);
  audioVolume = volume;
  portEXIT_CRITICAL(&pcmMux);
}

extern "C" void cyd_hw_tone(uint16_t frequency) {
  cyd_hw_tone_init();
  if (!frequency) {
    ledcWriteTone(kToneChannel, 0);
    ledcWrite(kToneChannel, 0);
    return;
  }
  ledcWriteTone(kToneChannel, frequency);
  uint32_t duty = ((uint32_t)CYD_WOLF_SOUND_DUTY * audioVolume) / 192;
  if (duty > CYD_WOLF_SOUND_DUTY) duty = CYD_WOLF_SOUND_DUTY;
  if (duty < 4) duty = 4;
  ledcWrite(kToneChannel, duty);
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
  pcmSegments[0] = data;
  pcmSegmentLengths[0] = length;
  pcmSegmentCount = 1;
  pcmSegmentIndex = 0;
  for (int i = 1; i < kPcmMaxSegments; ++i) {
    pcmSegments[i] = nullptr;
    pcmSegmentLengths[i] = 0;
  }
  pcmPlaying = true;
  portEXIT_CRITICAL(&pcmMux);
  timerAlarmEnable(pcmTimer);
}

extern "C" void cyd_hw_pcm_play_segments(const uint8_t **segments, const uint32_t *lengths, uint8_t count) {
  if (!segments || !lengths || !count) return;
  if (count > kPcmMaxSegments) count = kPcmMaxSegments;
  ensurePcmTimer();
  ledcWriteTone(kToneChannel, 0);
  ledcWrite(kToneChannel, 0);
  portENTER_CRITICAL(&pcmMux);
  for (uint8_t i = 0; i < count; ++i) {
    pcmSegments[i] = segments[i];
    pcmSegmentLengths[i] = lengths[i];
  }
  for (uint8_t i = count; i < kPcmMaxSegments; ++i) {
    pcmSegments[i] = nullptr;
    pcmSegmentLengths[i] = 0;
  }
  pcmSegmentCount = count;
  pcmSegmentIndex = 0;
  pcmData = pcmSegments[0];
  pcmLength = pcmSegmentLengths[0];
  pcmPos = 0;
  pcmPlaying = pcmData && pcmLength;
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
  pcmSegmentCount = 0;
  pcmSegmentIndex = 0;
  portEXIT_CRITICAL(&pcmMux);
  dac_output_voltage(DAC_CHANNEL_2, 128);
}
