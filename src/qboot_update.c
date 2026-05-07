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
#ifndef QBOOT_SHELL_KEY_CHK_TMO
#define QBOOT_SHELL_KEY_CHK_TMO 1
#endif /* QBOOT_SHELL_KEY_CHK_TMO */
#define QBT_UPDATE_WAIT_MS_DEFAULT (QBOOT_SHELL_KEY_CHK_TMO * 1000)
/** @brief Default idle timeout during download before treating as interruption (ms). */
#define QBT_UPDATE_IDLE_MS_DEFAULT 30000u

/**
 * @brief Update manager runtime context.
 */
typedef struct
{
    /** @brief Bootloader-provided callbacks. */
    const qbt_update_ops_t *ops;
    /** @brief Current update state. */
    qbt_upd_state_t state;
    /** @brief Timestamp when WAIT state started (ticks). */
    rt_tick_t wait_start;
    /** @brief Timestamp of last received data (ticks). */
    rt_tick_t download_last;
    /** @brief WAIT state timeout in milliseconds. */
    rt_uint32_t wait_ms;
    /** @brief RECV idle timeout in milliseconds. */
    rt_uint32_t idle_ms;
    /** @brief One-shot flag to release qboot wait loop. */
    rt_bool_t ready_flag;
    /** @brief One-shot recover probe gate for current download session. */
    rt_bool_t recover_probe_used;
} qbt_update_mgr_ctx_t;

/** @brief Global manager context (single-instance). */
static qbt_update_mgr_ctx_t s_mgr = {
    .ops = RT_NULL,
    .state = QBT_UPD_STATE_IDLE,
    .wait_start = 0,
    .download_last = 0,
    .wait_ms = QBT_UPDATE_WAIT_MS_DEFAULT,
    .idle_ms = QBT_UPDATE_IDLE_MS_DEFAULT,
    .ready_flag = RT_FALSE,
    .recover_probe_used = RT_FALSE,
};
static const char *const s_state_names[] = {
    "IDLE",
    "WAIT_DOWNLOAD",
    "RECV_DOWNLOAD",
    "DONE",
    "ERROR",
    "READY",
};


/**
 * @brief Check whether all update-manager callbacks are registered.
 *
 * @return RT_TRUE when the callback table is complete, RT_FALSE otherwise.
 */
static rt_bool_t qbt_update_mgr_ops_ready(void)
{
    return (s_mgr.ops != RT_NULL &&
            s_mgr.ops->get_reason != RT_NULL &&
            s_mgr.ops->set_reason != RT_NULL &&
            s_mgr.ops->is_app_valid != RT_NULL &&
            s_mgr.ops->enter_download != RT_NULL &&
            s_mgr.ops->leave_download != RT_NULL &&
            s_mgr.ops->on_error != RT_NULL &&
            s_mgr.ops->on_ready_to_app != RT_NULL &&
            s_mgr.ops->try_recover != RT_NULL) ? RT_TRUE : RT_FALSE;
}

#ifdef QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER
/**
 * @brief Download helper runtime context.
 */
typedef struct
{
    /** @brief Download target descriptor. */
    const qboot_store_desc_t *desc;
    /** @brief Active download target handle. */
    void *handle;
    /**
     * @brief Download target capacity or backend-reported object size.
     *
     * This field is not a received-length accumulator. The protocol adapter
     * and package parser own complete-object length validation.
     */
    rt_uint32_t size;
    /** @brief Download validity flag used by fw_check helper. */
    rt_bool_t ok;
} qbt_update_download_ctx_t;

/** @brief Global download helper context (single-instance). */
static qbt_update_download_ctx_t s_dl = {0};

/*--------------------WEAK FUNCTIONS ------------------------------------------ */
#if !defined(QBOOT_CI_HOST_RBL_PACKAGE_TEST)
/**
 * @brief Fill firmware header information for the weak raw-helper flow.
 *
 * This weak fallback does not parse an RBL package header. It trusts the
 * storage backend's reported effective object length as both raw_size and
 * pkg_size. Backends or protocol adapters that need exact received-length
 * semantics must provide that effective length themselves, or replace this
 * weak function with a package-aware implementation.
 *
 * @param fw_handle Firmware storage handle (unused).
 * @param part_len  Effective source length reported by the storage backend.
 * @param name      Target name (unused).
 * @param fw_info   Output firmware header structure.
 *
 * @return RT_TRUE when header is accepted, RT_FALSE otherwise.
 */
