#include <Arduino.h>

#ifdef LCDWIKI_ES3C28P
extern "C" void cyd_hw_tone_init(void) {}
extern "C" void cyd_hw_tone(uint16_t) {}
extern "C" uint32_t cyd_hw_millis(void) { return millis(); }
extern "C" void cyd_hw_pcm_play_channel(uint8_t, const uint8_t *, uint32_t) {}
extern "C" void cyd_hw_pcm_play_segments_channel(uint8_t, const uint8_t **, const uint32_t *, uint8_t) {}
extern "C" bool cyd_hw_pcm_active_channel(uint8_t) { return false; }
extern "C" void cyd_hw_pcm_stop_channel(uint8_t) {}
extern "C" void cyd_hw_pc_speaker_play(const uint8_t *, uint32_t) {}
extern "C" bool cyd_hw_pc_speaker_active(void) { return false; }
extern "C" void cyd_hw_pc_speaker_stop(void) {}
extern "C" void cyd_hw_audio_volume_channel(uint8_t, uint8_t) {}
extern "C" void cyd_hw_music_clear_buffer(void) {}
extern "C" void cyd_hw_music_volume(uint8_t) {}
extern "C" void cyd_hw_music_reset_opl(void) {}
extern "C" void cyd_hw_music_start(const char *, int32_t, int32_t) {}
extern "C" void cyd_hw_music_stop(void) {}
#endif
