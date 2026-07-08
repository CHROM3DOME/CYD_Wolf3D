#include <wl_def.h>
#include "board_config.h"

extern "C" void furi_log_print_format(int, const char *, const char *, ...);
extern "C" void cyd_trace_sound(int sound, int usedPcm);

#ifndef CYD_WOLF_BASIC_SOUND
#define CYD_WOLF_BASIC_SOUND 0
#endif

#ifndef CYD_WOLF_SOUND_TRACE
#define CYD_WOLF_SOUND_TRACE 0
#endif

#ifndef AUDIO_DAC_PIN
#define AUDIO_DAC_PIN 26
#endif

// Shared 12288-byte pool: used as banner graphic buffer in menus,
// and as 2x4096 PCM sound buffers during gameplay (mutually exclusive).
extern byte *cyd_banner_buffer;

namespace {
constexpr int kPcmMaxSegments = 2;
constexpr int kPcSoundBufferSize = 2048;
constexpr int kMaxChannels = 2;
bool toneReady = false;
uint32_t toneStopMs = 0;
bool currentPcSound = false;
byte pcSoundBuffer[kPcSoundBufferSize];

static uint8_t *s_soundBuffer[kMaxChannels] = { nullptr };

struct ChannelState {
  word currentSound = 0;
  uint32_t toneStopMs = 0;
  
  const uint8_t *pinnedPcm[kPcmMaxSegments] = {};
  uint32_t pinnedPcmLen[kPcmMaxSegments] = {};
  int pinnedPage[kPcmMaxSegments] = {-1, -1};
  uint8_t pinnedSegmentCount = 0;
  uint32_t pinnedTotalLen = 0;
  int pinnedDigi = -1;
  
