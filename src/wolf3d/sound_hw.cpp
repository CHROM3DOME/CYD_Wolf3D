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
constexpr int kPcmMaxSegments = 2;
constexpr uint32_t kAudioTimerPeriodUs = 143; // ~6993 Hz, close to Wolf's 7000 Hz PCM.
constexpr uint32_t kAudioSampleRate = 1000000UL / kAudioTimerPeriodUs;
constexpr uint16_t kPcSpeakerTicksPerStep = (kAudioSampleRate + 70) / 140;
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
volatile bool tonePlaying = false;
volatile uint32_t tonePhase = 0;
volatile uint32_t toneStep = 0;
volatile uint8_t toneAmplitude = 86;
volatile const uint8_t *pcData = nullptr;
volatile uint32_t pcLength = 0;
volatile uint32_t pcPos = 0;
volatile bool pcPlaying = false;
volatile uint16_t pcTicksLeft = 0;
volatile uint32_t pcToneStep = 0;
volatile uint32_t pcPhase = 0;
volatile uint8_t pcAmplitude = 28;

uint8_t amplitudeFromVolume(uint8_t volume) {
  uint32_t amplitude = (uint32_t)CYD_WOLF_SOUND_DUTY * 2;
  if (amplitude < 24) amplitude = 24;
  if (amplitude > 112) amplitude = 112;
  amplitude = (amplitude * volume) / 192;
  if (amplitude < 8) amplitude = 8;
  if (amplitude > 112) amplitude = 112;
  return (uint8_t)amplitude;
}

uint8_t pcAmplitudeFromVolume(uint8_t volume) {
  uint32_t amplitude = amplitudeFromVolume(volume) / 3;
  if (amplitude < 6) amplitude = 6;
  if (amplitude > 42) amplitude = 42;
  return (uint8_t)amplitude;
}

void IRAM_ATTR onPcmTimer() {
  portENTER_CRITICAL_ISR(&pcmMux);
  if (pcPlaying) {
    if (pcTicksLeft == 0) {
      if (!pcData || pcPos >= pcLength) {
        pcPlaying = false;
        pcData = nullptr;
        pcLength = 0;
        pcPos = 0;
        pcToneStep = 0;
        pcPhase = 0;
        portEXIT_CRITICAL_ISR(&pcmMux);
        dac_output_voltage(DAC_CHANNEL_2, 128);
        return;
      }

      uint8_t pcSample = pcData[pcPos++];
      if (pcSample) {
        uint32_t frequency = 1193180UL / ((uint32_t)pcSample * 60UL);
        pcToneStep = ((uint64_t)frequency << 32) / kAudioSampleRate;
      } else {
        pcToneStep = 0;
      }
      pcTicksLeft = kPcSpeakerTicksPerStep;
    }

    --pcTicksLeft;
    if (!pcToneStep) {
      portEXIT_CRITICAL_ISR(&pcmMux);
      dac_output_voltage(DAC_CHANNEL_2, 128);
      return;
    }

    pcPhase += pcToneStep;
    uint8_t sample = (pcPhase & 0x80000000UL)
      ? (uint8_t)(128 + pcAmplitude)
      : (uint8_t)(128 - pcAmplitude);
    portEXIT_CRITICAL_ISR(&pcmMux);
    dac_output_voltage(DAC_CHANNEL_2, sample);
    return;
  }

  if (tonePlaying) {
    tonePhase += toneStep;
    uint8_t sample = (tonePhase & 0x80000000UL)
      ? (uint8_t)(128 + toneAmplitude)
      : (uint8_t)(128 - toneAmplitude);
    portEXIT_CRITICAL_ISR(&pcmMux);
    dac_output_voltage(DAC_CHANNEL_2, sample);
    return;
  }

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
  timerAlarmWrite(pcmTimer, kAudioTimerPeriodUs, true);
}
}

extern "C" void cyd_hw_tone_init(void) {
  if (ready) return;
  (void)AUDIO_DAC_PIN;
  ensurePcmTimer();
  timerAlarmEnable(pcmTimer);
  ready = true;
}