rt_bool_t qbt_fw_check(void *fw_handle, rt_uint32_t part_len, const char *name, fw_info_t *fw_info)
{
    rt_bool_t allow = RT_FALSE;
    RT_UNUSED(fw_handle);

    if (name == RT_NULL)
    {
        LOG_D("fw_check reject: null part name.");
        return RT_FALSE;
    }

    if (rt_strcmp(name, QBOOT_DOWNLOAD_PART_NAME) == 0)
    {
        allow = s_dl.ok;
    }
    else if (rt_strcmp(name, QBOOT_FACTORY_PART_NAME) == 0)
    {
        allow = RT_TRUE;
    }
    else if (rt_strcmp(name, QBOOT_APP_PART_NAME) == 0)
    {
        LOG_D("fw_check app reject in helper mode.");
        return RT_FALSE;
    }
    else
    {
        LOG_D("fw_check reject unsupported part: %s", name);
        return RT_FALSE;
    }

    LOG_D("fw_check part=%s allow=%d size=%u", name, allow,
          (unsigned int)part_len);
    if (allow == RT_FALSE)
    {
        return RT_FALSE;
    }

    if (fw_info == RT_NULL)
    {
        return RT_TRUE;
    }

    fw_info->algo = 0;
    rt_strcpy((char *)fw_info->part_name, QBOOT_APP_PART_NAME);
    /* The helper does not track received length. Backends that support raw
     * helper recovery must report the effective object length through size().
     */
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
    RT_UNUSED(handle);
    RT_UNUSED(part_len);
    RT_UNUSED(name);

    return RT_TRUE;
}

/**
 * @brief Return source read position for raw download-helper payloads.
 *
 * @return Read offset in bytes.
 */
rt_int32_t qboot_src_read_pos(void)
{
    return 0;
}
#endif /* !defined(QBOOT_CI_HOST_RBL_PACKAGE_TEST) */

/**
 * @brief Close and reset download handle.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
static rt_err_t qbt_update_mgr_download_cleanup(void)
{
    rt_err_t result = RT_EOK;

    if (s_dl.handle != RT_NULL)
    {
        result = qbt_target_close(s_dl.handle);
        s_dl.handle = RT_NULL;
    }
    return result;
}

/**
 * @brief Set helper download validity and close the active handle.
 *
 * @param ok RT_TRUE when the download object is valid.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
static rt_err_t qbt_update_mgr_set_download_ok(rt_bool_t ok)
{
    rt_err_t result;

    s_dl.ok = ok;
    result = qbt_update_mgr_download_cleanup();
    if (result != RT_EOK)
    {
        s_dl.ok = RT_FALSE;
    }
    return result;
}

/**
 * @brief Open download target, clear sign, erase, and enter RECV state.
 *
 * @return RT_TRUE on success, RT_FALSE otherwise.
 */
rt_bool_t qbt_update_mgr_download_begin(void)
{
    s_dl.desc = qbt_target_desc(QBOOT_TARGET_DOWNLOAD);
    if (s_dl.desc == RT_NULL)
    {
        return RT_FALSE;
    }
    /* Start a new session with download validity cleared. No received-length
     * state is created here; the protocol adapter and firmware package
     * parser own complete-object length validation.
     */
    if (qbt_update_mgr_set_download_ok(RT_FALSE) != RT_EOK)
    {
        return RT_FALSE;
    }
    if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &s_dl.handle, &s_dl.size, QBT_OPEN_WRITE | QBT_OPEN_CREATE | QBT_OPEN_TRUNC))
    {
        return RT_FALSE;
    }
    qbt_release_sign_clear(s_dl.handle, s_dl.desc->role_name, RT_NULL);
    if (qbt_erase_with_feed(s_dl.handle, 0, s_dl.size) != RT_EOK)
    {
        (void)qbt_update_mgr_download_cleanup();
        return RT_FALSE;
    }

    qbt_update_mgr_set_total(s_dl.size);
    qbt_update_mgr_on_start();
    return RT_TRUE;
}