  uint8_t pendingVolume = 192;
  bool pendingPositionedVolume = false;
};

ChannelState s_chans[kMaxChannels];

extern "C" void cyd_hw_tone_init(void);
extern "C" void cyd_hw_tone(uint16_t frequency);
extern "C" uint32_t cyd_hw_millis(void);
extern "C" void cyd_hw_pcm_play_channel(uint8_t channel, const uint8_t *data, uint32_t length);
extern "C" void cyd_hw_pcm_play_segments_channel(uint8_t channel, const uint8_t **segments, const uint32_t *lengths, uint8_t count);
extern "C" bool cyd_hw_pcm_active_channel(uint8_t channel);
extern "C" void cyd_hw_pcm_stop_channel(uint8_t channel);
extern "C" void cyd_hw_pc_speaker_play(const uint8_t *data, uint32_t length);
extern "C" bool cyd_hw_pc_speaker_active(void);
extern "C" void cyd_hw_pc_speaker_stop(void);
extern "C" void cyd_hw_audio_volume_channel(uint8_t channel, uint8_t volume);

uint8_t volumeFromPosition(int left, int right) {
    if(left < 0) left = 0;
    if(right < 0) right = 0;
    if(left > 15) left = 15;
    if(right > 15) right = 15;

    // Wolf's positional values are attenuation-like: 0 is close/loud, 15 is far.
    // The CYD build is mono, so convert the stereo pan pair into one distance volume.
    const int attenuation = (left + right) / 2;
    
    // Cost-effective linear distance attenuation fading all the way down to 0 at distance 15
    // Integer calculation: (15 - attenuation) * 192 / 15
    int volume = ((15 - attenuation) * 192) / 15;
    if(volume < 0) volume = 0;
    if(volume > 192) volume = 192;
    return (uint8_t)volume;
}

uint8_t consumePendingVolume(uint8_t channel) {
    ChannelState &ch = s_chans[channel];
    uint8_t volume = ch.pendingPositionedVolume ? ch.pendingVolume : 192;
    ch.pendingVolume = 192;
    ch.pendingPositionedVolume = false;
    return volume;
}

uint8_t volumeForSound(soundnames sound, uint8_t volume) {
    switch(sound) {
        case GETKEYSND:
        case GETAMMOSND:
        case GETMACHINESND:
        case GETGATLINGSND:
        case HEALTH1SND:
        case HEALTH2SND:
        case BONUS1SND:
        case BONUS2SND:
        case BONUS3SND:
        case BONUS4SND:
        case BONUS1UPSND:
            return (uint8_t)(volume / 2);
        default:
            return volume;
    }
}

void unpinCurrentPcm(uint8_t channel) {
    ChannelState &ch = s_chans[channel];
    for(uint8_t i = 0; i < ch.pinnedSegmentCount; ++i)
    {
        ch.pinnedPage[i] = -1;
        ch.pinnedPcm[i] = nullptr;
        ch.pinnedPcmLen[i] = 0;
    }
    ch.pinnedSegmentCount = 0;
    ch.pinnedTotalLen = 0;
    ch.pinnedDigi = -1;
}

void ensureToneReady() {
#if CYD_WOLF_BASIC_SOUND
    if(toneReady) return;
    // Borrow PCM channel buffers from cyd_banner_buffer (12288 bytes).
    // Banner graphics and PCM SFX are never needed simultaneously:
    // - banners only shown on title/menu screens (no SFX fire then)
    // - SFX fire during gameplay (no banners loaded then)
    // So the first 8192 bytes (2 x 4096) of the shared buffer serve both.
    if (!cyd_banner_buffer) {
        cyd_banner_buffer = (byte *) malloc(12288);
    }
    if (!cyd_banner_buffer) {
        printf("Wolf3D: banner/PCM buffer OOM — skipping SFX\n");
        return;
    }
    for (int i = 0; i < kMaxChannels; i++) {
        if (!s_soundBuffer[i]) {
            s_soundBuffer[i] = (uint8_t *)(cyd_banner_buffer + i * 4096);
        }
    }
    cyd_hw_tone_init();
    toneReady = true;
#endif
}

void stopChannel(uint8_t channel) {
#if CYD_WOLF_BASIC_SOUND
    cyd_hw_pcm_stop_channel(channel);
    unpinCurrentPcm(channel);
    s_chans[channel].currentSound = 0;
    s_chans[channel].toneStopMs = 0;
    if (channel < 8) {
        channelSoundPos[channel].valid = 0;
    }
#endif
}

void stopTone() {
#if CYD_WOLF_BASIC_SOUND
    if(!toneReady) return;
    cyd_hw_tone(0);
    cyd_hw_pc_speaker_stop();
    currentPcSound = false;
    toneStopMs = 0;
    
    // Stop all PCM channels and unpin their buffers
    for (int c = 0; c < kMaxChannels; ++c) {
        stopChannel(c);
    }
#endif
}

void playTone(word sound, uint16_t frequency, uint16_t durationMs) {
#if CYD_WOLF_BASIC_SOUND
    ensureToneReady();
    // PC speaker sounds take over channel 0
    stopChannel(0);
    cyd_hw_pc_speaker_stop();
    currentPcSound = false;
    
    cyd_hw_audio_volume_channel(0, volumeForSound((soundnames)sound, consumePendingVolume(0)));
    cyd_hw_tone(frequency);
    toneStopMs = cyd_hw_millis() + durationMs;
    s_chans[0].currentSound = sound;
    channelSoundPos[0].valid = 0;
#else
    (void)sound;
    (void)frequency;
    (void)durationMs;
#endif
}

uint32_t readLe32(const byte *ptr) {
    return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) |
           ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

bool playPcSpeakerSound(soundnames sound) {
#if CYD_WOLF_BASIC_SOUND
    if(sound < 0 || sound >= LASTSOUND)
        return false;

    const int chunk = STARTPCSOUNDS + (int)sound;
    stopChannel(0);
    cyd_hw_pc_speaker_stop();
    currentPcSound = false;

    int32_t chunkSize = CA_ReadAudioChunk(chunk, pcSoundBuffer, kPcSoundBufferSize);
    if(chunkSize <= ORIG_SOUNDCOMMON_SIZE)
        return false;

    uint32_t length = readLe32(pcSoundBuffer);
    const uint32_t maxLength = (uint32_t)chunkSize - ORIG_SOUNDCOMMON_SIZE;
    if(length > maxLength)
        length = maxLength;
    if(!length)
        return false;

    ensureToneReady();
    cyd_hw_audio_volume_channel(0, volumeForSound(sound, consumePendingVolume(0)));
    cyd_hw_pc_speaker_play(pcSoundBuffer + ORIG_SOUNDCOMMON_SIZE, length);
    s_chans[0].currentSound = sound;
    currentPcSound = true;
    toneStopMs = 0;
    channelSoundPos[0].valid = 0;
    return true;
#else
    (void)sound;
    return false;
#endif
}

uint8_t soundPriority(soundnames sound) {
    switch(sound) {
        case PLAYERDEATHSND:
        case LEVELDONESND:
        case BOSSACTIVESND:
        case SCHABBSHASND:
        case HITLERHASND:
        case GETKEYSND:
        case GETAMMOSND:
        case GETMACHINESND:
        case GETGATLINGSND:
        case BONUS1SND:
        case BONUS2SND:
        case BONUS3SND:
        case BONUS4SND:
        case BONUS1UPSND:
        case HEALTH1SND:
        case HEALTH2SND:
            return 5;
        case ATKPISTOLSND:
        case ATKMACHINEGUNSND:
        case ATKGATLINGSND:
        case SHOOTSND:
        case HITENEMYSND:
        case TAKEDAMAGESND:
        case BOSSFIRESND:
        case SSFIRESND:
        case NAZIFIRESND:
        case DOGATTACKSND:
        case DEATHSCREAM1SND:
        case DEATHSCREAM2SND:
        case DEATHSCREAM3SND:
        case DEATHSCREAM4SND:
        case DEATHSCREAM5SND:
        case DEATHSCREAM6SND:
        case DEATHSCREAM7SND:
        case DEATHSCREAM8SND:
        case DEATHSCREAM9SND:
        case DOGDEATHSND:
            return 4;
        case OPENDOORSND:
        case CLOSEDOORSND:
        case HALTSND:
        case SCHUTZADSND:
        case GUTENTAGSND:
        case MUTTISND:
        case LEBENSND:
        case AHHHGSND:
        case DIESND:
        case PUSHWALLSND:
        case DOGBARKSND:
            return 3;
        default:
            return 1;
    }
}

int fallbackDigiForSound(soundnames sound) {
    switch(sound) {
        case CLOSEDOORSND:     return 2;
        case OPENDOORSND:      return 3;
        case ATKMACHINEGUNSND: return 4;
        case ATKPISTOLSND:
        case SHOOTSND:         return 5;
        case ATKGATLINGSND:    return 6;
        case HALTSND:          return 0;
        case DOGBARKSND:       return 1;
        case TAKEDAMAGESND:
        case NAZIHITPLAYERSND: return 14;
        default:               return -1;
    }
}

bool pinDigiForSound(uint8_t channel, soundnames sound) {
#if CYD_WOLF_BASIC_SOUND
    int digi = DigiMap[sound];
    if(digi < 0)
        digi = fallbackDigiForSound(sound);
    if(digi < 0)
        return false;

    ChannelState &ch = s_chans[channel];
    if(ch.pinnedDigi == digi && ch.pinnedSegmentCount && ch.pinnedTotalLen)
        return true;

    cyd_hw_pcm_stop_channel(channel);
    unpinCurrentPcm(channel);

    // s_soundBuffer is statically allocated now

    word *soundInfoPage = (word *)(void *)PM_GetPage(ChunksInFile - 1);
    int numDigi = (int)PM_GetPageSize(ChunksInFile - 1) / 4;
    if(!soundInfoPage || digi < 0 || digi >= numDigi)
    {
        furi_log_print_format(2, "Wolf3D", "PCM unavailable snd=%i soundInfo=%p digi=%i num=%i",
                              (int)sound, soundInfoPage, digi, numDigi);
        return false;
    }

    int startPage = soundInfoPage[digi * 2];
    uint32_t declaredLen = soundInfoPage[digi * 2 + 1];
    if(!declaredLen)
        return false;

    if(declaredLen > 4096)
    {
        declaredLen = 4096;
    }

    uint32_t firstPage = (uint32_t)(PMSoundStart + startPage);
    uint32_t remaining = declaredLen;
    uint32_t absPage = firstPage;
    if (!s_soundBuffer[channel]) {
        // Buffer not available (e.g. OOM during ensureToneReady) — skip silently
        return false;
    }
    uint8_t *destPtr = s_soundBuffer[channel];

    while(remaining > 0)
    {
        uint32_t pageLen = PM_GetPageSize((int)absPage);
        if(pageLen == 0) break;
        uint32_t segLen = remaining < pageLen ? remaining : pageLen;
        const uint8_t *pcm = (const uint8_t *)PM_GetPage((int)absPage);
        if(!pcm || segLen == 0)
        {
            furi_log_print_format(2, "Wolf3D", "PCM page failed snd=%i digi=%i page=%u pcm=%p len=%u",
                                  (int)sound, digi, (unsigned)absPage, pcm, (unsigned)segLen);
            return false;
        }
        memcpy(destPtr, pcm, segLen);
        destPtr += segLen;
        remaining -= segLen;
        absPage++;
    }

    ch.pinnedPcm[0] = s_soundBuffer[channel];
    ch.pinnedPcmLen[0] = declaredLen - remaining;
    ch.pinnedPage[0] = (int)firstPage;
    ch.pinnedSegmentCount = 1;
    ch.pinnedTotalLen = declaredLen - remaining;
    ch.pinnedDigi = digi;

#if CYD_WOLF_SOUND_TRACE
    furi_log_print_format(2, "Wolf3D", "PCM snd %i digi %i page %i abs %u len %u play %u seg %u",
                          (int)sound, digi, startPage, (unsigned)firstPage,
                          (unsigned)declaredLen, (unsigned)ch.pinnedTotalLen,
                          (unsigned)ch.pinnedSegmentCount);
#endif
    return ch.pinnedSegmentCount > 0;
#else
    (void)channel;
    (void)sound;
    return false;
#endif
}

bool playPinnedPcm(uint8_t channel, soundnames sound) {
#if CYD_WOLF_BASIC_SOUND
    ensureToneReady();
    if(!pinDigiForSound(channel, sound))
        return false;

    cyd_hw_tone(0);
    cyd_hw_pc_speaker_stop();
    currentPcSound = false;
    
    ChannelState &ch = s_chans[channel];
    cyd_hw_audio_volume_channel(channel, volumeForSound(sound, consumePendingVolume(channel)));
    cyd_hw_pcm_play_segments_channel(channel, ch.pinnedPcm, ch.pinnedPcmLen, ch.pinnedSegmentCount);
    ch.toneStopMs = cyd_hw_millis() + (ch.pinnedTotalLen / 7) + 20;
    ch.currentSound = sound;
    return true;
#endif
}
}

