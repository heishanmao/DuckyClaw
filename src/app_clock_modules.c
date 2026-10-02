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
#include "tal_time_service.h"
#include "tuya_weather.h"
#include "app_base_config.h"

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
#include "ai_ui_manage.h"
#endif

#include "lv_port_disp.h"
#include "lv_port_indev.h"

#define APP_CLOCK_MODULES_FIRST_TICK  30   /* first weather fetch ~30 s after boot */
#define APP_CLOCK_MODULES_WEATHER_INT 1800 /* then every 30 min */

/* ---- DIM (night backlight) module ---- */
#define APP_CLOCK_KV_DIM_ENABLE   "clock_dim_enable"
#define APP_CLOCK_KV_DIM_START    "clock_dim_start"
#define APP_CLOCK_KV_DIM_END      "clock_dim_end"
#define APP_CLOCK_KV_DIM_LEVEL    "clock_dim_level"  /* base brightness (user master, 5-100) */
#define APP_CLOCK_KV_DIM_NIGHT    "clock_dim_night"  /* brightness while auto-dimming at night */
#define APP_CLOCK_KV_DIM_WAKE     "clock_dim_wake"

#define APP_CLOCK_DIM_DEF_ENABLE  "1"
#define APP_CLOCK_DIM_DEF_START   "23:00"
#define APP_CLOCK_DIM_DEF_END     "07:00"
#define APP_CLOCK_DIM_DEF_LEVEL   "100"    /* base brightness, slider-backed */
#define APP_CLOCK_DIM_DEF_NIGHT   "5"      /* night dim level (low but readable) */
#define APP_CLOCK_DIM_DEF_WAKE    "60"     /* seconds to stay bright after a tap */
#define APP_CLOCK_DIM_MIN_LEVEL   5        /* slider floor: never go fully black */

static int s_dim_enable       = 1;
static int s_dim_start_min    = 23 * 60;
static int s_dim_end_min      = 7 * 60;
static int s_dim_level        = 100;
static int s_dim_night_level  = 5;
static int s_dim_wake_s       = 60;
static int s_dim_cur_level    = -1;    /* last backlight level actually applied */
static int s_dim_force_level  = -1;    /* >=0 = CLI forced brightness override */
static volatile int s_dim_test = 0;    /* 1 = CLI "dim test": simulate night now */
static volatile uint32_t s_dim_ticks          = 0;
static volatile uint32_t s_dim_last_touch_ticks = 0;

static void __mod_dim_apply(int level)
{
    if (level == s_dim_cur_level) {
        return;
    }
    s_dim_cur_level = level;
    lv_display_t *disp = lv_port_get_lv_disp_by_name((char *)"display");
    if (disp != NULL) {
        disp_set_backlight(disp, (uint8_t)(level < 0 ? 0 : (level > 100 ? 100 : level)));
        PR_NOTICE("[clock] dim backlight -> %d", level);
    } else {
        PR_WARN("[clock] dim: display handle not found");
    }
}

/* Called from the touch input scan (any press). Safe from LVGL thread. */
void app_clock_dim_notify_touch(void)
{
    s_dim_last_touch_ticks = s_dim_ticks;
}

/* CLI: force a fixed brightness (0-100). -1 restores automatic behavior. */
void app_clock_dim_force(int level)
{
    s_dim_force_level = (level < 0) ? -1 : (level > 100 ? 100 : level);
    s_dim_test = 0;
    if (s_dim_force_level < 0) {
        s_dim_cur_level = -1;   /* re-evaluate on next tick */
    } else {
        __mod_dim_apply(s_dim_force_level);
    }
}

/* CLI: simulate night-time now (for testing auto-dim + touch wake). */
void app_clock_dim_test(bool on)
{
    s_dim_test = on ? 1 : 0;
    s_dim_force_level = -1;
    s_dim_cur_level = -1;       /* re-evaluate on next tick */
}

void app_clock_dim_get_state(int *level, int *mode)
{
    if (level != NULL) {
        *level = s_dim_cur_level;
    }
    if (mode != NULL) {
        *mode = (s_dim_force_level >= 0) ? 1 : 0;
    }
}