/**
 * @brief Write a download data block at a caller-selected offset.
 *
 * This helper is a storage-session primitive only. Packet ordering, retry,
 * duplicate, overlap, gap, declared total size, transport completeness, and
 * complete-object length validation belong to the protocol adapter and later
 * firmware parser/release path. This function only forwards the write to the
 * active DOWNLOAD backend.
 *
 * @param offset Byte offset selected by the caller.
 * @param data   Data buffer.
 * @param size   Bytes to write.
 *
 * @return RT_TRUE on success, RT_FALSE otherwise.
 */
rt_bool_t qbt_update_mgr_download_write(rt_uint32_t offset, rt_uint8_t *data, rt_uint32_t size)
{
    if (s_dl.handle == RT_NULL)
    {
        return RT_FALSE;
    }
    if (data == RT_NULL || size == 0u)
    {
        (void)qbt_update_mgr_download_finish(RT_FALSE);
        return RT_FALSE;
    }
    /* Do not enforce packet ordering or received-length policy here.
     * Protocol adapters own offset/completeness policy, and storage backends
     * own capacity checks.
     */
    if (_header_io_ops->write(s_dl.handle, offset, data, size) != RT_EOK)
    {
        (void)qbt_update_mgr_download_finish(RT_FALSE);
        return RT_FALSE;
    }

    qbt_update_mgr_on_data();
    qbt_update_mgr_on_data_len(size);
    return RT_TRUE;
}

/**
 * @brief Finish the active download helper session.
 *
 * The @p ok argument is a caller-owned completion verdict. Passing RT_TRUE
 * means the protocol adapter has already accepted the received object. This
 * helper does not perform an independent received-length check before making
 * the DOWNLOAD target available to the firmware check/release path.
 *
 * @param ok RT_TRUE when the caller has accepted the complete download body.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
rt_err_t qbt_update_mgr_download_finish(rt_bool_t ok)
{
    rt_err_t result;

    if (s_dl.handle == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (qbt_update_mgr_ops_ready() && s_mgr.state == QBT_UPD_STATE_RECV)
    {
        result = qbt_update_mgr_on_finish(ok);
        if (result != RT_EOK)
        {
            return result;
        }
        return (!ok || s_mgr.state == QBT_UPD_STATE_READY) ? RT_EOK : -RT_ERROR;
    }

    return qbt_update_mgr_set_download_ok(ok);
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

    if (qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ))
    {
        if (qbt_fw_check(handle, part_size, QBOOT_DOWNLOAD_PART_NAME, RT_NULL))
        {
            ok = RT_TRUE;
        }
        if (qbt_target_close(handle) != RT_EOK)
        {
            ok = RT_FALSE;
        }
    }

    if (!ok && qbt_target_open(QBOOT_TARGET_FACTORY, &handle, &part_size, QBT_OPEN_READ))
    {
        if (qbt_fw_check(handle, part_size, QBOOT_FACTORY_PART_NAME, RT_NULL))
        {
            ok = RT_TRUE;
        }
        if (qbt_target_close(handle) != RT_EOK)
        {
            ok = RT_FALSE;
        }
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
 */
void qbt_update_mgr_set_total(rt_uint32_t total_bytes)
{
    s_dl_total_bytes = total_bytes;
}

/**
 * @brief Accumulate received byte count.
 *
 * @param bytes Received bytes for this chunk.
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
 */
void qbt_update_mgr_set_total(rt_uint32_t total_bytes)
{
    RT_UNUSED(total_bytes);
}

/**
 * @brief Stub when progress is disabled.
 *
 * @param bytes Received bytes for this chunk.
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
 */
static void qbt_update_mgr_set_state(qbt_upd_state_t state)
{
    qbt_upd_state_t prev = s_mgr.state;
    s_mgr.state = state;
    if (state == QBT_UPD_STATE_WAIT)
    {
        s_mgr.wait_start = rt_tick_get();
    }
    else if (state == QBT_UPD_STATE_RECV)
    {
        s_mgr.download_last = rt_tick_get();
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
#endif /* QBT_UPDATE_MGR_PROGRESS_ENABLE */
        LOG_I("Update state: %s -> %s", prev_name, curr_name);
    }
}

/**
 * @brief Reset recover probe gate for a new download session.
 *
 */
static void qbt_update_mgr_reset_recover_probe(void)
{
    s_mgr.recover_probe_used = RT_FALSE;
}

/**
 * @brief Try recovery at most once in current download session.
 *
 * @return RT_TRUE on recover success, RT_FALSE otherwise.
 */
