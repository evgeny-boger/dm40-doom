/*
 * Build configuration of the DM40 Doom port (from the MG24 port's main.h).
 */
#ifndef MAIN_H
#define MAIN_H
#include <stdbool.h>
#include <stdint.h>
#include "macros.h"
#include "boards.h"
#include "dm40.h"

#define DISABLE_CACHING_TEXTURE_TO_FLASH  0
#define TEST_DISABLE_ASYNCH_LOAD          0
#define FAST_CPU_SMALL_FLASH              1
#define CORRECT_TABLE_ERROR               1
#define CACHE_ALL_COLORMAP_TO_RAM         0
#define DEMO_SUPPORT                      1
#define DOOM2SUPPORT                      1
#define HAS_NETWORK                       0
#define SHOW_FPS_IN_HUD                   true
#define SHOW_FPS_IN_AMMO                  false
#define TIME_DEMO                         NULL
#define NO_DEMO_LOOP                      false
#define START_MAP                         1
#define DEBUG_OUT_PRINTF                  1
#define USE_SHARED_USART                  0
#define ENABLE_MUSIC                      0
#define MUSIC_NUM_SAMPLES                 1024

#define SET_FLASH_MODE()
#define SET_DISPLAY_MODE()
static inline void sl_system_process_action(void) { wdt_kick(); }
#endif