globalsoundpos channelSoundPos[8];
boolean AdLibPresent = false;
boolean SoundBlasterPresent = false;
boolean SoundSourcePresent = false;
SDMode SoundMode = sdm_AdLib;
SDSMode DigiMode = sds_SoundBlaster;
SMMode MusicMode = smm_AdLib;
int DigiMap[LASTSOUND];
int DigiChannel[LASTSOUND];

void SD_Startup(void) {
    AdLibPresent = true;
    SoundBlasterPresent = true;
    for (int i=0;i<LASTSOUND;++i) { DigiMap[i]=-1; DigiChannel[i]=-1; }
    
    // Explicitly initialize channel states to avoid garbage values
    for (int c = 0; c < kMaxChannels; ++c) {
        s_chans[c].currentSound = 0;
        s_chans[c].toneStopMs = 0;
        s_chans[c].pinnedSegmentCount = 0;
        s_chans[c].pinnedTotalLen = 0;
        s_chans[c].pinnedDigi = -1;
        s_chans[c].pendingVolume = 192;
        s_chans[c].pendingPositionedVolume = false;
        for (int i = 0; i < kPcmMaxSegments; ++i) {
            s_chans[c].pinnedPcm[i] = nullptr;
            s_chans[c].pinnedPcmLen[i] = 0;
            s_chans[c].pinnedPage[i] = -1;
        }
    }
}
void SD_Shutdown(void) {}
void SD_StopSound(void) { stopTone(); }
void SD_WaitSoundDone(void) { stopTone(); }

