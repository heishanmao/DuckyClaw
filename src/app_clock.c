/**
 * @file app_clock.c
 * @brief Clock AI skeleton: a 1-second timer that reads the Tuya time
 *        service, logs the local time, and (when the AI display is enabled)
 *        shows HH:MM:SS in the status-bar notification label.
 *
 * Design notes:
 *  - Uses tal_time_get_local_time_custom() so timezone/DST are handled by
 *    the Tuya time service after cloud time sync.
 *  - The status-bar notification path (AI_UI_DISP_NOTIFICATION) is the
 *    SDK-supported way for app code to show text without owning the screen.
 *    A full-screen clock page can be layered on top of ai_ui_page later.
 *  - Gate: ENABLE_APP_CLOCK_AI (Kconfig `config ENABLE_APP_CLOCK_AI`;
 *    generated header tuya_kconfig.h strips the CONFIG_ prefix).
 *
 * @version 0.1
 * @copyright Copyright (c) 2026 TuyaOpenClaw (heishanmao). All Rights Reserved.
 */

#include "tal_api.h"
#include "tal_time_service.h"

#if defined(ENABLE_APP_CLOCK_AI)

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
#include "ai_ui_manage.h"
#endif

#define APP_CLOCK_TICK_MS       1000
#define APP_CLOCK_TIME_TEXT_LEN 24

static TIMER_ID sg_clock_timer  = NULL;
static bool     sg_clock_running = false;

/**
 * @brief Format current local time as "HH:MM:SS YYYY-MM-DD".
 */
static void __app_clock_format(char *buf, uint32_t buf_len)
{
    POSIX_TM_S tm = {0};

    if (OPRT_OK != tal_time_get_local_time_custom(0, &tm)) {
        snprintf(buf, buf_len, "--:--:--");
        return;
    }
    snprintf(buf, buf_len, "%02d:%02d:%02d %04d-%02d-%02d",
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

/**
 * @brief 1-second tick: log local time, push to status-bar notification.
 */
static void __app_clock_tick(TIMER_ID timer_id, void *arg)
{
    char full[APP_CLOCK_TIME_TEXT_LEN] = {0};

    (void)timer_id;
    (void)arg;

    __app_clock_format(full, sizeof(full));
    PR_NOTICE("[clock] %s", full);

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, (uint8_t *)full, (int)strlen(full));
#endif
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
