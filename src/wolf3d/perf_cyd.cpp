#include <Arduino.h>
#include "board_config.h"

#ifndef CYD_WOLF_ENABLE_PERF_LOGS
#define CYD_WOLF_ENABLE_PERF_LOGS 0
#endif

namespace {
uint32_t lastLogMs = 0;
uint32_t frameCount = 0;
uint64_t renderTotalUs = 0;
uint64_t presentTotalUs = 0;
uint64_t prepTotalUs = 0;
uint64_t clearTotalUs = 0;
uint64_t wallTotalUs = 0;
uint64_t spriteTotalUs = 0;
uint64_t weaponTotalUs = 0;
uint32_t spriteVisibleTotal = 0;
uint32_t spriteDrawnTotal = 0;
uint32_t spriteDecorTotal = 0;
uint32_t spriteBonusTotal = 0;
uint32_t spriteActorTotal = 0;
uint64_t spriteDecorUsTotal = 0;
uint64_t spriteBonusUsTotal = 0;
uint64_t spriteActorUsTotal = 0;
uint64_t wallTexLookupsTotal = 0;
uint64_t wallTexHitsTotal = 0;
uint64_t wallTexBuildsTotal = 0;
uint64_t wallTexBuildUsTotal = 0;
}

extern "C" uint32_t cyd_perf_micros(void) {
  return micros();
}

extern "C" void cyd_perf_record_sprites(uint16_t visible, uint16_t drawn, uint16_t decor,
                                        uint16_t bonus, uint16_t actor,
                                        uint32_t decorUs, uint32_t bonusUs, uint32_t actorUs) {
#if CYD_WOLF_ENABLE_PERF_LOGS
  spriteVisibleTotal += visible;
  spriteDrawnTotal += drawn;
  spriteDecorTotal += decor;
  spriteBonusTotal += bonus;
  spriteActorTotal += actor;
  spriteDecorUsTotal += decorUs;
  spriteBonusUsTotal += bonusUs;
  spriteActorUsTotal += actorUs;
#else
  (void)visible;
  (void)drawn;
  (void)decor;
  (void)bonus;
  (void)actor;
  (void)decorUs;
  (void)bonusUs;
  (void)actorUs;
#endif
}

extern "C" void cyd_perf_record_walltex(uint32_t lookups, uint32_t hits, uint32_t builds,
                                        uint32_t buildUs) {
#if CYD_WOLF_ENABLE_PERF_LOGS
  wallTexLookupsTotal += lookups;
  wallTexHitsTotal += hits;
  wallTexBuildsTotal += builds;
  wallTexBuildUsTotal += buildUs;
#else
  (void)lookups;
  (void)hits;
  (void)builds;
  (void)buildUs;
#endif
}

extern "C" void cyd_perf_record_render(uint32_t renderUs, uint32_t presentUs) {
#if CYD_WOLF_ENABLE_PERF_LOGS
  ++frameCount;
  renderTotalUs += renderUs;
  presentTotalUs += presentUs;

  const uint32_t nowMs = millis();
  if (nowMs - lastLogMs < 2000 || frameCount == 0) return;

  const uint32_t totalUs = static_cast<uint32_t>((renderTotalUs + presentTotalUs) / frameCount);
  const uint32_t renderAvg = static_cast<uint32_t>(renderTotalUs / frameCount);
  const uint32_t presentAvg = static_cast<uint32_t>(presentTotalUs / frameCount);
  const uint32_t fps10 = totalUs ? (10000000UL / totalUs) : 0;

  Serial.printf("PERF fps=%lu.%lu render=%lu.%03lums present=%lu.%03lums frames=%lu\n",
                static_cast<unsigned long>(fps10 / 10),
                static_cast<unsigned long>(fps10 % 10),
                static_cast<unsigned long>(renderAvg / 1000),
                static_cast<unsigned long>(renderAvg % 1000),
                static_cast<unsigned long>(presentAvg / 1000),
                static_cast<unsigned long>(presentAvg % 1000),
                static_cast<unsigned long>(frameCount));

  lastLogMs = nowMs;
  frameCount = 0;
  renderTotalUs = 0;
  presentTotalUs = 0;
#else
  (void)renderUs;
  (void)presentUs;
#endif
}