int SD_GetChannelForDigi(int which) {
    if (which >= 0 && which < LASTSOUND) {
        if (which == ATKPISTOLSND || which == ATKMACHINEGUNSND || which == ATKGATLINGSND || which == SHOOTSND || which == GETKEYSND || which == GETAMMOSND || which == GETMACHINESND || which == GETGATLINGSND || which == BONUS1SND || which == BONUS2SND || which == BONUS3SND || which == BONUS4SND || which == BONUS1UPSND || which == HEALTH1SND || which == HEALTH2SND || which == PLAYERDEATHSND) {
            return 0;
        }
    }
    return 1;
}

void SD_PositionSound(int left, int right) {
    s_chans[0].pendingVolume = volumeFromPosition(left, right);
    s_chans[0].pendingPositionedVolume = true;
    s_chans[1].pendingVolume = volumeFromPosition(left, right);
    s_chans[1].pendingPositionedVolume = true;
}

void SD_SetPosition(int channel, int left, int right) {
    if (channel < 0 || channel >= kMaxChannels) return;
    s_chans[channel].pendingVolume = volumeFromPosition(left, right);
    s_chans[channel].pendingPositionedVolume = true;
    if(s_chans[channel].currentSound)
        cyd_hw_audio_volume_channel(channel, s_chans[channel].pendingVolume);
}