static void __mod_dim_load_cfg(void)
{
    char buf[24] = {0};

    if (OPRT_OK == app_kv_get_string(APP_CLOCK_KV_DIM_ENABLE,
                                     APP_CLOCK_DIM_DEF_ENABLE, buf, sizeof(buf))) {
        s_dim_enable = (strcmp(buf, "0") != 0);
    }
    buf[0] = '\0';
    if (OPRT_OK == app_kv_get_string(APP_CLOCK_KV_DIM_START,
                                     APP_CLOCK_DIM_DEF_START, buf, sizeof(buf))) {
        int h = 0, m = 0;
        if (sscanf(buf, "%d:%d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
            s_dim_start_min = h * 60 + m;
        }
    }
    buf[0] = '\0';
    if (OPRT_OK == app_kv_get_string(APP_CLOCK_KV_DIM_END,
                                     APP_CLOCK_DIM_DEF_END, buf, sizeof(buf))) {
        int h = 0, m = 0;
        if (sscanf(buf, "%d:%d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
            s_dim_end_min = h * 60 + m;
        }
    }
    buf[0] = '\0';
    if (OPRT_OK == app_kv_get_string(APP_CLOCK_KV_DIM_LEVEL,
                                     APP_CLOCK_DIM_DEF_LEVEL, buf, sizeof(buf))) {
        int v = atoi(buf);
        if (v >= APP_CLOCK_DIM_MIN_LEVEL && v <= 100) {
            s_dim_level = v;
        }
    }
    buf[0] = '\0';
    if (OPRT_OK == app_kv_get_string(APP_CLOCK_KV_DIM_NIGHT,
                                     APP_CLOCK_DIM_DEF_NIGHT, buf, sizeof(buf))) {
        int v = atoi(buf);
        if (v >= 0 && v <= 100) {
            s_dim_night_level = v;
        }
    }
    buf[0] = '\0';
    if (OPRT_OK == app_kv_get_string(APP_CLOCK_KV_DIM_WAKE,
                                     APP_CLOCK_DIM_DEF_WAKE, buf, sizeof(buf))) {
        int v = atoi(buf);
        if (v >= 0 && v <= 3600) {
            s_dim_wake_s = v;
        }
    }
}

static void __mod_dim_init(void)
{
    __mod_dim_load_cfg();

    lv_port_indev_set_touch_cb(app_clock_dim_notify_touch);
    PR_NOTICE("[clock] dim init: enable=%d %02d:%02d-%02d:%02d level=%d night=%d wake=%ds",
              s_dim_enable, s_dim_start_min / 60, s_dim_start_min % 60,
              s_dim_end_min / 60, s_dim_end_min % 60, s_dim_level,
              s_dim_night_level, s_dim_wake_s);
}

/* ---- Settings-page accessors (persist KV + apply immediately) ---- */

void app_clock_dim_get_cfg(int *enable, int *start_min, int *end_min, int *level)
{
    if (enable)    *enable    = s_dim_enable;
    if (start_min) *start_min = s_dim_start_min;
    if (end_min)   *end_min   = s_dim_end_min;
    if (level)     *level     = s_dim_level;
}

void app_clock_dim_reload(void)
{
    __mod_dim_load_cfg();
    s_dim_force_level = -1;
    s_dim_cur_level   = -1;   /* re-evaluate on next tick */
}

void app_clock_dim_set_enable(bool on)
{
    const char *v = on ? "1" : "0";
    s_dim_enable = on ? 1 : 0;
    app_kv_set_string(APP_CLOCK_KV_DIM_ENABLE, v);
    s_dim_force_level = -1;
    s_dim_cur_level   = -1;   /* re-evaluate on next tick */
}

void app_clock_dim_set_level(int level)
{
    char buf[8] = {0};

    /* The slider is the user's master brightness control: clamp to the
     * readable floor (5%) so the screen never goes fully black from UI. */
    if (level < APP_CLOCK_DIM_MIN_LEVEL) {
        level = APP_CLOCK_DIM_MIN_LEVEL;
    }
    if (level > 100) {
        level = 100;
    }
    s_dim_level = level;
    snprintf(buf, sizeof(buf), "%d", level);
    app_kv_set_string(APP_CLOCK_KV_DIM_LEVEL, buf);
    /* Apply immediately and treat the drag as a wake interaction: at night the
     * brightness the user just chose is kept for the wake window, then the
     * auto-dim (night level) takes over again. During the day the next tick
     * simply keeps this value. */
    s_dim_last_touch_ticks = s_dim_ticks;
    __mod_dim_apply(level);
}

void app_clock_dim_preview_done(void)
{
    s_dim_cur_level = -1;   /* re-evaluate on next tick */
}

void app_clock_dim_set_period(const char *start, const char *end)
{
    int h = 0, m = 0;

    if (start != NULL && sscanf(start, "%d:%d", &h, &m) == 2 &&
        h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        s_dim_start_min = h * 60 + m;
        app_kv_set_string(APP_CLOCK_KV_DIM_START, start);
    }
    if (end != NULL && sscanf(end, "%d:%d", &h, &m) == 2 &&
        h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        s_dim_end_min = h * 60 + m;
        app_kv_set_string(APP_CLOCK_KV_DIM_END, end);
    }
    s_dim_force_level = -1;
    s_dim_cur_level   = -1;   /* re-evaluate on next tick */
}

static void __mod_dim_tick(void)
{
    int  target = s_dim_level;   /* base brightness = the slider's value */
    int  in_night = 0;
    POSIX_TM_S tm = {0};

    s_dim_ticks++;

    if (s_dim_force_level >= 0) {
        /* CLI forced mode keeps the fixed brightness; nothing to re-evaluate. */
        return;
    }

    if (s_dim_test) {
        in_night = 1;   /* "dim test": pretend it is night right now */
    } else if (s_dim_enable &&
               OPRT_OK == tal_time_get_local_time_custom(0, &tm)) {
        int now_min = tm.tm_hour * 60 + tm.tm_min;

        if (s_dim_start_min <= s_dim_end_min) {
            in_night = (now_min >= s_dim_start_min && now_min < s_dim_end_min);
        } else {
            in_night = (now_min >= s_dim_start_min || now_min < s_dim_end_min);
        }
    }

    if (in_night) {
        if (s_dim_wake_s > 0 &&
            (uint32_t)(s_dim_ticks - s_dim_last_touch_ticks) < (uint32_t)s_dim_wake_s) {
            target = s_dim_level;   /* touch wake window: back to user brightness */
        } else {
            target = s_dim_night_level;   /* auto dim while sleeping */
        }
    }

    __mod_dim_apply(target);
}

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
    __mod_dim_init();
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
