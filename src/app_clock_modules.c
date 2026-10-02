/**
 * @file app_clock_modules.c
 * @brief Clock AI feature-module registry implementation.
 *
 * See app_clock_modules.h for the module list and scheduling model.
 *
 * The weather module reuses the existing Tuya weather fetch pipeline
 * (tuya_weather_* + ai_ui_disp_msg(AI_UI_DISP_CLOCK_UPDATE_WEATHER)),
 * so enabling this framework does not change the current clock behavior.
 * Reminder / calendar / AI-status / dim modules are registered with
 * placeholder callbacks; each gains its real implementation as the
 * feature is built.
 *
 * @version 0.1
 * @copyright Copyright (c) 2026 TuyaOpenClaw (heishanmao). All Rights Reserved.
 */

#include "app_clock_modules.h"
#include "tal_log.h"
#include "tal_workq_service.h"
#include "tuya_weather.h"

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
#include "ai_ui_manage.h"
#endif

#define APP_CLOCK_MODULES_FIRST_TICK  30   /* first weather fetch ~30 s after boot */
#define APP_CLOCK_MODULES_WEATHER_INT 1800 /* then every 30 min */

/* ---------------------------------------------------------------------------
 * Module fetch implementations
 * --------------------------------------------------------------------------- */

static void __mod_weather_fetch(void *arg)
{
    WEATHER_CURRENT_CONDITIONS_T cur = {0};
    UI_DISP_CLOCK_WEATHER_T wi = {0};
    int high = 0;
    int low  = 0;

    (void)arg;

    if (false == tuya_weather_allow_update()) {
        return;
    }
    if (OPRT_OK != tuya_weather_get_current_conditions(&cur)) {
        return;
    }
    /* Best effort; failure only drops today's high/low range. */
    tuya_weather_get_today_high_low_temp(&high, &low);
    /* Best effort; city name (province/city/area -> "city"). */
    {
        char province[32] = {0}, city[32] = {0}, area[32] = {0};

        if (OPRT_OK == tuya_weather_get_city(province, sizeof(province),
                                             city, sizeof(city),
                                             area, sizeof(area)) &&
            city[0] != '\0') {
            snprintf(wi.city, sizeof(wi.city), "%s%s",
                     city, (area[0] != '\0' && strcmp(area, city) != 0) ? area : "");
        }
    }

    wi.weather_code = cur.weather;
    wi.temperature  = cur.temp;
    wi.temp_high    = high;
    wi.temp_low     = low;
    wi.humi         = cur.humi;

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    ai_ui_disp_msg(AI_UI_DISP_CLOCK_UPDATE_WEATHER,
                   (uint8_t *)&wi, sizeof(UI_DISP_CLOCK_WEATHER_T));
#endif
    PR_NOTICE("[clock] mod weather code=%d temp=%dC high=%dC low=%dC humi=%d city=%s",
              cur.weather, cur.temp, high, low, cur.humi, wi.city);
}

static void __mod_weather_fetch_sched(void)
{
    tal_workq_schedule(WORKQ_SYSTEM, __mod_weather_fetch, NULL);
}

/* ---- placeholder modules (to be implemented) ---- */

static void __mod_reminder_fetch_sched(void)
{
    /* TODO: read cron_service alarms and push AI_UI_DISP_CLOCK_UPDATE_REMINDER */
}

static void __mod_calendar_fetch_sched(void)
{
    /* TODO: compute lunar date / festivals; push AI_UI_DISP_CLOCK_UPDATE_CALENDAR */
}

static void __mod_ai_status_tick(void)
{
    /* TODO: show AI online / listening state in a clock-page corner */
}

static void __mod_dim_tick(void)
{
    /* TODO: after 22:00 / before 07:00, lower the backlight brightness */
}

/* ---------------------------------------------------------------------------
 * Registry
 * --------------------------------------------------------------------------- */

static CLOCK_MOD_T s_modules[CLOCK_MOD_COUNT] = {
    {
        .id = CLOCK_MOD_WEATHER, .name = "weather", .enabled = true,
        .period_s = APP_CLOCK_MODULES_WEATHER_INT,
        .counter  = APP_CLOCK_MODULES_FIRST_TICK,
        .fetch    = __mod_weather_fetch_sched, .tick = NULL, .show = NULL,
    },
    {
        .id = CLOCK_MOD_REMINDER, .name = "reminder", .enabled = true,
        .period_s = 0, .counter = 0,     /* fetch wired up with the feature */
        .fetch = __mod_reminder_fetch_sched, .tick = NULL, .show = NULL,
    },
    {
        .id = CLOCK_MOD_CALENDAR, .name = "calendar", .enabled = true,
        .period_s = 0, .counter = 0,     /* fetch wired up with the feature */
        .fetch = __mod_calendar_fetch_sched, .tick = NULL, .show = NULL,
    },
    {
        .id = CLOCK_MOD_AI_STATUS, .name = "ai_status", .enabled = true,
        .period_s = 0, .counter = 0,
        .fetch = NULL, .tick = __mod_ai_status_tick, .show = NULL,
    },
    {
        .id = CLOCK_MOD_DIM, .name = "dim", .enabled = true,
        .period_s = 0, .counter = 0,
        .fetch = NULL, .tick = __mod_dim_tick, .show = NULL,
    },
};

void app_clock_modules_init(void)
{
    for (int i = 0; i < CLOCK_MOD_COUNT; i++) {
        if (s_modules[i].counter == 0 && s_modules[i].period_s != 0) {
            s_modules[i].counter = s_modules[i].period_s;
        }
    }
    PR_NOTICE("[clock] modules registry ready (%d modules)", CLOCK_MOD_COUNT);
}

void app_clock_modules_tick(void)
{
    for (int i = 0; i < CLOCK_MOD_COUNT; i++) {
        CLOCK_MOD_T *m = &s_modules[i];
        if (!m->enabled) {
            continue;
        }
        if (m->tick != NULL) {
            m->tick();
        }
        if (m->period_s != 0 && m->fetch != NULL && (0 == --m->counter)) {
            m->counter = m->period_s;
            m->fetch();
        }
    }
}

void app_clock_module_set_enabled(CLOCK_MOD_E id, bool on)
{
    if (id >= CLOCK_MOD_COUNT) {
        return;
    }
    s_modules[id].enabled = on;
    if (on && s_modules[id].show != NULL) {
        s_modules[id].show(true);
    } else if (!on && s_modules[id].show != NULL) {
        s_modules[id].show(false);
    }
}

void app_clock_module_request_fetch(CLOCK_MOD_E id)
{
    if (id >= CLOCK_MOD_COUNT || s_modules[id].fetch == NULL) {
        return;
    }
    s_modules[id].counter = 1;   /* next tick schedules the fetch */
}

const CLOCK_MOD_T *app_clock_modules_get(void)
{
    return s_modules;
}
