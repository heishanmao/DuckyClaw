/**
 * @file app_clock_modules.h
 * @brief Clock AI feature-module registry (Clock AI architecture).
 *
 * Every future clock-page feature is a registered module driven by the
 * clock's 1-second tick. Modules expose fetch (heavy, on workqueue),
 * tick (1s, UI-safe) and show (UI section visibility) hooks so new
 * features can be added without touching the clock page or the weather
 * pipeline.
 *
 * Currently implemented:
 *   - CLOCK_MOD_WEATHER   : current weather (wired to the existing fetch)
 * Planned (registry slots ready, callbacks to be filled):
 *   - CLOCK_MOD_REMINDER  : alarms / reminders via cron_service
 *   - CLOCK_MOD_CALENDAR  : lunar date / festivals / holidays
 *   - CLOCK_MOD_AI_STATUS : AI online / listening indicator
 *   - CLOCK_MOD_DIM       : night-time backlight dimming
 *
 * @version 0.1
 * @copyright Copyright (c) 2026 TuyaOpenClaw (heishanmao). All Rights Reserved.
 */

#ifndef __APP_CLOCK_MODULES_H__
#define __APP_CLOCK_MODULES_H__

#include "tal_api.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CLOCK_MOD_WEATHER = 0,   /* current weather + high/low + humidity + city */
    CLOCK_MOD_REMINDER,      /* alarms / reminders (cron_service backed) */
    CLOCK_MOD_CALENDAR,      /* lunar / festivals / holidays */
    CLOCK_MOD_AI_STATUS,     /* AI assistant online / listening state */
    CLOCK_MOD_DIM,           /* auto backlight dimming at night */
    CLOCK_MOD_COUNT,
} CLOCK_MOD_E;

typedef struct {
    CLOCK_MOD_E id;
    const char *name;        /* module name, e.g. "weather" */
    bool        enabled;     /* runtime switch (persisted later via KV) */
    uint32_t    period_s;    /* fetch period in seconds; 0 = never auto-fetch */
    uint32_t    counter;     /* internal countdown to next fetch */
    void (*fetch)(void);     /* heavy work -> run on workqueue, NULL if none */
    void (*tick)(void);      /* called every 1 s on the clock tick; UI-safe */
    void (*show)(bool on);   /* show/hide this module's UI section */
} CLOCK_MOD_T;

/**
 * @brief Init the module registry. Called once from app_clock_init().
 */
void app_clock_modules_init(void);

/**
 * @brief Drive module scheduling; called from the 1-second clock tick.
 */
void app_clock_modules_tick(void);

/**
 * @brief Enable/disable a module at runtime.
 * @param[in] id     module id
 * @param[in] on     true = enable, false = disable
 */
void app_clock_module_set_enabled(CLOCK_MOD_E id, bool on);

/**
 * @brief Ask a module to refresh now (e.g. after settings change).
 * @param[in] id     module id
 */
void app_clock_module_request_fetch(CLOCK_MOD_E id);

/**
 * @brief Get the registry (read-only usage for debug / settings).
 * @return pointer to the module table
 */
const CLOCK_MOD_T *app_clock_modules_get(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CLOCK_MODULES_H__ */
