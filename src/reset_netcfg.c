/**
 * @file reset_netcfg.c
 * @brief Reset network configuration — DISABLED automatic trigger (Clock AI).
 *
 * The stock Tuya demo auto-resets the device when it boots RESET_NETCNT_MAX
 * (3) times within a 5 s window: tuya_iot_reset() unbinds the device from
 * the Tuya cloud, which made the clock disappear from TuyaApp whenever the
 * user toggled screen orientation (each toggle reboots the device after
 * 800 ms — easily 3+ reboots in 5 s, tripping the counter).
 *
 * Automatic reset is therefore removed. Factory reset / re-pairing is still
 * available on demand through the serial CLI:
 *     cfg_reset          -> clear all app config KV overrides + authorize
 *     (then reboot and re-pair with TuyaApp)
 *
 * @version 0.3
 * @copyright Copyright (c) 2026 TuyaOpenClaw (heishanmao). All Rights Reserved.
 */

#include "reset_netcfg.h"

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
***********************function define**********************
***********************************************************/

int reset_netconfig_start(void)
{
    /* No-op: the boot-count based auto-reset is disabled so rapid reboots
     * (e.g. orientation switching) never unbind the device from the cloud.
     * Factory reset is done explicitly via the `cfg_reset` CLI command. */
    return OPRT_OK;
}

int reset_netconfig_check(void)
{
    /* No-op: see reset_netconfig_start(). */
    return OPRT_OK;
}