extern "C" void cyd_hw_audio_volume(uint8_t volume) {
  if (volume > 224) volume = 224;
  portENTER_CRITICAL(&pcmMux);
  audioVolume = volume;
  toneAmplitude = amplitudeFromVolume(volume);
  pcAmplitude = pcAmplitudeFromVolume(volume);
  portEXIT_CRITICAL(&pcmMux);
}

extern "C" void cyd_hw_tone(uint16_t frequency) {
  cyd_hw_tone_init();
  if (!frequency) {
    portENTER_CRITICAL(&pcmMux);
    tonePlaying = false;
    tonePhase = 0;
    portEXIT_CRITICAL(&pcmMux);
    dac_output_voltage(DAC_CHANNEL_2, 128);
    return;
  }

  uint32_t step = ((uint64_t)frequency << 32) / kAudioSampleRate;
  if (!step) step = 1;
  portENTER_CRITICAL(&pcmMux);
  pcmPlaying = false;
  pcmData = nullptr;
  pcmLength = 0;
  pcmPos = 0;
  pcmSegmentCount = 0;
  pcmSegmentIndex = 0;
  pcPlaying = false;
  pcData = nullptr;
  pcLength = 0;
  pcPos = 0;
  pcTicksLeft = 0;
  pcToneStep = 0;
  pcPhase = 0;
  tonePhase = 0;
  toneStep = step;
  tonePlaying = true;
  portEXIT_CRITICAL(&pcmMux);
  timerAlarmEnable(pcmTimer);
}

extern "C" uint32_t cyd_hw_millis(void) {
  return millis();
}

extern "C" void cyd_hw_pcm_play(const uint8_t *data, uint32_t length) {
  if (!data || !length) return;
  dac_output_enable(DAC_CHANNEL_2);
  ensurePcmTimer();
  portENTER_CRITICAL(&pcmMux);
  tonePlaying = false;
  pcPlaying = false;
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
  dac_output_enable(DAC_CHANNEL_2);
  ensurePcmTimer();
  portENTER_CRITICAL(&pcmMux);
  tonePlaying = false;
  pcPlaying = false;
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

extern "C" void cyd_hw_pc_speaker_play(const uint8_t *data, uint32_t length) {
  if (!data || !length) return;
  dac_output_enable(DAC_CHANNEL_2);
  ensurePcmTimer();
  portENTER_CRITICAL(&pcmMux);
  tonePlaying = false;
  pcmPlaying = false;
  pcmData = nullptr;
  pcmLength = 0;
  pcmPos = 0;
  pcmSegmentCount = 0;
  pcmSegmentIndex = 0;
  pcData = data;
  pcLength = length;
  pcPos = 0;
  pcTicksLeft = 0;
  pcToneStep = 0;
  pcPhase = 0;
  pcPlaying = true;
  portEXIT_CRITICAL(&pcmMux);
  timerAlarmEnable(pcmTimer);
}

extern "C" bool cyd_hw_pc_speaker_active(void) {
  return pcPlaying;
}

extern "C" void cyd_hw_pc_speaker_stop(void) {
  if (!pcmTimer) return;
  portENTER_CRITICAL(&pcmMux);
  pcPlaying = false;
  pcData = nullptr;
  pcLength = 0;
  pcPos = 0;
  pcTicksLeft = 0;
  pcToneStep = 0;
  pcPhase = 0;
  portEXIT_CRITICAL(&pcmMux);
  dac_output_voltage(DAC_CHANNEL_2, 128);
}

extern "C" bool cyd_hw_pcm_active(void) {
  return pcmPlaying;
}

extern "C" void cyd_hw_pcm_stop(void) {
  if (!pcmTimer) return;
  bool silenceDac;
  portENTER_CRITICAL(&pcmMux);
  pcmPlaying = false;
  pcmData = nullptr;
  pcmLength = 0;
  pcmPos = 0;
  pcmSegmentCount = 0;
  pcmSegmentIndex = 0;
  silenceDac = !tonePlaying && !pcPlaying;
  portEXIT_CRITICAL(&pcmMux);
  if (silenceDac)
    dac_output_voltage(DAC_CHANNEL_2, 128);
}