extern "C" void cyd_perf_record_render_phases(uint32_t prepUs, uint32_t clearUs,
                                              uint32_t wallUs, uint32_t spriteUs,
                                              uint32_t weaponUs, uint32_t presentUs) {
#if CYD_WOLF_ENABLE_PERF_LOGS
  const uint32_t renderUs = prepUs + clearUs + wallUs + spriteUs + weaponUs;
  ++frameCount;
  renderTotalUs += renderUs;
  presentTotalUs += presentUs;
  prepTotalUs += prepUs;
  clearTotalUs += clearUs;
  wallTotalUs += wallUs;
  spriteTotalUs += spriteUs;
  weaponTotalUs += weaponUs;

  const uint32_t nowMs = millis();
  if (nowMs - lastLogMs < 2000 || frameCount == 0) return;

  const uint32_t totalUs = static_cast<uint32_t>((renderTotalUs + presentTotalUs) / frameCount);
  const uint32_t renderAvg = static_cast<uint32_t>(renderTotalUs / frameCount);
  const uint32_t presentAvg = static_cast<uint32_t>(presentTotalUs / frameCount);
  const uint32_t prepAvg = static_cast<uint32_t>(prepTotalUs / frameCount);
  const uint32_t clearAvg = static_cast<uint32_t>(clearTotalUs / frameCount);
  const uint32_t wallAvg = static_cast<uint32_t>(wallTotalUs / frameCount);
  const uint32_t spriteAvg = static_cast<uint32_t>(spriteTotalUs / frameCount);
  const uint32_t weaponAvg = static_cast<uint32_t>(weaponTotalUs / frameCount);
  const uint32_t visibleAvg = spriteVisibleTotal / frameCount;
  const uint32_t drawnAvg = spriteDrawnTotal / frameCount;
  const uint32_t decorAvg = spriteDecorTotal / frameCount;
  const uint32_t bonusAvg = spriteBonusTotal / frameCount;
  const uint32_t actorAvg = spriteActorTotal / frameCount;
  const uint32_t decorUsAvg = static_cast<uint32_t>(spriteDecorUsTotal / frameCount);
  const uint32_t bonusUsAvg = static_cast<uint32_t>(spriteBonusUsTotal / frameCount);
  const uint32_t actorUsAvg = static_cast<uint32_t>(spriteActorUsTotal / frameCount);
  const uint32_t wallTexLookupsAvg = static_cast<uint32_t>(wallTexLookupsTotal / frameCount);
  const uint32_t wallTexHitsAvg = static_cast<uint32_t>(wallTexHitsTotal / frameCount);
  const uint32_t wallTexBuildsAvg = static_cast<uint32_t>(wallTexBuildsTotal / frameCount);
  const uint32_t wallTexBuildUsAvg = static_cast<uint32_t>(wallTexBuildUsTotal / frameCount);
  const uint32_t fps10 = totalUs ? (10000000UL / totalUs) : 0;

  Serial.printf("PERF fps=%lu.%lu render=%lu.%03lums prep=%lu.%03lums clear=%lu.%03lums wall=%lu.%03lums sprite=%lu.%03lums weapon=%lu.%03lums present=%lu.%03lums frames=%lu vis=%lu draw=%lu decor=%lu bonus=%lu actor=%lu decorUs=%lu.%03lums bonusUs=%lu.%03lums actorUs=%lu.%03lums wallTexLook=%lu wallTexHit=%lu wallTexBuild=%lu wallTexBuildUs=%lu.%03lums\n",
                static_cast<unsigned long>(fps10 / 10),
                static_cast<unsigned long>(fps10 % 10),
                static_cast<unsigned long>(renderAvg / 1000),
                static_cast<unsigned long>(renderAvg % 1000),
                static_cast<unsigned long>(prepAvg / 1000),
                static_cast<unsigned long>(prepAvg % 1000),
                static_cast<unsigned long>(clearAvg / 1000),
                static_cast<unsigned long>(clearAvg % 1000),
                static_cast<unsigned long>(wallAvg / 1000),
                static_cast<unsigned long>(wallAvg % 1000),
                static_cast<unsigned long>(spriteAvg / 1000),
                static_cast<unsigned long>(spriteAvg % 1000),
                static_cast<unsigned long>(weaponAvg / 1000),
                static_cast<unsigned long>(weaponAvg % 1000),
                static_cast<unsigned long>(presentAvg / 1000),
                static_cast<unsigned long>(presentAvg % 1000),
                static_cast<unsigned long>(frameCount),
                static_cast<unsigned long>(visibleAvg),
                static_cast<unsigned long>(drawnAvg),
                static_cast<unsigned long>(decorAvg),
                static_cast<unsigned long>(bonusAvg),
                static_cast<unsigned long>(actorAvg),
                static_cast<unsigned long>(decorUsAvg / 1000),
                static_cast<unsigned long>(decorUsAvg % 1000),
                static_cast<unsigned long>(bonusUsAvg / 1000),
                static_cast<unsigned long>(bonusUsAvg % 1000),
                static_cast<unsigned long>(actorUsAvg / 1000),
                static_cast<unsigned long>(actorUsAvg % 1000),
                static_cast<unsigned long>(wallTexLookupsAvg),
                static_cast<unsigned long>(wallTexHitsAvg),
                static_cast<unsigned long>(wallTexBuildsAvg),
                static_cast<unsigned long>(wallTexBuildUsAvg / 1000),
                static_cast<unsigned long>(wallTexBuildUsAvg % 1000));

  lastLogMs = nowMs;
  frameCount = 0;
  renderTotalUs = 0;
  presentTotalUs = 0;
  prepTotalUs = 0;
  clearTotalUs = 0;
  wallTotalUs = 0;
  spriteTotalUs = 0;
  weaponTotalUs = 0;
  spriteVisibleTotal = 0;
  spriteDrawnTotal = 0;
  spriteDecorTotal = 0;
  spriteBonusTotal = 0;
  spriteActorTotal = 0;
  spriteDecorUsTotal = 0;
  spriteBonusUsTotal = 0;
  spriteActorUsTotal = 0;
  wallTexLookupsTotal = 0;
  wallTexHitsTotal = 0;
  wallTexBuildsTotal = 0;
  wallTexBuildUsTotal = 0;
#else
  (void)prepUs;
  (void)clearUs;
  (void)wallUs;
  (void)spriteUs;
  (void)weaponUs;
  (void)presentUs;
#endif
}
