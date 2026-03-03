/**
 * @file qboot_update.c
 * @brief 
 * @author wdfk-prog ()
 * @version 1.0
 * @date 2026-01-23
 * 
 * @copyright Copyright (c) 2026  
 * 
 * @note :
 * @par Change Log:
 * Date       Version Author      Description
 * 2026-01-23 1.0     wdfk-prog   first version
 */
#include "qboot_update.h"

#define DBG_TAG "qb_upd"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/** @brief Default wait window before falling back to app (ms). */
#define QBT_UPDATE_WAIT_MS_DEFAULT (QBOOT_SHELL_KEY_CHK_TMO * 1000)
/** @brief Default idle timeout during download before treating as interruption (ms). */
#define QBT_UPDATE_IDLE_MS_DEFAULT 30000u

/** @brief Bootloader-provided callbacks. */
static const qbt_update_ops_t *s_ops = RT_NULL;
/** @brief Current update state. */
static qbt_upd_state_t s_state = QBT_UPD_STATE_IDLE;
/** @brief Timestamp when WAIT state started (ticks). */
static rt_tick_t s_wait_start = 0;
/** @brief Timestamp of last received data (ticks). */
static rt_tick_t s_download_last = 0;
/** @brief WAIT state timeout in milliseconds. */
static rt_uint32_t s_wait_ms = QBT_UPDATE_WAIT_MS_DEFAULT;
/** @brief RECV idle timeout in milliseconds. */
static rt_uint32_t s_idle_ms = QBT_UPDATE_IDLE_MS_DEFAULT;
/** @brief One-shot flag to release qboot wait loop. */
static rt_bool_t s_ready_flag = RT_FALSE;
/** @brief One-shot recover probe gate for current download session. */
static rt_bool_t s_recover_probe_used = RT_FALSE;
static const char *const s_state_names[] = {
    "IDLE",
    "WAIT_DOWNLOAD",
    "RECV_DOWNLOAD",
    "DONE",
    "ERROR",
    "READY",
};

#ifdef QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER
static const qboot_store_desc_t *s_download_desc = RT_NULL;
static void *s_download_handle = RT_NULL;
static rt_uint32_t s_download_size = 0;
static rt_bool_t s_download_ok = RT_FALSE;

/*--------------------WEAK FUNCTIONS ------------------------------------------ */
/**
 * @brief Fill firmware header information for update flow.
 *
 * @param fw_handle Firmware storage handle (unused).
 * @param part_len  Total partition length in bytes.
 * @param name      Target name (unused).
 * @param fw_info   Output firmware header structure.
 *
 * @return RT_TRUE when header is accepted, RT_FALSE otherwise.
 */
rt_bool_t qbt_fw_check(void *fw_handle, rt_uint32_t part_len, const char *name, fw_info_t *fw_info)
{
    if(s_download_ok == RT_FALSE)
    {
        return RT_FALSE;
    }

    fw_info->algo = 0;
    rt_strcpy((char *)fw_info->part_name, QBOOT_APP_PART_NAME);
    fw_info->raw_size = part_len;
    fw_info->pkg_size = part_len;
    return RT_TRUE;
}

/**
 * @brief Verify destination partition before release.
 *
 * @param handle   Target storage handle.
 * @param part_len Target length in bytes.
 * @param name     Target name.
 *
 * @return RT_TRUE when destination is valid, RT_FALSE otherwise.
 */
rt_bool_t qbt_dest_part_verify(void *handle, rt_uint32_t part_len, const char *name)
{
    return RT_TRUE;
}

/**
 * @brief Return source read position for firmware header.
 *
 * @return Read offset in bytes.
 */
rt_int32_t qboot_src_read_pos(void)
{
    return 0;
}

/**
 * @brief Close and reset download handle.
 *
 * @return None.
 */
static void qbt_update_mgr_download_cleanup(void)
{
    if (s_download_handle != RT_NULL)
    {
        qbt_target_close(s_download_handle);
        s_download_handle = RT_NULL;
    }
}

/**
 * @brief Close and reset download handle.
 *
 * @return None.
 */
static void qbt_update_mgr_set_download_ok(rt_bool_t ok)
{
    s_download_ok = ok;
    qbt_update_mgr_download_cleanup();
}

/**
 * @brief Open download target, clear sign, erase, and enter RECV state.
 *
 * @return RT_TRUE on success, RT_FALSE otherwise.
 */
