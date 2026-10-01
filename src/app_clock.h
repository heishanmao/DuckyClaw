/**
 * @file app_clock.h
 * @brief Clock AI skeleton module: periodic local-time display service.
 * @version 0.1
 * @copyright Copyright (c) 2026 TuyaOpenClaw (heishanmao). All Rights Reserved.
 */

#ifndef __APP_CLOCK_H__
#define __APP_CLOCK_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Clock AI module (1s tick).
 *
 * When CONFIG_ENABLE_APP_CLOCK_AI is not set this is a no-op returning OPRT_OK.
 *
 * @return OPRT_OK on success, otherwise an error code.
 */
OPERATE_RET app_clock_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CLOCK_H__ */
