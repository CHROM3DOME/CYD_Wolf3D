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
constexpr int kMaxChannels = 2;

struct Channel {
  volatile const uint8_t *segments[kPcmMaxSegments] = {};
  volatile uint32_t segmentLengths[kPcmMaxSegments] = {};
  volatile uint8_t segmentCount = 0;
  volatile uint8_t segmentIndex = 0;
  volatile const uint8_t *data = nullptr;
  volatile uint32_t length = 0;
  volatile uint32_t pos = 0;
  volatile bool playing = false;
  volatile uint8_t volume = 192;
};

volatile Channel s_channels[kMaxChannels];
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

  int mixedSample = 0;
  int activeCount = 0;

  for (int c = 0; c < kMaxChannels; ++c) {
    volatile Channel &chan = s_channels[c];
    if (chan.playing) {
      if (!chan.data || chan.pos >= chan.length) {
        while (chan.playing && chan.segmentCount && chan.segmentIndex + 1 < chan.segmentCount && chan.pos >= chan.length) {
          chan.segmentIndex++;
          chan.data = chan.segments[chan.segmentIndex];
          chan.length = chan.segmentLengths[chan.segmentIndex];
          chan.pos = 0;
        }
      }
      if (!chan.data || chan.pos >= chan.length) {
        chan.playing = false;
      } else {
        uint8_t sample = chan.data[chan.pos++];
        int signedSample = static_cast<int>(sample) - 128;
        mixedSample += (signedSample * chan.volume) / 256;
        activeCount++;
      }
    }
  }

  if (activeCount == 0) {
    portEXIT_CRITICAL_ISR(&pcmMux);
    dac_output_voltage(DAC_CHANNEL_2, 128);
    return;
  }

  mixedSample += 128;
  if (mixedSample < 0) mixedSample = 0;
  if (mixedSample > 255) mixedSample = 255;

  portEXIT_CRITICAL_ISR(&pcmMux);
  dac_output_voltage(DAC_CHANNEL_2, static_cast<uint8_t>(mixedSample));
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

extern "C" void cyd_hw_audio_volume_channel(uint8_t channel, uint8_t volume) {
  if (channel >= kMaxChannels) return;
  if (volume > 224) volume = 224;
  portENTER_CRITICAL(&pcmMux);
  s_channels[channel].volume = volume;
  if (channel == 0) {
    toneAmplitude = amplitudeFromVolume(volume);
    pcAmplitude = pcAmplitudeFromVolume(volume);
  }
  portEXIT_CRITICAL(&pcmMux);
}

extern "C" void cyd_hw_audio_volume(uint8_t volume) {
  cyd_hw_audio_volume_channel(0, volume);
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
  for (int c = 0; c < kMaxChannels; ++c) {
    s_channels[c].playing = false;
    s_channels[c].data = nullptr;
    s_channels[c].length = 0;
    s_channels[c].pos = 0;
    s_channels[c].segmentCount = 0;
    s_channels[c].segmentIndex = 0;
  }
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

extern "C" void cyd_hw_pcm_play_channel(uint8_t channel, const uint8_t *data, uint32_t length) {
  if (channel >= kMaxChannels || !data || !length) return;
  dac_output_enable(DAC_CHANNEL_2);
  ensurePcmTimer();
  portENTER_CRITICAL(&pcmMux);
  tonePlaying = false;
  pcPlaying = false;
  
  volatile Channel &chan = s_channels[channel];
  chan.data = data;
  chan.length = length;
  chan.pos = 0;
  chan.segments[0] = data;
  chan.segmentLengths[0] = length;
  chan.segmentCount = 1;
  chan.segmentIndex = 0;
  for (int i = 1; i < kPcmMaxSegments; ++i) {
    chan.segments[i] = nullptr;
    chan.segmentLengths[i] = 0;
  }
  chan.playing = true;
  portEXIT_CRITICAL(&pcmMux);
  timerAlarmEnable(pcmTimer);
}

extern "C" void cyd_hw_pcm_play(const uint8_t *data, uint32_t length) {
  cyd_hw_pcm_play_channel(0, data, length);
}

extern "C" void cyd_hw_pcm_play_segments_channel(uint8_t channel, const uint8_t **segments, const uint32_t *lengths, uint8_t count) {
  if (channel >= kMaxChannels || !segments || !lengths || !count) return;
  if (count > kPcmMaxSegments) count = kPcmMaxSegments;
  dac_output_enable(DAC_CHANNEL_2);
  ensurePcmTimer();
  portENTER_CRITICAL(&pcmMux);
  tonePlaying = false;
  pcPlaying = false;
  
  volatile Channel &chan = s_channels[channel];
  for (uint8_t i = 0; i < count; ++i) {
    chan.segments[i] = segments[i];
    chan.segmentLengths[i] = lengths[i];
  }
  for (uint8_t i = count; i < kPcmMaxSegments; ++i) {
    chan.segments[i] = nullptr;
    chan.segmentLengths[i] = 0;
  }
  chan.segmentCount = count;
  chan.segmentIndex = 0;
  chan.data = chan.segments[0];
  chan.length = chan.segmentLengths[0];
  chan.pos = 0;
  chan.playing = chan.data && chan.length;
  portEXIT_CRITICAL(&pcmMux);
  timerAlarmEnable(pcmTimer);
}

extern "C" void cyd_hw_pcm_play_segments(const uint8_t **segments, const uint32_t *lengths, uint8_t count) {
  cyd_hw_pcm_play_segments_channel(0, segments, lengths, count);
}

extern "C" void cyd_hw_pc_speaker_play(const uint8_t *data, uint32_t length) {
  if (!data || !length) return;
  dac_output_enable(DAC_CHANNEL_2);
  ensurePcmTimer();
  portENTER_CRITICAL(&pcmMux);
  tonePlaying = false;
  for (int c = 0; c < kMaxChannels; ++c) {
    s_channels[c].playing = false;
    s_channels[c].data = nullptr;
    s_channels[c].length = 0;
    s_channels[c].pos = 0;
    s_channels[c].segmentCount = 0;
    s_channels[c].segmentIndex = 0;
  }
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

extern "C" bool cyd_hw_pcm_active_channel(uint8_t channel) {
  if (channel >= kMaxChannels) return false;
  return s_channels[channel].playing;
}

extern "C" bool cyd_hw_pcm_active(void) {
  return cyd_hw_pcm_active_channel(0) || cyd_hw_pcm_active_channel(1);
}

extern "C" void cyd_hw_pcm_stop_channel(uint8_t channel) {
  if (channel >= kMaxChannels || !pcmTimer) return;
  bool silenceDac;
  portENTER_CRITICAL(&pcmMux);
  s_channels[channel].playing = false;
  s_channels[channel].data = nullptr;
  s_channels[channel].length = 0;
  s_channels[channel].pos = 0;
  s_channels[channel].segmentCount = 0;
  s_channels[channel].segmentIndex = 0;
  
  bool anyPlaying = false;
  for (int c = 0; c < kMaxChannels; ++c) {
    if (s_channels[c].playing) anyPlaying = true;
  }
  silenceDac = !tonePlaying && !pcPlaying && !anyPlaying;
  portEXIT_CRITICAL(&pcmMux);
  if (silenceDac)
    dac_output_voltage(DAC_CHANNEL_2, 128);
}

extern "C" void cyd_hw_pcm_stop(void) {
  cyd_hw_pcm_stop_channel(0);
  cyd_hw_pcm_stop_channel(1);
}