rt_bool_t qbt_update_mgr_download_begin(void)
{
    s_download_desc = qbt_target_desc(QBOOT_TARGET_DOWNLOAD);
    if (s_download_desc == RT_NULL)
    {
        return RT_FALSE;
    }
    qbt_update_mgr_download_cleanup();
    if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &s_download_handle, &s_download_size, QBT_OPEN_WRITE | QBT_OPEN_CREATE | QBT_OPEN_TRUNC))
    {
        return RT_FALSE;
    }
    qbt_release_sign_clear(s_download_handle, s_download_desc->role_name, RT_NULL);
    if (qbt_erase_with_feed(s_download_handle, 0, s_download_size) != RT_EOK)
    {
        qbt_update_mgr_download_cleanup();
        return RT_FALSE;
    }

    qbt_update_mgr_set_total(s_download_size);
    qbt_update_mgr_on_start();
    return RT_TRUE;
}

/**
 * @brief Write a download data block and refresh activity timestamps.
 *
 * @param offset Byte offset to write.
 * @param data   Data buffer.
 * @param size   Bytes to write.
 *
 * @return RT_TRUE on success, RT_FALSE otherwise.
 */
rt_bool_t qbt_update_mgr_download_write(rt_uint32_t offset, rt_uint8_t *data, rt_uint32_t size)
{
    if (s_download_handle == RT_NULL)
    {
        return RT_FALSE;
    }
    if (_header_io_ops->write(s_download_handle, offset, data, size) != RT_EOK)
    {
        qbt_update_mgr_on_finish(RT_FALSE);
        qbt_update_mgr_download_cleanup();
        return RT_FALSE;
    }
    qbt_update_mgr_on_data();
    qbt_update_mgr_on_data_len(size);
    return RT_TRUE;
}

/**
 * @brief Try to recover app from DOWNLOAD/FACTORY if present.
 *
 * @return RT_TRUE when a valid backup exists, RT_FALSE otherwise.
 */
rt_bool_t qbt_update_mgr_try_recover(void)
{
    void *handle = RT_NULL;
    rt_uint32_t part_size = 0;
    rt_bool_t ok = RT_FALSE;

    qbt_update_mgr_set_download_ok(RT_TRUE);
    if (qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ))
    {
        if (qbt_fw_check(handle, part_size, QBOOT_DOWNLOAD_PART_NAME, RT_NULL))
        {
            ok = RT_TRUE;
        }
        qbt_target_close(handle);
    }

    if (!ok && qbt_target_open(QBOOT_TARGET_FACTORY, &handle, &part_size, QBT_OPEN_READ))
    {
        if (qbt_fw_check(handle, part_size, QBOOT_FACTORY_PART_NAME, RT_NULL))
        {
            ok = RT_TRUE;
        }
        qbt_target_close(handle);
    }

    return ok;
}
#endif /* QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER */

#ifdef QBT_UPDATE_MGR_PROGRESS_ENABLE
static rt_uint32_t s_dl_total_bytes = 0;
static rt_uint32_t s_dl_done_bytes = 0;
static rt_tick_t s_dl_last_print = 0;
static rt_uint32_t s_dl_last_percent = 0;

/**
 * @brief Reset progress counters and print throttling.
 *
 * @return None.
 */
void qbt_update_reset_progress(void)
{
    s_dl_done_bytes = 0;
    s_dl_last_print = 0;
    s_dl_last_percent = 0;
}

/**
 * @brief Set total bytes for progress percentage.
 *
 * @param total_bytes Total expected bytes.
 * @return None.
 */
void qbt_update_mgr_set_total(rt_uint32_t total_bytes)
{
    s_dl_total_bytes = total_bytes;
}

/**
 * @brief Accumulate received byte count.
 *
 * @param bytes Received bytes for this chunk.
 * @return None.
 */
void qbt_update_mgr_on_data_len(rt_uint32_t bytes)
{
    s_dl_done_bytes += bytes;
}
#else
/**
 * @brief Stub when progress is disabled.
 *
 * @param total_bytes Total expected bytes.
 * @return None.
 */
void qbt_update_mgr_set_total(rt_uint32_t total_bytes)
{
    RT_UNUSED(total_bytes);
}

/**
 * @brief Stub when progress is disabled.
 *
 * @param bytes Received bytes for this chunk.
 * @return None.
 */
void qbt_update_mgr_on_data_len(rt_uint32_t bytes)
{
    RT_UNUSED(bytes);
}
#endif /* QBT_UPDATE_MGR_PROGRESS_ENABLE */
/**
 * @brief Update state and refresh timestamps for timeout tracking.
 *
 * @param state New update state.
 * @return None.
 */
