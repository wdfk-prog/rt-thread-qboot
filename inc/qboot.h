/**
 * @file qboot.h
 * @brief
 * @author qiyongzhong
 * @version 1.0
 * @date 2020-07-06
 *
 * @copyright Copyright (c) 2026
 *
 * @note :
 * @par Change Log:
 * Date       Version Author      Description
 * 2020-07-06     qiyongzhong     first version
 * 2026-01-15     wdfk-prog       split header
 */

#ifndef __QBOOT_H__
#define __QBOOT_H__

#include "qboot_cfg.h"
#include "qboot_ops.h"
#include "qboot_stream.h"
#include "qboot_algo.h"
#include "crc32.h"

/**
 * @brief Jump to the application image.
 *
 * Platform code normally overrides this hook to switch MSP/PC and branch to APP.
 */
void qbt_jump_to_app(void);

/**
 * @brief Check an RBL firmware package header and body.
 *
 * @param fw_handle Firmware package handle.
 * @param part_len  Storage partition length in bytes.
 * @param name      Storage name for diagnostics.
 * @param fw_info   [out] Parsed firmware header.
 * @return RT_TRUE when package validation succeeds, RT_FALSE otherwise.
 */
rt_bool_t qbt_fw_check(void *fw_handle, rt_uint32_t part_len, const char *name, fw_info_t *fw_info);

#ifdef QBOOT_CI_HOST_TEST
/**
 * @brief Release firmware from DOWNLOAD to APP for host-side CI simulation.
 *
 * @param check_sign RT_TRUE to skip an already released package.
 *
 * @return RT_TRUE on release success, RT_FALSE otherwise.
 */
rt_bool_t qbt_ci_release_from_download(rt_bool_t check_sign);

/**
 * @brief Release firmware from a selected source target for host-side tests.
 *
 * @param src_id     Source target identifier.
 * @param check_sign RT_TRUE to skip an already released package.
 *
 * @return RT_TRUE on release success, RT_FALSE otherwise.
 */
rt_bool_t qbt_ci_release_from_target(qbt_target_id_t src_id, rt_bool_t check_sign);

/**
 * @brief Run one qboot startup pass synchronously for host-side tests.
 */
void qbt_ci_run_boot_once(void);

/**
 * @brief Execute the qboot shell command handler from host-side tests.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 */
void qbt_ci_shell_cmd(rt_uint8_t argc, char **argv);

/**
 * @brief Force qboot_register_storage_ops() to fail in host-side tests.
 *
 * @param enable RT_TRUE to force failure, RT_FALSE for normal registration.
 */
void qbt_ci_storage_register_fail_set(rt_bool_t enable);
#endif /* QBOOT_CI_HOST_TEST */

#endif /* __QBOOT_H__ */
