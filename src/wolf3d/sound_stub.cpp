#include <wl_def.h>
#include "board_config.h"

extern "C" void furi_log_print_format(int, const char *, const char *, ...);

#ifndef CYD_WOLF_BASIC_SOUND
#define CYD_WOLF_BASIC_SOUND 0
#endif

#ifndef AUDIO_DAC_PIN
#define AUDIO_DAC_PIN 26
#endif

namespace {
constexpr int kToneChannel = 7;
constexpr uint32_t kPistolPcmMax = 384;
bool toneReady = false;
uint32_t toneStopMs = 0;
word currentSound = 0;
uint8_t pistolPcm[kPistolPcmMax];
uint32_t pistolPcmLen = 0;
bool pistolPcmLoaded = false;

extern "C" void cyd_hw_tone_init(void);
extern "C" void cyd_hw_tone(uint16_t frequency);
extern "C" uint32_t cyd_hw_millis(void);
extern "C" void cyd_hw_pcm_play(const uint8_t *data, uint32_t length);
extern "C" bool cyd_hw_pcm_active(void);
extern "C" void cyd_hw_pcm_stop(void);

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
    toneStopMs = 0;
    currentSound = 0;
#endif
}

void playTone(word sound, uint16_t frequency, uint16_t durationMs) {
#if CYD_WOLF_BASIC_SOUND
    ensureToneReady();
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
            return 5;
        case ATKPISTOLSND:
        case ATKMACHINEGUNSND:
        case ATKGATLINGSND:
        case SHOOTSND:
        case HITENEMYSND:
        case TAKEDAMAGESND:
            return 4;
        case OPENDOORSND:
        case CLOSEDOORSND:
        case GETKEYSND:
        case GETAMMOSND:
        case GETMACHINESND:
        case GETGATLINGSND:
            return 3;
        case BONUS1SND:
        case BONUS2SND:
        case BONUS3SND:
        case BONUS4SND:
        case HEALTH1SND:
        case HEALTH2SND:
            return 2;
        default:
            return 1;
    }
}

uint8_t currentPriority() {
    return currentSound ? soundPriority((soundnames)currentSound) : 0;
}

bool loadPistolPcm() {
#if CYD_WOLF_BASIC_SOUND
    if(pistolPcmLoaded)
        return pistolPcmLen > 0;

    int digi = DigiMap[ATKPISTOLSND];
    if(digi < 0)
    {
        furi_log_print_format(2, "Wolf3D", "Pistol PCM DigiMap missing, using WL6 fallback 5");
        digi = 5; // WL6 InitDigiMap maps pistol to digitized sound 5.
    }

    word *soundInfoPage = (word *)(void *)PM_GetPage(ChunksInFile - 1);
    int numDigi = (int)PM_GetPageSize(ChunksInFile - 1) / 4;
    if(!soundInfoPage || digi < 0 || digi >= numDigi)
    {
        furi_log_print_format(2, "Wolf3D", "Pistol PCM unavailable soundInfo=%p digi=%i num=%i",
                              soundInfoPage, digi, numDigi);
        pistolPcmLoaded = true;
        pistolPcmLen = 0;
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
    uint32_t firstPage = (uint32_t)(PMSoundStart + startPage);
    uint32_t firstPageLen = PM_GetPageSize((int)firstPage);
    uint32_t copyLen = declaredLen;
    if(copyLen > firstPageLen) copyLen = firstPageLen;
    if(copyLen > kPistolPcmMax) copyLen = kPistolPcmMax;

    const uint8_t *pcm = (const uint8_t *)PM_GetSound(startPage);
    if(!pcm || copyLen == 0)
    {
        furi_log_print_format(2, "Wolf3D", "Pistol PCM page failed pcm=%p copy=%u",
                              pcm, (unsigned)copyLen);
        pistolPcmLoaded = true;
        pistolPcmLen = 0;
        return false;
    }

    memcpy(pistolPcm, pcm, copyLen);
    pistolPcmLen = copyLen;
    pistolPcmLoaded = true;
    furi_log_print_format(2, "Wolf3D", "Pistol PCM digi %i page %i len %u copy %u lastPage %i",
                          digi, startPage, (unsigned)declaredLen, (unsigned)copyLen, lastPage);
    return true;
#else
    return false;
#endif
}

bool playPistolPcm(word sound) {
#if CYD_WOLF_BASIC_SOUND
    ensureToneReady();
    if(!loadPistolPcm())
    {
        furi_log_print_format(2, "Wolf3D", "Pistol PCM fallback tone");
        return false;
    }
    cyd_hw_tone(0);
    cyd_hw_pcm_play(pistolPcm, pistolPcmLen);
    toneStopMs = cyd_hw_millis() + (pistolPcmLen / 7) + 20;
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
void SD_PositionSound(int, int) {}
void SD_SetPosition(int, int, int) {}
void SD_PrepareSound(int) {}
int SD_PlayDigitized(word which, int, int) { return SD_PlaySound((soundnames)which) ? 1 : 0; }
void SD_StopDigitized(void) { stopTone(); }
boolean SD_PlaySound(soundnames sound) {
#if CYD_WOLF_BASIC_SOUND
    const uint32_t now = cyd_hw_millis();
    if(toneStopMs && now >= toneStopMs)
        stopTone();

    if(currentSound && soundPriority(sound) < currentPriority())
        return true;

    switch(sound) {
        case ATKPISTOLSND:
        case SHOOTSND:
            if(playPistolPcm(sound)) break;
            playTone(sound, 1150, 55);
            break;
        case OPENDOORSND:      playTone(sound, 220, 90); break;
        case CLOSEDOORSND:     playTone(sound, 165, 90); break;
        case ATKMACHINEGUNSND:
        case ATKGATLINGSND:    playTone(sound, 1350, 35); break;
        case HITENEMYSND:      playTone(sound, 520, 70); break;
        case TAKEDAMAGESND:
        case NAZIHITPLAYERSND: playTone(sound, 130, 140); break;
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
        case LEVELDONESND:     playTone(sound, 1040, 220); break;
        case PLAYERDEATHSND:   playTone(sound, 90, 300); break;
        case HALTSND:
        case SCHUTZADSND:
        case GUTENTAGSND:
        case DOGBARKSND:       playTone(sound, 360, 100); break;
        default:
            return false;
    }
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