static rt_bool_t qbt_update_mgr_try_recover_once(void)
{
    if (s_mgr.recover_probe_used || !qbt_update_mgr_ops_ready())
    {
        return RT_FALSE;
    }
    s_mgr.recover_probe_used = RT_TRUE;
    return s_mgr.ops->try_recover();
}

/**
 * @brief Get current update state.
 *
 * @return Current update state.
 */
qbt_upd_state_t qbt_update_mgr_get_state(void)
{
    return s_mgr.state;
}

/**
 * @brief Notify bootloader that it's safe to jump to app.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
static rt_err_t qbt_update_mgr_ready(void)
{
    rt_err_t result = RT_EOK;

    if (!qbt_update_mgr_ops_ready())
    {
        return -RT_ERROR;
    }
#ifdef QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER
    result = qbt_update_mgr_download_cleanup();
    if (result != RT_EOK)
    {
        s_dl.ok = RT_FALSE;
        s_mgr.ready_flag = RT_FALSE;
        s_mgr.state = QBT_UPD_STATE_ERROR;
        s_mgr.ops->on_error(result);
        qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
        return result;
    }
#endif /* QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER */
    s_mgr.ready_flag = RT_TRUE;
    qbt_update_mgr_set_state(QBT_UPD_STATE_READY);
    s_mgr.ops->on_ready_to_app();
    return RT_EOK;
}

/**
 * @brief Handle update request event.
 *
 */
void qbt_update_mgr_on_request(void)
{
    if (!qbt_update_mgr_ops_ready() || s_mgr.state == QBT_UPD_STATE_RECV)
    {
        return;
    }
    /* Ensure we never stick at REQ across reboots. */
    s_mgr.ops->set_reason(QBT_UPD_REASON_REQ);
    qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
}

/**
 * @brief Handle download start event.
 *
 */
void qbt_update_mgr_on_start(void)
{
    if (!qbt_update_mgr_ops_ready() ||
        s_mgr.state == QBT_UPD_STATE_RECV ||
        s_mgr.state == QBT_UPD_STATE_READY)
    {
        return;
    }
    /* New download session begins: publish RECV before invoking callbacks so
     * reentrant start requests are rejected by the normal state guard.
     */
    qbt_update_mgr_reset_recover_probe();
    qbt_update_mgr_set_state(QBT_UPD_STATE_RECV);
    s_mgr.ops->enter_download();
#ifdef QBT_UPDATE_MGR_PROGRESS_ENABLE
    qbt_update_reset_progress();
#endif /* QBT_UPDATE_MGR_PROGRESS_ENABLE */
}

/**
 * @brief Handle download data event.
 *
 */
void qbt_update_mgr_on_data(void)
{
    if (s_mgr.state == QBT_UPD_STATE_RECV)
    {
        /* Refresh last-receive timestamp for idle timeout. */
        s_mgr.download_last = rt_tick_get();
    }
}

/**
 * @brief Handle download finish event.
 *
 * The state machine always runs the normal finish handling for a valid RECV
 * session. When helper cleanup fails, the session is treated as a failed
 * download, the error callback receives the cleanup error, and the return
 * value propagates the original backend error to the caller.
 *
 * @param ok RT_TRUE when download completed successfully.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
rt_err_t qbt_update_mgr_on_finish(rt_bool_t ok)
{
    rt_err_t result = RT_EOK;
    int error_code = -RT_ERROR;

    if (!qbt_update_mgr_ops_ready() || s_mgr.state != QBT_UPD_STATE_RECV)
    {
        return -RT_ERROR;
    }
    s_mgr.ops->leave_download();

#ifdef QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER
    result = qbt_update_mgr_set_download_ok(ok);
    if (result != RT_EOK)
    {
        ok = RT_FALSE;
        error_code = result;
    }
#endif /* QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER */
    if (ok)
    {
        /* Download completed: allow qboot to proceed with release/apply. */
        s_mgr.ops->set_reason(QBT_UPD_REASON_DONE);
        result = qbt_update_mgr_ready();
    }
    else
    {
        s_mgr.state = QBT_UPD_STATE_ERROR;
        s_mgr.ops->on_error(error_code);
        /* Go back to wait window after failure. */
        qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
    }
    return result;
}

/**
 * @brief Handle download abort event.
 *
 */
