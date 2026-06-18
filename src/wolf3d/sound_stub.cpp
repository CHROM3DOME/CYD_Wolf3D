#include <wl_def.h>

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
void SD_StopSound(void) {}
void SD_WaitSoundDone(void) {}
int SD_GetChannelForDigi(int) { return 0; }
void SD_PositionSound(int, int) {}
void SD_SetPosition(int, int, int) {}
void SD_PrepareSound(int) {}
int SD_PlayDigitized(word, int, int) { return 0; }
void SD_StopDigitized(void) {}
boolean SD_PlaySound(soundnames) { return false; }
word SD_SoundPlaying(void) { return 0; }
void SD_StartMusic(int) {}
void SD_ContinueMusic(int, int) {}
void SD_MusicOn(void) {}
int SD_MusicOff(void) { return 0; }
void SD_FadeOutMusic(void) {}
boolean SD_MusicPlaying(void) { return false; }
boolean SD_SetSoundMode(SDMode mode) { SoundMode=mode; return true; }
boolean SD_SetMusicMode(SMMode mode) { MusicMode=mode; return true; }
void SD_SetDigiDevice(SDSMode mode) { DigiMode=mode; }