static void qbt_update_mgr_set_state(qbt_upd_state_t state)
{
    qbt_upd_state_t prev = s_state;
    s_state = state;
    if (state == QBT_UPD_STATE_WAIT)
    {
        s_wait_start = rt_tick_get();
    }
    else if (state == QBT_UPD_STATE_RECV)
    {
        s_download_last = rt_tick_get();
    }
    if (prev != state)
    {
        const rt_size_t name_count = sizeof(s_state_names) / sizeof(s_state_names[0]);
        const char *prev_name = (prev < name_count) ? s_state_names[prev] : "UNKNOWN";
        const char *curr_name = (state < name_count) ? s_state_names[state] : "UNKNOWN";
#ifdef QBT_UPDATE_MGR_PROGRESS_ENABLE
        if (state == QBT_UPD_STATE_RECV)
        {
            rt_kprintf("\n");
        }
        if (prev == QBT_UPD_STATE_RECV)
        {
            rt_kprintf("\n");
        }
#endif // QBT_UPDATE_MGR_PROGRESS_ENABLE
        LOG_I("Update state: %s -> %s", prev_name, curr_name);
    }
}

/**
 * @brief Reset recover probe gate for a new download session.
 *
 * @return None.
 */
static void qbt_update_mgr_reset_recover_probe(void)
{
    s_recover_probe_used = RT_FALSE;
}

/**
 * @brief Try recovery at most once in current download session.
 *
 * @return RT_TRUE on recover success, RT_FALSE otherwise.
 */
static rt_bool_t qbt_update_mgr_try_recover_once(void)
{
    if (s_recover_probe_used)
    {
        return RT_FALSE;
    }
    s_recover_probe_used = RT_TRUE;
    return s_ops->try_recover();
}

/**
 * @brief Get current update state.
 *
 * @return Current update state.
 */
qbt_upd_state_t qbt_update_mgr_get_state(void)
{
    return s_state;
}

/**
 * @brief Notify bootloader that it's safe to jump to app.
 *
 * @return None.
 */
static void qbt_update_mgr_ready(void)
{
    s_ready_flag = RT_TRUE;
    qbt_update_mgr_set_state(QBT_UPD_STATE_READY);
#ifdef QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER
    qbt_update_mgr_download_cleanup();
#endif
    s_ops->on_ready_to_app();
}

/**
 * @brief Handle update request event.
 *
 * @return None.
 */
void qbt_update_mgr_on_request(void)
{
    /* Ensure we never stick at REQ across reboots. */
    s_ops->set_reason(QBT_UPD_REASON_REQ);
    qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
}

/**
 * @brief Handle download start event.
 *
 * @return None.
 */
void qbt_update_mgr_on_start(void)
{
    /* New download session begins: allow one recover probe in this session. */
    qbt_update_mgr_reset_recover_probe();
    s_ops->enter_download();
    /* Enter active receive mode and start idle timeout tracking. */
    qbt_update_mgr_set_state(QBT_UPD_STATE_RECV);
#ifdef QBT_UPDATE_MGR_PROGRESS_ENABLE
    qbt_update_reset_progress();
#endif  /* QBT_UPDATE_MGR_PROGRESS_ENABLE */
}

/**
 * @brief Handle download data event.
 *
 * @return None.
 */
void qbt_update_mgr_on_data(void)
{
    if (s_state == QBT_UPD_STATE_RECV)
    {
        /* Refresh last-receive timestamp for idle timeout. */
        s_download_last = rt_tick_get();
    }
}

/**
 * @brief Handle download finish event.
 *
 * @param ok RT_TRUE when download completed successfully.
 * @return None.
 */
void qbt_update_mgr_on_finish(rt_bool_t ok)
{
    s_ops->leave_download();

#ifdef QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER
    qbt_update_mgr_set_download_ok(ok);
#endif /* QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER */
    if (ok)
    {
        /* Download completed: allow qboot to proceed with release/apply. */
        s_ops->set_reason(QBT_UPD_REASON_DONE);
        qbt_update_mgr_ready();
    }
    else
    {
        s_state = QBT_UPD_STATE_ERROR;
        s_ops->on_error(-1);
        /* Go back to wait window after failure. */
        qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
    }
}

/**
 * @brief Handle download abort event.
 *
 * @return None.
 */
void qbt_update_mgr_on_abort(void)
{
    s_ops->leave_download();
    /* Abort returns to wait state to allow retry. */
    qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
}

/**
 * @brief Poll update state machine and enforce timeouts.
 *
 * @param poll_delay_ms Delay between polls in milliseconds.
 * @return None.
 */
