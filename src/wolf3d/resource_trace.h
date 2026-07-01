#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  CYD_TRACE_SPRITE_DECOR = 1,
  CYD_TRACE_SPRITE_BONUS = 2,
  CYD_TRACE_SPRITE_ACTOR = 3,
  CYD_TRACE_SPRITE_WEAPON = 4
};

enum {
  CYD_TRACE_STATIC_DECOR = 1,
  CYD_TRACE_STATIC_BLOCK = 2,
  CYD_TRACE_STATIC_BONUS = 3
};

void cyd_trace_frame(void);
void cyd_trace_pm_hit(int page);
void cyd_trace_pm_miss(int page, uint32_t bytes, uint32_t readUs);
void cyd_trace_gr_request(int chunk, int32_t compressedBytes);
void cyd_trace_gr_expand(int chunk, int32_t expandedBytes);
void cyd_trace_sprite(int shapenum, int category, unsigned height);
void cyd_trace_sound(int sound, int usedPcm);
void cyd_trace_static_spawn(int used, int capacity, int category, int type, int shapenum);
void cyd_trace_static_drop(int used, int capacity, int type, int shapenum);

#ifdef __cplusplus
}
#endif
