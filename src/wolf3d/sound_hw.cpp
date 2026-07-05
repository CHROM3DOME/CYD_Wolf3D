#include <Arduino.h>
#include <driver/dac.h>
#include <fcntl.h>
#include <unistd.h>
#include "board_config.h"
#include "opl3.h"

extern "C" {
int wolf_open(const char *path, int flags, ...);
int wolf_close(int fd);
int32_t wolf_read(int fd, void *buffer, size_t count);
int32_t wolf_lseek(int fd, int32_t offset, int whence);
}

#define open wolf_open
#define close wolf_close
#define read wolf_read
#define lseek wolf_lseek

#ifndef AUDIO_DAC_PIN
#define AUDIO_DAC_PIN 26
#endif

#ifndef CYD_WOLF_SOUND_DUTY
#define CYD_WOLF_SOUND_DUTY 48
#endif

// Exposed music sequencer variables for external linkage with sound_stub
extern "C" {
volatile bool cyd_music_active = false;
volatile uint8_t cyd_music_volume = 128; // Default level (max)
volatile int32_t cyd_music_file_start = -1;
volatile int32_t cyd_music_file_len = 0;
}

namespace {
constexpr int kPcmMaxSegments = 2;
constexpr uint32_t kImfTickRate = 700;
constexpr uint32_t kWolfPcmSampleRate = 7000;
constexpr uint32_t kAudioSampleRate = 49700;
constexpr uint32_t kAudioTimerClockHz = 40000000UL;
constexpr uint32_t kAudioTimerPeriodTicks = (kAudioTimerClockHz + (kAudioSampleRate / 2)) / kAudioSampleRate;
constexpr int kMusicSamplesPerTick = kAudioSampleRate / kImfTickRate;
constexpr uint32_t kPcmSourceStepQ16 = (kWolfPcmSampleRate << 16) / kAudioSampleRate;
constexpr uint16_t kPcSpeakerTicksPerStep = (kAudioSampleRate + 70) / 140;
bool ready = false;
hw_timer_t *pcmTimer = nullptr;
portMUX_TYPE pcmMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE oplMux = portMUX_INITIALIZER_UNLOCKED;
constexpr int kMaxChannels = 2;

// OPL3 emulator state
opl3_chip oplChip;
TaskHandle_t oplTaskHandle = nullptr;

// Thread-safe lock-free ring buffer for OPL3 music audio stream
constexpr int kMusicRingBufSize = 2048;
volatile int16_t musicRingBuf[kMusicRingBufSize];
volatile int musicRingBufHead = 0;
volatile int musicRingBufTail = 0;

struct Channel {
  volatile const uint8_t *segments[kPcmMaxSegments] = {};
  volatile uint32_t segmentLengths[kPcmMaxSegments] = {};
  volatile uint8_t segmentCount = 0;
  volatile uint8_t segmentIndex = 0;
  volatile const uint8_t *data = nullptr;
  volatile uint32_t length = 0;
  volatile uint32_t pos = 0;
  volatile uint32_t posFrac = 0;
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
int32_t musicHighPassPrevIn = 0;
int32_t musicHighPassPrevOut = 0;

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

bool cyd_music_ringbuf_push(int16_t sample) {
  int nextHead = (musicRingBufHead + 1) % kMusicRingBufSize;
  if (nextHead == musicRingBufTail) {
    return false; // Ring buffer full
  }
  musicRingBuf[musicRingBufHead] = sample;
  musicRingBufHead = nextHead;
  return true;
}

int cyd_music_ringbuf_free_space() {
  int head = musicRingBufHead;
  int tail = musicRingBufTail;
  if (head >= tail) {
    return kMusicRingBufSize - 1 - (head - tail);
  } else {
    return tail - head - 1;
  }
}

// Static state for streaming IMF playback on Core 0
static int cyd_music_fd = -1;
static int32_t cyd_music_file_pos = 0;
static int32_t cyd_music_file_bytes_read = 0;

static uint16_t cyd_music_buf[128];
static int cyd_music_buf_index = 0;
static int cyd_music_buf_avail = 0;

static uint32_t cyd_music_tick_count = 0;
static uint32_t cyd_music_seq_time = 0;

static void cyd_music_rewind_sequence() {
  if (cyd_music_fd == -1) return;
  lseek(cyd_music_fd, cyd_music_file_start, SEEK_SET);
  cyd_music_file_pos = cyd_music_file_start;
  cyd_music_file_bytes_read = 0;
  cyd_music_buf_index = 0;
  cyd_music_buf_avail = 0;
  cyd_music_tick_count = 0;
  cyd_music_seq_time = 0;
}

static bool cyd_music_read_word(uint16_t *val) {
  if (cyd_music_fd == -1) return false;

  if (cyd_music_buf_index >= cyd_music_buf_avail) {
    if (cyd_music_file_bytes_read >= cyd_music_file_len) {
      cyd_music_rewind_sequence();
    }
    
    int32_t remaining = cyd_music_file_len - cyd_music_file_bytes_read;
    int32_t to_read = sizeof(cyd_music_buf);
    if (to_read > remaining) {
      to_read = remaining;
    }
    
    if (to_read <= 0) return false;
    
    int bytes = read(cyd_music_fd, cyd_music_buf, to_read);
    if (bytes <= 0) return false;
    
    cyd_music_file_bytes_read += bytes;
    cyd_music_buf_index = 0;
    cyd_music_buf_avail = bytes / 2;
  }
  
  if (cyd_music_buf_index < cyd_music_buf_avail) {
    *val = cyd_music_buf[cyd_music_buf_index++];
    return true;
  }
  return false;
}

// OPL3 emulation task running on Core 0
void cyd_music_task(void *pvParameters) {
  int16_t stereoBuffer[kMusicSamplesPerTick * 2];
  
  while (true) {
    if (!cyd_music_active || cyd_music_fd == -1) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    
    const int free_space = cyd_music_ringbuf_free_space();
    const int ticks_to_process = free_space / kMusicSamplesPerTick;
    
    if (ticks_to_process <= 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    
    for (int t = 0; t < ticks_to_process; ++t) {
      // 1. Process 1 IMF sequencer tick (700 Hz)
      if (cyd_music_file_bytes_read >= cyd_music_file_len && cyd_music_buf_index >= cyd_music_buf_avail) {
        cyd_music_rewind_sequence();
      }

      while (cyd_music_file_bytes_read < cyd_music_file_len || cyd_music_buf_index < cyd_music_buf_avail) {
        if (cyd_music_seq_time > cyd_music_tick_count) break;
        
        uint16_t reg_val = 0;
        if (!cyd_music_read_word(&reg_val)) break;
        uint8_t reg = reg_val & 0xFF;
        uint8_t val = (reg_val >> 8) & 0xFF;
        
        portENTER_CRITICAL(&oplMux);
        OPL3_WriteReg(&oplChip, reg, val);
        portEXIT_CRITICAL(&oplMux);
        
        uint16_t delay = 0;
        if (!cyd_music_read_word(&delay)) break;
        cyd_music_seq_time = cyd_music_tick_count + delay;
      }
      cyd_music_tick_count++;
      
      // Generate one 700 Hz IMF tick worth of audio at the DAC rate.
      portENTER_CRITICAL(&oplMux);
      OPL3_GenerateStream(&oplChip, stereoBuffer, kMusicSamplesPerTick);
      portEXIT_CRITICAL(&oplMux);
      
      // 3. Convert to mono and push into the ring buffer
      for (int i = 0; i < kMusicSamplesPerTick; ++i) {
        int16_t left = stereoBuffer[i * 2];
        int32_t mono = left;
        int32_t filtered = mono - musicHighPassPrevIn + ((musicHighPassPrevOut * 255) >> 8);
        musicHighPassPrevIn = mono;
        musicHighPassPrevOut = filtered;
        if (filtered < -32768) filtered = -32768;
        if (filtered > 32767) filtered = 32767;

        cyd_music_ringbuf_push((int16_t)filtered);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void ensureOplTask() {
  if (oplTaskHandle) return;
  portENTER_CRITICAL(&oplMux);
  OPL3_Reset(&oplChip, kAudioSampleRate);
  portEXIT_CRITICAL(&oplMux);
  musicRingBufHead = 0;
  musicRingBufTail = 0;
  
  xTaskCreatePinnedToCore(
    cyd_music_task,
    "cyd_music_task",
    3072,
    nullptr,
    5,
    &oplTaskHandle,
    0 // Pinned to Core 0 (idle radio core)
  );
}

void IRAM_ATTR onPcmTimer() {
  portENTER_CRITICAL_ISR(&pcmMux);

  // Consume next music sample from ring buffer if available
  int16_t musicSample16 = 0;
  if (musicRingBufHead != musicRingBufTail) {
    musicSample16 = musicRingBuf[musicRingBufTail];
    musicRingBufTail = (musicRingBufTail + 1) % kMusicRingBufSize;
  }
  int musicOffset = (static_cast<int>(musicSample16) * cyd_music_volume * 2) / 32768;
  if (musicOffset < -96) musicOffset = -96;
  if (musicOffset > 96) musicOffset = 96;

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
      // Direct mix with music if pcSpeaker is silent
      int mixed = 128 + musicOffset;
      if (mixed < 0) mixed = 0;
      if (mixed > 255) mixed = 255;
      portEXIT_CRITICAL_ISR(&pcmMux);
      dac_output_voltage(DAC_CHANNEL_2, static_cast<uint8_t>(mixed));
      return;
    }

    pcPhase += pcToneStep;
    uint8_t sample = (pcPhase & 0x80000000UL)
      ? (uint8_t)(128 + pcAmplitude)
      : (uint8_t)(128 - pcAmplitude);
      
    int mixed = static_cast<int>(sample) - 128 + musicOffset + 128;
    if (mixed < 0) mixed = 0;
    if (mixed > 255) mixed = 255;
    portEXIT_CRITICAL_ISR(&pcmMux);
    dac_output_voltage(DAC_CHANNEL_2, static_cast<uint8_t>(mixed));
    return;
  }

  if (tonePlaying) {
    tonePhase += toneStep;
    uint8_t sample = (tonePhase & 0x80000000UL)
      ? (uint8_t)(128 + toneAmplitude)
      : (uint8_t)(128 - toneAmplitude);
      
    int mixed = static_cast<int>(sample) - 128 + musicOffset + 128;
    if (mixed < 0) mixed = 0;
    if (mixed > 255) mixed = 255;
    portEXIT_CRITICAL_ISR(&pcmMux);
    dac_output_voltage(DAC_CHANNEL_2, static_cast<uint8_t>(mixed));
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
          chan.posFrac = 0;
        }
      }
      if (!chan.data || chan.pos >= chan.length) {
        chan.playing = false;
      } else {
        uint8_t sample = chan.data[chan.pos];
        chan.posFrac += kPcmSourceStepQ16;
        while (chan.posFrac >= 0x10000UL) {
          chan.pos++;
          chan.posFrac -= 0x10000UL;
        }
        int signedSample = static_cast<int>(sample) - 128;
        mixedSample += (signedSample * chan.volume) / 256;
        activeCount++;
      }
    }
  }

  mixedSample += musicOffset;
  if (musicOffset != 0) {
    activeCount++;
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
  pcmTimer = timerBegin(0, 2, true); // 40 MHz tick for high-rate audio precision.
  timerAttachInterrupt(pcmTimer, &onPcmTimer, true);
  timerAlarmWrite(pcmTimer, kAudioTimerPeriodTicks, true);
}
}

extern "C" void cyd_hw_tone_init(void) {
  if (ready) return;
  (void)AUDIO_DAC_PIN;

  // Explicitly initialize channels to default values to avoid garbage state
  for (int c = 0; c < kMaxChannels; ++c) {
    s_channels[c].playing = false;
    s_channels[c].volume = 192;
    s_channels[c].segmentCount = 0;
    s_channels[c].segmentIndex = 0;
    s_channels[c].data = nullptr;
    s_channels[c].length = 0;
    s_channels[c].pos = 0;
    s_channels[c].posFrac = 0;
    for (int i = 0; i < kPcmMaxSegments; ++i) {
      s_channels[c].segments[i] = nullptr;
      s_channels[c].segmentLengths[i] = 0;
    }
  }

  ensurePcmTimer();
  ensureOplTask();
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
    s_channels[c].posFrac = 0;
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
  chan.posFrac = 0;
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
  chan.posFrac = 0;
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
    s_channels[c].posFrac = 0;
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
  s_channels[channel].posFrac = 0;
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

extern "C" void cyd_hw_music_clear_buffer(void) {
  portENTER_CRITICAL(&pcmMux);
  musicRingBufHead = 0;
  musicRingBufTail = 0;
  musicHighPassPrevIn = 0;
  musicHighPassPrevOut = 0;
  portEXIT_CRITICAL(&pcmMux);
}

extern "C" void cyd_hw_music_volume(uint8_t volume) {
  if (volume > 128) volume = 128;
  cyd_music_volume = volume;
}

extern "C" void cyd_hw_music_reset_opl(void) {
  portENTER_CRITICAL(&oplMux);
  OPL3_Reset(&oplChip, kAudioSampleRate);
  portEXIT_CRITICAL(&oplMux);
}

extern "C" void cyd_hw_music_start(const char *filename, int32_t start, int32_t len) {
  cyd_hw_tone_init();
  cyd_music_active = false;
  
  if (cyd_music_fd != -1) {
    close(cyd_music_fd);
    cyd_music_fd = -1;
  }
  
  cyd_music_fd = open(filename, O_RDONLY);
  if (cyd_music_fd == -1) {
    Serial.printf("cyd_hw_music_start: failed to open %s\n", filename);
    return;
  }
  
  lseek(cyd_music_fd, start, SEEK_SET);
  
  uint16_t first_word = 0;
  if (read(cyd_music_fd, &first_word, 2) != 2) {
    Serial.printf("cyd_hw_music_start: failed to read first word\n");
    close(cyd_music_fd);
    cyd_music_fd = -1;
    return;
  }
  
  if (first_word == 0) {
    lseek(cyd_music_fd, start, SEEK_SET);
    cyd_music_file_start = start;
    cyd_music_file_len = len;
  } else {
    cyd_music_file_start = start + 2;
    cyd_music_file_len = first_word;
  }
  
  Serial.printf("cyd_hw_music_start: playing %s, start=%d, len=%d (first_word=%d)\n", 
                filename, (int)cyd_music_file_start, (int)cyd_music_file_len, (int)first_word);

  cyd_music_file_pos = cyd_music_file_start;
  cyd_music_file_bytes_read = 0;
  
  cyd_music_buf_index = 0;
  cyd_music_buf_avail = 0;
  
  cyd_music_tick_count = 0;
  cyd_music_seq_time = 0;
  
  cyd_music_active = true;
}

extern "C" void cyd_hw_music_stop(void) {
  cyd_music_active = false;
  if (cyd_music_fd != -1) {
    close(cyd_music_fd);
    cyd_music_fd = -1;
  }
}
