#include <wl_def.h>
#include "board_config.h"

extern "C" void furi_log_print_format(int, const char *, const char *, ...);
extern "C" void cyd_trace_sound(int sound, int usedPcm);

#ifndef CYD_WOLF_BASIC_SOUND
#define CYD_WOLF_BASIC_SOUND 0
#endif

#ifndef AUDIO_DAC_PIN
#define AUDIO_DAC_PIN 26
#endif

namespace {
constexpr int kToneChannel = 7;
constexpr int kPcmMaxSegments = 2;
bool toneReady = false;
uint32_t toneStopMs = 0;
word currentSound = 0;
const uint8_t *pinnedPcm[kPcmMaxSegments] = {};
uint32_t pinnedPcmLen[kPcmMaxSegments] = {};
int pinnedPage[kPcmMaxSegments] = {-1, -1};
uint8_t pinnedSegmentCount = 0;
uint32_t pinnedTotalLen = 0;
int pinnedDigi = -1;
uint8_t pendingVolume = 192;
bool pendingPositionedVolume = false;

extern "C" void cyd_hw_tone_init(void);
extern "C" void cyd_hw_tone(uint16_t frequency);
extern "C" uint32_t cyd_hw_millis(void);
extern "C" void cyd_hw_pcm_play(const uint8_t *data, uint32_t length);
extern "C" void cyd_hw_pcm_play_segments(const uint8_t **segments, const uint32_t *lengths, uint8_t count);
extern "C" bool cyd_hw_pcm_active(void);
extern "C" void cyd_hw_pcm_stop(void);
extern "C" void cyd_hw_audio_volume(uint8_t volume);

uint8_t volumeFromPosition(int left, int right) {
    if(left < 0) left = 0;
    if(right < 0) right = 0;
    if(left > 15) left = 15;
    if(right > 15) right = 15;

    // Wolf's positional values are attenuation-like: 0 is close/loud, 15 is far.
    // The CYD build is mono, so convert the stereo pan pair into one distance volume.
    const int attenuation = (left + right) / 2;
    int volume = 56 + (15 - attenuation) * 9;
    if(volume < 48) volume = 48;
    if(volume > 192) volume = 192;
    return (uint8_t)volume;
}

uint8_t consumePendingVolume() {
    uint8_t volume = pendingPositionedVolume ? pendingVolume : 192;
    pendingVolume = 192;
    pendingPositionedVolume = false;
    return volume;
}

void unpinCurrentPcm() {
    for(uint8_t i = 0; i < pinnedSegmentCount; ++i)
    {
        if(pinnedPage[i] >= 0)
            PM_UnpinPage(pinnedPage[i]);
        pinnedPage[i] = -1;
        pinnedPcm[i] = nullptr;
        pinnedPcmLen[i] = 0;
    }
    pinnedSegmentCount = 0;
    pinnedTotalLen = 0;
    pinnedDigi = -1;
}

void ensureToneReady() {
#if CYD_WOLF_BASIC_SOUND
    if(toneReady) return;
    cyd_hw_tone_init();
    toneReady = true;
#endif
}

void stopTone() {
#if CYD_WOLF_BASIC_SOUND
    if(!toneReady) return;
    cyd_hw_tone(0);
    cyd_hw_pcm_stop();
    unpinCurrentPcm();
    toneStopMs = 0;
    currentSound = 0;
#endif
}

void playTone(word sound, uint16_t frequency, uint16_t durationMs) {
#if CYD_WOLF_BASIC_SOUND
    ensureToneReady();
    cyd_hw_audio_volume(consumePendingVolume());
    cyd_hw_tone(frequency);
    toneStopMs = cyd_hw_millis() + durationMs;
    currentSound = sound;
#else
    (void)sound;
    (void)frequency;
    (void)durationMs;
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

uint8_t currentPriority() {
    return currentSound ? soundPriority((soundnames)currentSound) : 0;
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

bool pinDigiForSound(soundnames sound) {
#if CYD_WOLF_BASIC_SOUND
    int digi = DigiMap[sound];
    if(digi < 0)
        digi = fallbackDigiForSound(sound);
    if(digi < 0)
        return false;

    if(pinnedDigi == digi && pinnedSegmentCount && pinnedTotalLen)
        return true;

    unpinCurrentPcm();

    word *soundInfoPage = (word *)(void *)PM_GetPage(ChunksInFile - 1);
    int numDigi = (int)PM_GetPageSize(ChunksInFile - 1) / 4;
    if(!soundInfoPage || digi < 0 || digi >= numDigi)
    {
        furi_log_print_format(2, "Wolf3D", "PCM unavailable snd=%i soundInfo=%p digi=%i num=%i",
                              (int)sound, soundInfoPage, digi, numDigi);
        return false;
    }

    int startPage = soundInfoPage[digi * 2];
    int lastPage;
    if(digi < numDigi - 1)
    {
        lastPage = soundInfoPage[digi * 2 + 2];
        if(lastPage == 0 || lastPage + PMSoundStart > ChunksInFile - 1)
            lastPage = ChunksInFile - 1;
        else
            lastPage += PMSoundStart;
    }
    else
        lastPage = ChunksInFile - 1;

    uint32_t declaredLen = soundInfoPage[digi * 2 + 1];
    if(!declaredLen)
        return false;
    uint32_t firstPage = (uint32_t)(PMSoundStart + startPage);
    uint32_t remaining = declaredLen;
    uint32_t absPage = firstPage;
    while(remaining > 0 && pinnedSegmentCount < kPcmMaxSegments)
    {
        uint32_t pageLen = PM_GetPageSize((int)absPage);
        if(pageLen == 0) break;
        uint32_t segLen = remaining < pageLen ? remaining : pageLen;
        const uint8_t *pcm = (const uint8_t *)PM_PinPage((int)absPage);
        if(!pcm || segLen == 0)
        {
            furi_log_print_format(2, "Wolf3D", "PCM page failed snd=%i digi=%i page=%u pcm=%p len=%u",
                                  (int)sound, digi, (unsigned)absPage, pcm, (unsigned)segLen);
            unpinCurrentPcm();
            return false;
        }
        pinnedPcm[pinnedSegmentCount] = pcm;
        pinnedPcmLen[pinnedSegmentCount] = segLen;
        pinnedPage[pinnedSegmentCount] = (int)absPage;
        pinnedSegmentCount++;
        pinnedTotalLen += segLen;
        remaining -= segLen;
        absPage++;
    }

    pinnedDigi = digi;
    furi_log_print_format(2, "Wolf3D", "PCM snd %i digi %i page %i abs %u len %u play %u seg %u lastPage %i%s",
                          (int)sound, digi, startPage, (unsigned)firstPage,
                          (unsigned)declaredLen, (unsigned)pinnedTotalLen,
                          (unsigned)pinnedSegmentCount, lastPage,
                          remaining ? " TRUNC" : "");
    return pinnedSegmentCount > 0;
#else
    (void)sound;
    return false;
#endif
}

bool playPinnedPcm(soundnames sound) {
#if CYD_WOLF_BASIC_SOUND
    ensureToneReady();
    if(!pinDigiForSound(sound))
        return false;

    cyd_hw_tone(0);
    cyd_hw_audio_volume(consumePendingVolume());
    cyd_hw_pcm_play_segments(pinnedPcm, pinnedPcmLen, pinnedSegmentCount);
    toneStopMs = cyd_hw_millis() + (pinnedTotalLen / 7) + 20;
    currentSound = sound;
    return true;
#else
    (void)sound;
    return false;
#endif
}
}

globalsoundpos channelSoundPos[8];
boolean AdLibPresent = false;
boolean SoundBlasterPresent = false;
boolean SoundSourcePresent = false;
SDMode SoundMode = sdm_Off;
SDSMode DigiMode = sds_Off;
SMMode MusicMode = smm_Off;
int DigiMap[LASTSOUND];
int DigiChannel[LASTSOUND];

void SD_Startup(void) { for (int i=0;i<LASTSOUND;++i) { DigiMap[i]=-1; DigiChannel[i]=-1; } }
void SD_Shutdown(void) {}
void SD_StopSound(void) { stopTone(); }
void SD_WaitSoundDone(void) { stopTone(); }
int SD_GetChannelForDigi(int) { return 0; }
void SD_PositionSound(int left, int right) {
    pendingVolume = volumeFromPosition(left, right);
    pendingPositionedVolume = true;
}
void SD_SetPosition(int, int left, int right) {
    pendingVolume = volumeFromPosition(left, right);
    pendingPositionedVolume = true;
    if(currentSound)
        cyd_hw_audio_volume(pendingVolume);
}
void SD_SetPosition(int left, int right) {
    SD_SetPosition(0, left, right);
}
void SD_PrepareSound(int) {}
int SD_PlayDigitized(word which, int, int) { return SD_PlaySound((soundnames)which) ? 1 : 0; }
void SD_StopDigitized(void) { stopTone(); }
boolean SD_PlaySound(soundnames sound) {
#if CYD_WOLF_BASIC_SOUND
    const uint32_t now = cyd_hw_millis();
    bool cydUsedPcm = false;
    if(toneStopMs && now >= toneStopMs)
        stopTone();

    if(currentSound && soundPriority(sound) < currentPriority())
    {
        cyd_trace_sound((int)sound, 0);
        return true;
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
            if(playPinnedPcm(sound)) { cydUsedPcm = true; break; }
            if(sound == OPENDOORSND) playTone(sound, 220, 90);
            else if(sound == CLOSEDOORSND) playTone(sound, 165, 90);
            else if(sound == ATKMACHINEGUNSND || sound == ATKGATLINGSND) playTone(sound, 1350, 35);
            else if(sound == TAKEDAMAGESND || sound == NAZIHITPLAYERSND) playTone(sound, 130, 140);
            else if(sound == HALTSND || sound == DOGBARKSND) playTone(sound, 360, 100);
            else playTone(sound, 1150, 55);
            break;
        case HITENEMYSND:      playTone(sound, 520, 70); break;
        case GETKEYSND:        playTone(sound, 880, 110); break;
        case GETAMMOSND:
        case GETMACHINESND:
        case GETGATLINGSND:    playTone(sound, 720, 80); break;
        case HEALTH1SND:
        case HEALTH2SND:       playTone(sound, 660, 85); break;
        case BONUS1SND:
        case BONUS2SND:
        case BONUS3SND:
        case BONUS4SND:
        case BONUS1UPSND:      playTone(sound, 980, 75); break;
        case HITWALLSND:
        case NOWAYSND:
        case NOITEMSND:        playTone(sound, 100, 55); break;
        case PLAYERDEATHSND:   playTone(sound, 90, 300); break;
        default:
            cyd_trace_sound((int)sound, 0);
            return false;
    }
    cyd_trace_sound((int)sound, cydUsedPcm ? 1 : 0);
    return true;
#else
    (void)sound;
    return false;
#endif
}
word SD_SoundPlaying(void) {
#if CYD_WOLF_BASIC_SOUND
    if(currentSound && cyd_hw_pcm_active())
        return currentSound;
    if(pinnedSegmentCount)
        unpinCurrentPcm();
    if(toneStopMs && cyd_hw_millis() >= toneStopMs)
        stopTone();
    return currentSound;
#else
    return 0;
#endif
}
void SD_StartMusic(int) {}
void SD_ContinueMusic(int, int) {}
void SD_MusicOn(void) {}
int SD_MusicOff(void) { return 0; }
void SD_FadeOutMusic(void) {}
boolean SD_MusicPlaying(void) { return false; }
boolean SD_SetSoundMode(SDMode mode) { SoundMode=mode; return true; }
boolean SD_SetMusicMode(SMMode mode) { MusicMode=mode; return true; }
void SD_SetDigiDevice(SDSMode mode) { DigiMode=mode; }

extern "C" void cyd_sound_poll(void) {
    (void)SD_SoundPlaying();
}
