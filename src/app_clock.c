/**
 * @file app_clock.c
 * @brief Clock AI: a 1-second timer that reads the Tuya time service,
 *        logs the local time, and pushes HH:MM:SS to the full-screen
 *        clock page (AI_UI_DISP_CLOCK_UPDATE_TIME) when the AI display
 *        is enabled.
 *
 * Design notes:
 *  - Uses tal_time_get_local_time_custom() so timezone/DST are handled by
 *    the Tuya time service after cloud time sync.
 *  - The full-screen clock page lives in ai_ui (wechat variant); this
 *    module only feeds it via ai_ui_disp_msg(). Status-bar notification
 *    is no longer used for the clock.
 *  - Gate: ENABLE_APP_CLOCK_AI (Kconfig `config ENABLE_APP_CLOCK_AI`;
 *    generated header tuya_kconfig.h strips the CONFIG_ prefix).
 *
 * @version 0.2
 * @copyright Copyright (c) 2026 TuyaOpenClaw (heishanmao). All Rights Reserved.
 */

#include "tal_api.h"
#include "tal_time_service.h"
#include "tal_workq_service.h"
#include "tuya_weather.h"

#if defined(ENABLE_APP_CLOCK_AI)

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
#include "ai_ui_manage.h"
#endif

#define APP_CLOCK_TICK_MS           1000
#define APP_CLOCK_TIME_TEXT_LEN     32
#define APP_CLOCK_WEATHER_FIRST_TICK 30   /* first fetch ~30 s after boot */
#define APP_CLOCK_WEATHER_INTERVAL  1800  /* then every 30 min */

static TIMER_ID sg_clock_timer  = NULL;
static bool     sg_clock_running = false;

static uint32_t        sg_weather_tick = APP_CLOCK_WEATHER_FIRST_TICK;
static volatile bool   sg_weather_busy = false;

/**
 * @brief Format current local time as "HH:MM:SS YYYY-MM-DD".
 */
static void __app_clock_format(char *buf, uint32_t buf_len, POSIX_TM_S *out_tm)
{
    POSIX_TM_S tm = {0};

    if (OPRT_OK != tal_time_get_local_time_custom(0, &tm)) {
        snprintf(buf, buf_len, "--:--:--");
        if (out_tm != NULL) {
            memset(out_tm, 0, sizeof(POSIX_TM_S));
        }
        return;
    }
    if (out_tm != NULL) {
        memcpy(out_tm, &tm, sizeof(POSIX_TM_S));
    }
    snprintf(buf, buf_len, "%02d:%02d:%02d %04d-%02d-%02d",
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

/**
 * @brief Fetch weather from the Tuya cloud and push it to the clock page.
 *
 * Runs on the low-priority workqueue (blocking cloud call is allowed);
 * triggered from the 1s tick every APP_CLOCK_WEATHER_INTERVAL.
 */
static void __app_clock_weather_fetch(void *arg)
{
    WEATHER_CURRENT_CONDITIONS_T cur = {0};
    UI_DISP_CLOCK_WEATHER_T wi = {0};
    int high = 0;
    int low  = 0;

    (void)arg;

    if (false == tuya_weather_allow_update()) {
        sg_weather_busy = false;
        return;
    }
    if (OPRT_OK != tuya_weather_get_current_conditions(&cur)) {
        sg_weather_busy = false;
        return;
    }
    /* Best effort; failure only drops today's high/low range. */
    tuya_weather_get_today_high_low_temp(&high, &low);

    wi.weather_code = cur.weather;
    wi.temperature  = cur.temp;
    wi.temp_high    = high;
    wi.temp_low     = low;

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    ai_ui_disp_msg(AI_UI_DISP_CLOCK_UPDATE_WEATHER,
                   (uint8_t *)&wi, sizeof(UI_DISP_CLOCK_WEATHER_T));
#endif
    PR_NOTICE("[clock] weather code=%d temp=%dC high=%dC low=%dC",
              cur.weather, cur.temp, high, low);
    sg_weather_busy = false;
}

/**
 * @brief 1-second tick: log local time, push to the full-screen clock page.
 */
static void __app_clock_tick(TIMER_ID timer_id, void *arg)
{
    char full[APP_CLOCK_TIME_TEXT_LEN] = {0};

    (void)timer_id;
    (void)arg;

    __app_clock_format(full, sizeof(full), NULL);
    PR_NOTICE("[clock] %s", full);

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    {
        UI_DISP_CLOCK_TIME_T time_info = {0};

        __app_clock_format(full, sizeof(full), &time_info.curr_time);
        ai_ui_disp_msg(AI_UI_DISP_CLOCK_UPDATE_TIME,
                       (uint8_t *)&time_info, sizeof(UI_DISP_CLOCK_TIME_T));
    }
#endif

    /* Periodic weather refresh on the low-priority workqueue. */
    if (0 == --sg_weather_tick && !sg_weather_busy) {
        sg_weather_tick = APP_CLOCK_WEATHER_INTERVAL;
        sg_weather_busy = true;
        tal_workq_schedule(WORKQ_SYSTEM, __app_clock_weather_fetch, NULL);
    }
}

OPERATE_RET app_clock_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (sg_clock_running) {
        return OPRT_OK;
    }

    rt = tal_sw_timer_create(__app_clock_tick, NULL, &sg_clock_timer);
    if (OPRT_OK != rt) {
        PR_ERR("[clock] sw timer create failed rt:%d", rt);
        return rt;
    }

    rt = tal_sw_timer_start(sg_clock_timer, APP_CLOCK_TICK_MS, TAL_TIMER_CYCLE);
    if (OPRT_OK != rt) {
        PR_ERR("[clock] sw timer start failed rt:%d", rt);
        return rt;
    }

    sg_clock_running = true;
    PR_NOTICE("[clock] Clock AI enabled (tick %d ms)", APP_CLOCK_TICK_MS);
    return OPRT_OK;
}

#else /* !ENABLE_APP_CLOCK_AI */

OPERATE_RET app_clock_init(void)
{
    return OPRT_OK;
}

#endif /* ENABLE_APP_CLOCK_AI */
