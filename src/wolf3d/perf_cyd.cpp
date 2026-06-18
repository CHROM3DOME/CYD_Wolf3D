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
}

extern "C" uint32_t cyd_perf_micros(void) {
  return micros();
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
  const uint32_t fps10 = totalUs ? (10000000UL / totalUs) : 0;

  Serial.printf("PERF fps=%lu.%lu render=%lu.%03lums prep=%lu.%03lums clear=%lu.%03lums wall=%lu.%03lums sprite=%lu.%03lums weapon=%lu.%03lums present=%lu.%03lums frames=%lu\n",
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
                static_cast<unsigned long>(frameCount));

  lastLogMs = nowMs;
  frameCount = 0;
  renderTotalUs = 0;
  presentTotalUs = 0;
  prepTotalUs = 0;
  clearTotalUs = 0;
  wallTotalUs = 0;
  spriteTotalUs = 0;
  weaponTotalUs = 0;
#else
  (void)prepUs;
  (void)clearUs;
  (void)wallUs;
  (void)spriteUs;
  (void)weaponUs;
  (void)presentUs;
#endif
}