void SD_SetPosition(int left, int right) {
    SD_SetPosition(0, left, right);
}

void SD_PrepareSound(int) {}
int SD_PlayDigitized(word which, int, int) { return (int)SD_PlaySound((soundnames)which); }
void SD_StopDigitized(void) { stopTone(); }
boolean SD_PlaySound(soundnames sound) {
#if CYD_WOLF_BASIC_SOUND
    const uint32_t now = cyd_hw_millis();
    bool cydUsedPcm = false;
    
    if(toneStopMs && now >= toneStopMs)
        stopTone();

    for (int c = 0; c < kMaxChannels; ++c) {
        if (s_chans[c].toneStopMs && now >= s_chans[c].toneStopMs) {
            stopChannel(c);
        }
    }

    int ch = SD_GetChannelForDigi(sound);

    if(s_chans[ch].currentSound && soundPriority(sound) < soundPriority((soundnames)s_chans[ch].currentSound))
    {
        cyd_trace_sound((int)sound, 0);
        return false;
    }

    switch(sound) {
        case OPENDOORSND:
        case CLOSEDOORSND:
        case ATKPISTOLSND:
        case SHOOTSND:
        case ATKMACHINEGUNSND:
        case ATKGATLINGSND:
        case TAKEDAMAGESND:
        case NAZIHITPLAYERSND:
        case HALTSND:
        case DOGBARKSND:
        case SCHUTZADSND:
        case GUTENTAGSND:
        case MUTTISND:
        case AHHHGSND:
        case DIESND:
        case LEBENSND:
        case NAZIFIRESND:
        case BOSSFIRESND:
        case SSFIRESND:
        case DEATHSCREAM1SND:
        case DEATHSCREAM2SND:
        case DEATHSCREAM3SND:
        case DEATHSCREAM4SND:
        case DEATHSCREAM5SND:
        case DEATHSCREAM6SND:
        case DEATHSCREAM7SND:
        case DEATHSCREAM8SND:
        case DEATHSCREAM9SND:
        case DOGDEATHSND:
        case DOGATTACKSND:
        case PUSHWALLSND:
        case LEVELDONESND:
        case SLURPIESND:
        case YEAHSND:
            if(playPinnedPcm(ch, sound)) { cydUsedPcm = true; break; }
            if(sound == OPENDOORSND) playTone(sound, 220, 90);
            else if(sound == CLOSEDOORSND) playTone(sound, 165, 90);
            else if(sound == ATKMACHINEGUNSND || sound == ATKGATLINGSND) playTone(sound, 1350, 35);
            else if(sound == TAKEDAMAGESND || sound == NAZIHITPLAYERSND) playTone(sound, 130, 140);
            else if(sound == HALTSND || sound == DOGBARKSND) playTone(sound, 360, 100);
            else playTone(sound, 1150, 55);
            break;
        case HITENEMYSND:
            if(!playPcSpeakerSound(sound)) playTone(sound, 520, 70);
            break;
        case GETKEYSND:
            if(!playPcSpeakerSound(sound)) playTone(sound, 880, 110);
            break;
        case GETAMMOSND:
        case GETMACHINESND:
        case GETGATLINGSND:
            if(!playPcSpeakerSound(sound)) playTone(sound, 720, 80);
            break;
        case HEALTH1SND:
        case HEALTH2SND:
            if(!playPcSpeakerSound(sound)) playTone(sound, 660, 85);
            break;
        case BONUS1SND:
        case BONUS2SND:
        case BONUS3SND:
        case BONUS4SND:
        case BONUS1UPSND:
            if(!playPcSpeakerSound(sound)) playTone(sound, 980, 75);
            break;
        case HITWALLSND:
        case NOWAYSND:
        case NOITEMSND:
            if(!playPcSpeakerSound(sound)) playTone(sound, 100, 55);
            break;
        case PLAYERDEATHSND:
            if(!playPcSpeakerSound(sound)) playTone(sound, 90, 300);
            break;
        default:
            cyd_trace_sound((int)sound, 0);
            return false;
    }
    cyd_trace_sound((int)sound, cydUsedPcm ? 1 : 0);
    return (boolean)(cydUsedPcm ? (ch + 1) : 1);
#else
    (void)sound;
    return false;
#endif
}
word SD_SoundPlaying(void) {
#if CYD_WOLF_BASIC_SOUND
    word activeSound = 0;
    
    if(currentPcSound)
    {
        if(cyd_hw_pc_speaker_active())
            activeSound = s_chans[0].currentSound;
        else {
            currentPcSound = false;
            s_chans[0].currentSound = 0;
            toneStopMs = 0;
            channelSoundPos[0].valid = 0;
        }
    }
    
    for (int c = 0; c < kMaxChannels; ++c) {
        if (s_chans[c].currentSound) {
            if (cyd_hw_pcm_active_channel(c)) {
                activeSound = s_chans[c].currentSound;
            } else {
                unpinCurrentPcm(c);
                s_chans[c].currentSound = 0;
                s_chans[c].toneStopMs = 0;
                if (c < 8) {
                    channelSoundPos[c].valid = 0;
                }
            }
        }
        if (s_chans[c].toneStopMs && cyd_hw_millis() >= s_chans[c].toneStopMs) {
            stopChannel(c);
        }
    }
    
    if (toneStopMs && cyd_hw_millis() >= toneStopMs) {
        stopTone();
    }
    
    return activeSound;
#else
    return 0;
#endif
}
extern "C" {
extern volatile bool cyd_music_active;
extern volatile uint8_t cyd_music_volume;

void cyd_hw_music_clear_buffer(void);
void cyd_hw_music_volume(uint8_t volume);
void cyd_hw_music_reset_opl(void);
void cyd_hw_music_start(const char *filename, int32_t start, int32_t len);
void cyd_hw_music_stop(void);

int32_t CA_GetAudioChunkStartAndLen(int chunk, int32_t *start);
void CA_GetAudioFileName(char *dest);
}