void qbt_update_mgr_poll(rt_uint32_t poll_delay_ms)
{
    s_wait_start = rt_tick_get();
    /* Blocking loop: caller waits here until ready flag is set. */
    while (!s_ready_flag)
    {
        rt_tick_t now = rt_tick_get();

        if (s_state == QBT_UPD_STATE_WAIT)
        {
            /* Wait window: no download yet. */
            if ((s_wait_ms > 0) && ((now - s_wait_start) >= s_wait_ms))
            {
                if (s_ops->is_app_valid())
                {
                    /* App valid: exit update flow and jump to app. */
                    s_ops->set_reason(QBT_UPD_REASON_NONE);
                    qbt_update_mgr_ready();
                }
                else
                {
                    if (qbt_update_mgr_try_recover_once())
                    {
                        s_ops->set_reason(QBT_UPD_REASON_NONE);
                        qbt_update_mgr_ready();
                    }
                    else
                    {
                        /* App invalid: keep waiting and restart window. */
                        s_wait_start = now;
                    }
                }
            }
        }
        else if (s_state == QBT_UPD_STATE_RECV)
        {
            /* Idle timeout during download: treat as interruption. */
            if ((s_idle_ms > 0) && ((now - s_download_last) >= s_idle_ms))
            {
                if (s_ops->is_app_valid())
                {
                    /* App valid: return to app if download stalled. */
                    s_ops->set_reason(QBT_UPD_REASON_NONE);
                    qbt_update_mgr_ready();
                }
                else
                {
                    if (qbt_update_mgr_try_recover_once())
                    {
                        s_ops->set_reason(QBT_UPD_REASON_NONE);
                        qbt_update_mgr_ready();
                    }
                    else
                    {
                        /* App invalid: stay in bootloader and wait for retry. */
                        qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
                    }
                }
            }
#ifdef QBT_UPDATE_MGR_PROGRESS_PRINT_ENABLE
            /*
             * Optional progress console output for download observation.
             * Synchronous rt_kprintf may affect real-time behavior if
             * RT_USING_THREADSAFE_PRINTF is not enabled or logs are not
             * handled by asynchronous ULog.
             */
            rt_tick_t print_gap = rt_tick_from_millisecond(500);
            if ((now - s_dl_last_print) >= print_gap)
            {
                if (s_dl_total_bytes > 0)
                {
                    rt_uint32_t percent = (s_dl_done_bytes * 100) / s_dl_total_bytes;
                    if (percent != s_dl_last_percent)
                    {
                        s_dl_last_percent = percent;
                        rt_kprintf("\rDownload: %3u%% (%u/%u bytes)", percent, s_dl_done_bytes, s_dl_total_bytes);
                    }
                }
                else
                {
                    rt_kprintf("\rDownload: %u bytes", s_dl_done_bytes);
                }
                s_dl_last_print = now;
            }
#endif /* QBT_UPDATE_MGR_PROGRESS_PRINT_ENABLE */
        }
        rt_thread_mdelay(poll_delay_ms);
    }
}

/**
 * @brief Notify update result after release/apply.
 *
 * @param success RT_TRUE when release/apply succeeded.
 * @return None.
 */
void qboot_notify_update_result(rt_bool_t success)
{
    if (success)
    {
        s_ops->set_reason(QBT_UPD_REASON_DONE);
    }
    else
    {
        s_ops->set_reason(QBT_UPD_REASON_IN_PROGRESS);
        qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
    }
}

/**
 * @brief Register bootloader callbacks and configure timeouts.
 *
 * @param ops     Bootloader callback table (must be non-NULL).
 * @param wait_ms Wait window timeout in milliseconds.
 * @param idle_ms Idle timeout in milliseconds.
 * @return None.
 */
void qbt_update_mgr_register(const qbt_update_ops_t *ops, rt_uint32_t wait_ms, rt_uint32_t idle_ms)
{
    s_ops = ops;
    s_ready_flag = RT_FALSE;
    qbt_update_mgr_reset_recover_probe();
    s_wait_ms = wait_ms;
    s_idle_ms = idle_ms;
    rt_uint32_t reason = s_ops->get_reason();
    LOG_I("Reason: %d", reason);
    switch (reason)
    {
    case QBT_UPD_REASON_IN_PROGRESS:
        qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
        break;
    case QBT_UPD_REASON_NONE:
    case QBT_UPD_REASON_DONE:
        if (s_ops->is_app_valid())
        {
            qbt_update_mgr_ready();
        }
        else
        {
            qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
        }
        break;
    case QBT_UPD_REASON_REQ:
        qbt_update_mgr_on_request();
        break;
    default:
        qbt_update_mgr_set_state(QBT_UPD_STATE_IDLE);
        break;
    }
}
