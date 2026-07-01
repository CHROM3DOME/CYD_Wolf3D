#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "board_config.h"
#include "resource_trace.h"

#ifndef CYD_WOLF_RESOURCE_TRACE
#define CYD_WOLF_RESOURCE_TRACE 0
#endif

#ifndef CYD_WOLF_RESOURCE_TRACE_INTERVAL_MS
#define CYD_WOLF_RESOURCE_TRACE_INTERVAL_MS 3000
#endif

#if CYD_WOLF_RESOURCE_TRACE

namespace {
struct TopEntry {
  int id = -1;
  uint32_t count = 0;
  uint32_t bytes = 0;
  uint32_t extra = 0;
};

constexpr int kPmTopCount = 8;
constexpr int kGrTopCount = 8;
constexpr int kSpriteTopCount = 10;
constexpr int kSoundTopCount = 8;
constexpr int kStaticTopCount = 8;

TopEntry pmTop[kPmTopCount];
TopEntry grTop[kGrTopCount];
TopEntry spriteTop[kSpriteTopCount];
TopEntry soundTop[kSoundTopCount];
TopEntry staticTop[kStaticTopCount];

uint32_t lastLogMs = 0;
uint32_t frames = 0;

uint32_t pmHits = 0;
uint32_t pmMisses = 0;
uint32_t pmReadUs = 0;
uint32_t pmMissBytes = 0;

uint32_t grRequests = 0;
uint32_t grExpands = 0;
uint32_t grCompressedBytes = 0;
uint32_t grExpandedBytes = 0;

uint32_t spriteTotal = 0;
uint32_t spriteDecor = 0;
uint32_t spriteBonus = 0;
uint32_t spriteActor = 0;
uint32_t spriteWeapon = 0;
uint32_t spriteTallest = 0;

uint32_t soundRequests = 0;
uint32_t soundPcm = 0;

uint32_t staticSpawns = 0;
uint32_t staticDecor = 0;
uint32_t staticBlock = 0;
uint32_t staticBonus = 0;
uint32_t staticDroppedDecor = 0;
uint32_t staticPeakUsed = 0;
uint32_t staticCapacity = 0;

void resetTop(TopEntry *top, int count) {
  for(int i = 0; i < count; ++i) top[i] = TopEntry{};
}

void resetAll() {
  frames = 0;
  pmHits = pmMisses = pmReadUs = pmMissBytes = 0;
  grRequests = grExpands = grCompressedBytes = grExpandedBytes = 0;
  spriteTotal = spriteDecor = spriteBonus = spriteActor = spriteWeapon = spriteTallest = 0;
  soundRequests = soundPcm = 0;
  staticSpawns = staticDecor = staticBlock = staticBonus = staticDroppedDecor = 0;
  staticPeakUsed = staticCapacity = 0;
  resetTop(pmTop, kPmTopCount);
  resetTop(grTop, kGrTopCount);
  resetTop(spriteTop, kSpriteTopCount);
  resetTop(soundTop, kSoundTopCount);
  resetTop(staticTop, kStaticTopCount);
}

void bumpTop(TopEntry *top, int count, int id, uint32_t bytes = 0, uint32_t extra = 0) {
  if(id < 0) return;
  int weakest = 0;
  for(int i = 0; i < count; ++i) {
    if(top[i].id == id) {
      top[i].count++;
      top[i].bytes += bytes;
      if(extra > top[i].extra) top[i].extra = extra;
      return;
    }
    if(top[i].id < 0) {
      top[i].id = id;
      top[i].count = 1;
      top[i].bytes = bytes;
      top[i].extra = extra;
      return;
    }
    if(top[i].count < top[weakest].count) weakest = i;
  }

  if(top[weakest].count <= 1) {
    top[weakest].id = id;
    top[weakest].count = 1;
    top[weakest].bytes = bytes;
    top[weakest].extra = extra;
  }
}

TopEntry bestOf(const TopEntry *top, int count) {
  TopEntry best;
  for(int i = 0; i < count; ++i) {
    if(top[i].id >= 0 && top[i].count > best.count) best = top[i];
  }
  return best;
}

void printBest(const char *label, const TopEntry &entry) {
  if(entry.id < 0 || entry.count == 0) {
    Serial.printf(" %s=-", label);
    return;
  }
  Serial.printf(" %s=%d:%lu", label, entry.id, static_cast<unsigned long>(entry.count));
  if(entry.bytes) Serial.printf("/%lub", static_cast<unsigned long>(entry.bytes));
  if(entry.extra) Serial.printf("/%lu", static_cast<unsigned long>(entry.extra));
}

void maybeLog() {
  const uint32_t nowMs = millis();
  if(lastLogMs == 0) lastLogMs = nowMs;
  if(nowMs - lastLogMs < CYD_WOLF_RESOURCE_TRACE_INTERVAL_MS) return;

  const uint32_t pmTotal = pmHits + pmMisses;
  const uint32_t pmMissPct = pmTotal ? (pmMisses * 100UL) / pmTotal : 0;
  const uint32_t avgPmReadUs = pmMisses ? pmReadUs / pmMisses : 0;
  const uint32_t avgGrExpanded = grExpands ? grExpandedBytes / grExpands : 0;

  Serial.printf("TRACE frames=%lu heap=%lu max=%lu",
                static_cast<unsigned long>(frames),
                static_cast<unsigned long>(esp_get_free_heap_size()),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  Serial.printf(" pm=%lu/%lu miss=%lu%% avgRead=%lu.%03lums missBytes=%lu",
                static_cast<unsigned long>(pmHits),
                static_cast<unsigned long>(pmMisses),
                static_cast<unsigned long>(pmMissPct),
                static_cast<unsigned long>(avgPmReadUs / 1000),
                static_cast<unsigned long>(avgPmReadUs % 1000),
                static_cast<unsigned long>(pmMissBytes));
  printBest("pmTop", bestOf(pmTop, kPmTopCount));
  Serial.println();

  Serial.printf("TRACE gr req=%lu exp=%lu compBytes=%lu expBytes=%lu avgExp=%lu",
                static_cast<unsigned long>(grRequests),
                static_cast<unsigned long>(grExpands),
                static_cast<unsigned long>(grCompressedBytes),
                static_cast<unsigned long>(grExpandedBytes),
                static_cast<unsigned long>(avgGrExpanded));
  printBest("grTop", bestOf(grTop, kGrTopCount));
  Serial.println();

  Serial.printf("TRACE spr total=%lu decor=%lu bonus=%lu actor=%lu weapon=%lu tallest=%lu",
                static_cast<unsigned long>(spriteTotal),
                static_cast<unsigned long>(spriteDecor),
                static_cast<unsigned long>(spriteBonus),
                static_cast<unsigned long>(spriteActor),
                static_cast<unsigned long>(spriteWeapon),
                static_cast<unsigned long>(spriteTallest));
  printBest("sprTop", bestOf(spriteTop, kSpriteTopCount));
  Serial.println();

  Serial.printf("TRACE snd req=%lu pcm=%lu",
                static_cast<unsigned long>(soundRequests),
                static_cast<unsigned long>(soundPcm));
  printBest("sndTop", bestOf(soundTop, kSoundTopCount));
  Serial.println();

  Serial.printf("TRACE stat spawn=%lu peak=%lu/%lu decor=%lu block=%lu bonus=%lu dropDecor=%lu",
                static_cast<unsigned long>(staticSpawns),
                static_cast<unsigned long>(staticPeakUsed),
                static_cast<unsigned long>(staticCapacity),
                static_cast<unsigned long>(staticDecor),
                static_cast<unsigned long>(staticBlock),
                static_cast<unsigned long>(staticBonus),
                static_cast<unsigned long>(staticDroppedDecor));
  printBest("statTop", bestOf(staticTop, kStaticTopCount));
  Serial.println();

  lastLogMs = nowMs;
  resetAll();
}
}

extern "C" void cyd_trace_frame(void) {
  ++frames;
  maybeLog();
}

extern "C" void cyd_trace_pm_hit(int page) {
  ++pmHits;
  (void)page;
}

extern "C" void cyd_trace_pm_miss(int page, uint32_t bytes, uint32_t readUs) {
  ++pmMisses;
  pmReadUs += readUs;
  pmMissBytes += bytes;
  bumpTop(pmTop, kPmTopCount, page, bytes, readUs);
}

extern "C" void cyd_trace_gr_request(int chunk, int32_t compressedBytes) {
  ++grRequests;
  if(compressedBytes > 0) grCompressedBytes += static_cast<uint32_t>(compressedBytes);
  bumpTop(grTop, kGrTopCount, chunk, compressedBytes > 0 ? static_cast<uint32_t>(compressedBytes) : 0);
}

extern "C" void cyd_trace_gr_expand(int chunk, int32_t expandedBytes) {
  ++grExpands;
  if(expandedBytes > 0) grExpandedBytes += static_cast<uint32_t>(expandedBytes);
  bumpTop(grTop, kGrTopCount, chunk, expandedBytes > 0 ? static_cast<uint32_t>(expandedBytes) : 0);
}

extern "C" void cyd_trace_sprite(int shapenum, int category, unsigned height) {
  ++spriteTotal;
  if(height > spriteTallest) spriteTallest = height;
  switch(category) {
    case CYD_TRACE_SPRITE_DECOR: ++spriteDecor; break;
    case CYD_TRACE_SPRITE_BONUS: ++spriteBonus; break;
    case CYD_TRACE_SPRITE_ACTOR: ++spriteActor; break;
    case CYD_TRACE_SPRITE_WEAPON: ++spriteWeapon; break;
    default: break;
  }
  bumpTop(spriteTop, kSpriteTopCount, shapenum, 0, height);
}

extern "C" void cyd_trace_sound(int sound, int usedPcm) {
  ++soundRequests;
  if(usedPcm) ++soundPcm;
  bumpTop(soundTop, kSoundTopCount, sound, 0, usedPcm ? 1 : 0);
}

extern "C" void cyd_trace_static_spawn(int used, int capacity, int category, int type, int shapenum) {
  ++staticSpawns;
  if(used > 0 && staticPeakUsed < static_cast<uint32_t>(used)) staticPeakUsed = used;
  if(capacity > 0) staticCapacity = static_cast<uint32_t>(capacity);
  switch(category) {
    case CYD_TRACE_STATIC_DECOR: ++staticDecor; break;
    case CYD_TRACE_STATIC_BLOCK: ++staticBlock; break;
    case CYD_TRACE_STATIC_BONUS: ++staticBonus; break;
    default: break;
  }
  bumpTop(staticTop, kStaticTopCount, shapenum, 0, static_cast<uint32_t>(type));
}

extern "C" void cyd_trace_static_drop(int used, int capacity, int type, int shapenum) {
  ++staticDroppedDecor;
  if(used > 0 && staticPeakUsed < static_cast<uint32_t>(used)) staticPeakUsed = used;
  if(capacity > 0) staticCapacity = static_cast<uint32_t>(capacity);
  bumpTop(staticTop, kStaticTopCount, shapenum, 0, static_cast<uint32_t>(type));
}

#else

extern "C" void cyd_trace_frame(void) {}
extern "C" void cyd_trace_pm_hit(int) {}
extern "C" void cyd_trace_pm_miss(int, uint32_t, uint32_t) {}
extern "C" void cyd_trace_gr_request(int, int32_t) {}
extern "C" void cyd_trace_gr_expand(int, int32_t) {}
extern "C" void cyd_trace_sprite(int, int, unsigned) {}
extern "C" void cyd_trace_sound(int, int) {}
extern "C" void cyd_trace_static_spawn(int, int, int, int, int) {}
extern "C" void cyd_trace_static_drop(int, int, int, int) {}

#endif