void SD_StartMusic(int chunk) {
#if CYD_WOLF_BASIC_SOUND
    SD_MusicOff();
    cyd_hw_music_reset_opl();
    cyd_hw_music_clear_buffer();

    int32_t start = -1;
    int32_t len = CA_GetAudioChunkStartAndLen(chunk, &start);
    if (len <= 0 || start < 0) {
        return; // Empty song
    }

    char fname[32];
    CA_GetAudioFileName(fname);

    cyd_hw_music_start(fname, start, len);
#endif
}

void SD_ContinueMusic(int chunk, int startoffs) {
    SD_StartMusic(chunk);
}

void SD_MusicOn(void) {
#if CYD_WOLF_BASIC_SOUND
    cyd_music_active = true;
#endif
}

int SD_MusicOff(void) {
#if CYD_WOLF_BASIC_SOUND
    cyd_hw_music_stop();
    cyd_hw_music_clear_buffer();
#endif
    return 0;
}

void SD_FadeOutMusic(void) {
    SD_MusicOff();
}

boolean SD_MusicPlaying(void) {
#if CYD_WOLF_BASIC_SOUND
    return cyd_music_active;
#else
    return false;
#endif
}

boolean SD_SetSoundMode(SDMode mode) { SoundMode=mode; return true; }
boolean SD_SetMusicMode(SMMode mode) { MusicMode=mode; return true; }
void SD_SetDigiDevice(SDSMode mode) { DigiMode=mode; }

extern "C" void cyd_sound_poll(void) {
    (void)SD_SoundPlaying();
}

extern "C" void cyd_hw_sound_preallocate(void) {
    ensureToneReady();
}