void qbt_update_mgr_on_abort(void)
{
    if (!qbt_update_mgr_ops_ready() || s_mgr.state != QBT_UPD_STATE_RECV)
    {
        return;
    }
    s_mgr.ops->leave_download();
#ifdef QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER
    rt_err_t result = qbt_update_mgr_set_download_ok(RT_FALSE);
    if (result != RT_EOK)
    {
        s_mgr.state = QBT_UPD_STATE_ERROR;
        s_mgr.ops->on_error(result);
    }
#endif /* QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER */
    /* Abort returns to wait state to allow retry. */
    qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
}

/**
 * @brief Poll update state machine and enforce timeouts.
 *
 * @param poll_delay_ms Delay between polls in milliseconds.
 */
void qbt_update_mgr_poll(rt_uint32_t poll_delay_ms)
{
    if (!qbt_update_mgr_ops_ready())
    {
        return;
    }
    s_mgr.wait_start = rt_tick_get();
    /* Blocking loop: caller waits here until ready flag is set. */
    while (!s_mgr.ready_flag)
    {
        rt_tick_t now = rt_tick_get();

        if (s_mgr.state == QBT_UPD_STATE_WAIT)
        {
            /* Wait window: no download yet. */
            if ((s_mgr.wait_ms > 0) && ((now - s_mgr.wait_start) >= s_mgr.wait_ms))
            {
                if (s_mgr.ops->is_app_valid())
                {
                    /* App valid: exit update flow and jump to app. */
                    s_mgr.ops->set_reason(QBT_UPD_REASON_NONE);
                    qbt_update_mgr_ready();
                }
                else
                {
                    if (qbt_update_mgr_try_recover_once())
                    {
                        s_mgr.ops->set_reason(QBT_UPD_REASON_NONE);
                        qbt_update_mgr_ready();
                    }
                    else
                    {
                        /* App invalid: keep waiting and restart window. */
                        s_mgr.wait_start = now;
                    }
                }
            }
        }
        else if (s_mgr.state == QBT_UPD_STATE_RECV)
        {
            /* Idle timeout during download: treat as interruption. */
            if ((s_mgr.idle_ms > 0) && ((now - s_mgr.download_last) >= s_mgr.idle_ms))
            {
                if (s_mgr.ops->is_app_valid())
                {
                    /* App valid: return to app if download stalled. */
                    s_mgr.ops->set_reason(QBT_UPD_REASON_NONE);
                    qbt_update_mgr_ready();
                }
                else
                {
                    if (qbt_update_mgr_try_recover_once())
                    {
                        s_mgr.ops->set_reason(QBT_UPD_REASON_NONE);
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
 */
void qboot_notify_update_result(rt_bool_t success)
{
    if (!qbt_update_mgr_ops_ready())
    {
        return;
    }
    if (success)
    {
        s_mgr.ops->set_reason(QBT_UPD_REASON_DONE);
    }
    else
    {
        s_mgr.ops->set_reason(QBT_UPD_REASON_IN_PROGRESS);
        qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
    }
}

/**
 * @brief Register bootloader callbacks and configure timeouts.
 *
 * @param ops     Bootloader callback table (must be non-NULL).
 * @param wait_ms Wait window timeout in milliseconds.
 * @param idle_ms Idle timeout in milliseconds.
 */
void qbt_update_mgr_register(const qbt_update_ops_t *ops, rt_uint32_t wait_ms, rt_uint32_t idle_ms)
{
    rt_uint32_t reason;

    s_mgr.ops = ops;
    s_mgr.ready_flag = RT_FALSE;
    qbt_update_mgr_reset_recover_probe();
    s_mgr.wait_ms = wait_ms;
    s_mgr.idle_ms = idle_ms;
    if (!qbt_update_mgr_ops_ready())
    {
        qbt_update_mgr_set_state(QBT_UPD_STATE_IDLE);
        return;
    }
    reason = s_mgr.ops->get_reason();
    LOG_I("Reason: %d", reason);
    switch (reason)
    {
    case QBT_UPD_REASON_IN_PROGRESS:
        qbt_update_mgr_set_state(QBT_UPD_STATE_WAIT);
        break;
    case QBT_UPD_REASON_NONE:
    case QBT_UPD_REASON_DONE:
        if (s_mgr.ops->is_app_valid())
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
