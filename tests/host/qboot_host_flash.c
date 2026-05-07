/**
 * @file qboot_host_flash.c
 * @brief Host storage simulator and qboot weak-hook overrides.
 */
#include "qboot_host_flash.h"
#include <qboot_update.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static rt_uint8_t g_host_flash[QBOOT_HOST_FLASH_SIZE]; /**< Simulated custom flash bytes. */
static int g_jump_count;                               /**< Host jump-spy count. */
static qboot_host_jump_trace_t g_jump_trace;           /**< Host Cortex-M jump preparation trace. */
static rt_bool_t g_malloc_fault_enabled;               /**< rt_malloc fault enable flag. */
static rt_uint32_t g_malloc_fault_remaining;           /**< Allocations to pass before fault. */
static rt_bool_t g_flash_physical_enabled;             /**< Physical flash rule enable flag. */
static rt_uint32_t g_flash_program_unit;                /**< Optional program-unit alignment in bytes. */

typedef struct
{
    rt_bool_t enabled;                /**< Rule enable flag. */
    qboot_host_fault_target_t target; /**< Target selector. */
    rt_uint32_t remaining_passes;     /**< Matching operations to pass before failing. */
} qboot_host_fault_rule_t;

static qboot_host_fault_rule_t g_faults[QBOOT_HOST_FAULT_COUNT]; /**< Fault rules. */

static qboot_host_fault_target_t qboot_host_id_to_fault_target(qbt_target_id_t id)
{
    switch (id)
    {
    case QBOOT_TARGET_APP:
        return QBOOT_HOST_FAULT_TARGET_APP;
    case QBOOT_TARGET_DOWNLOAD:
        return QBOOT_HOST_FAULT_TARGET_DOWNLOAD;
    case QBOOT_TARGET_FACTORY:
        return QBOOT_HOST_FAULT_TARGET_FACTORY;
#if defined(QBOOT_USING_HPATCHLITE) && defined(QBOOT_HPATCH_USE_STORAGE_SWAP)
    case QBOOT_TARGET_SWAP:
        return QBOOT_HOST_FAULT_TARGET_SWAP;
#endif /* defined(QBOOT_USING_HPATCHLITE) && defined(QBOOT_HPATCH_USE_STORAGE_SWAP) */
    default:
        return QBOOT_HOST_FAULT_TARGET_COUNT;
    }
}

static rt_uint32_t qboot_host_target_size(qbt_target_id_t id)
{
    const qboot_store_desc_t *desc = qbt_target_desc(id);
    if (desc != RT_NULL && desc->flash_len != 0u)
    {
        return desc->flash_len;
    }
    switch (id)
    {
    case QBOOT_TARGET_APP:
        return QBOOT_HOST_APP_LEN;
    case QBOOT_TARGET_DOWNLOAD:
        return QBOOT_HOST_DOWNLOAD_LEN;
    case QBOOT_TARGET_FACTORY:
        return QBOOT_HOST_FACTORY_LEN;
    default:
        return 0u;
    }
}

static rt_bool_t qboot_host_addr_to_offset(rt_uint32_t addr, rt_uint32_t len, rt_uint32_t *off)
{
    rt_uint32_t offset;
    if (off == RT_NULL || addr < QBOOT_HOST_FLASH_BASE)
    {
        return RT_FALSE;
    }
    offset = addr - QBOOT_HOST_FLASH_BASE;
    if (offset > QBOOT_HOST_FLASH_SIZE || len > (QBOOT_HOST_FLASH_SIZE - offset))
    {
        return RT_FALSE;
    }
    *off = offset;
    return RT_TRUE;
}

static qboot_host_fault_target_t qboot_host_addr_to_fault_target(rt_uint32_t addr, rt_uint32_t len)
{
    for (qbt_target_id_t id = 0; id < QBOOT_TARGET_COUNT; id++)
    {
        const qboot_store_desc_t *desc = qbt_target_desc(id);
        if (desc == RT_NULL || desc->flash_len == 0u || addr < desc->flash_addr)
        {
            continue;
        }
        rt_uint32_t rel = addr - desc->flash_addr;
        if (rel <= desc->flash_len && len <= (desc->flash_len - rel))
        {
            return qboot_host_id_to_fault_target(id);
        }
    }
    return QBOOT_HOST_FAULT_TARGET_ANY;
}

rt_bool_t qboot_host_fault_check_target(qboot_host_fault_op_t op, qboot_host_fault_target_t target)
{
    qboot_host_fault_rule_t *rule;
    if (op >= QBOOT_HOST_FAULT_COUNT)
    {
        return RT_FALSE;
    }
    rule = &g_faults[op];
    if (!rule->enabled || (rule->target != QBOOT_HOST_FAULT_TARGET_ANY && rule->target != target))
    {
        return RT_FALSE;
    }
    if (rule->remaining_passes > 0u)
    {
        rule->remaining_passes--;
        return RT_FALSE;
    }
    rule->enabled = RT_FALSE;
    return RT_TRUE;
}

#ifdef QBOOT_HOST_BACKEND_FS
static void qboot_host_fs_reset(void)
{
    mkdir("_ci", 0777);
    mkdir("_ci/host-sim", 0777);
    mkdir("_ci/host-sim/fs", 0777);
    unlink(QBOOT_APP_FILE_PATH);
    unlink(QBOOT_APP_SIGN_FILE_PATH);
    unlink(QBOOT_DOWNLOAD_FILE_PATH);
    unlink(QBOOT_DOWNLOAD_SIGN_FILE_PATH);
    unlink(QBOOT_FACTORY_FILE_PATH);
    unlink("_ci/host-sim/fs/download.tmp");
    unlink("_ci/host-sim/fs/download.sign.tmp");
}
#endif /* QBOOT_HOST_BACKEND_FS */

void qboot_host_flash_reset(void)
{
    rt_memset(g_host_flash, 0xFF, sizeof(g_host_flash));
#ifdef QBOOT_HOST_BACKEND_FAL
    qboot_host_fal_reset();
#endif /* QBOOT_HOST_BACKEND_FAL */
#ifdef QBOOT_HOST_BACKEND_FS
    qboot_host_fs_reset();
#endif /* QBOOT_HOST_BACKEND_FS */
    qboot_host_fault_reset();
    qboot_host_jump_reset();
    g_flash_physical_enabled = RT_FALSE;
    g_flash_program_unit = 0u;
}

void qboot_host_flash_physical_enable(rt_bool_t enable)
{
    g_flash_physical_enabled = enable;
}

void qboot_host_flash_program_unit_set(rt_uint32_t unit)
{
    g_flash_program_unit = unit;
}

void qboot_host_fault_reset(void)
{
    rt_memset(g_faults, 0, sizeof(g_faults));
    g_malloc_fault_enabled = RT_FALSE;
    g_malloc_fault_remaining = 0u;
}

void *qboot_host_rt_malloc(size_t size)
{
    if (g_malloc_fault_enabled)
    {
        if (g_malloc_fault_remaining > 0u)
        {
            g_malloc_fault_remaining--;
        }
        else
        {
            g_malloc_fault_enabled = RT_FALSE;
            return RT_NULL;
        }
    }
    return malloc(size);
}

void qboot_host_rt_free(void *ptr)
{
    free(ptr);
}

void qboot_host_rt_malloc_fail_after(rt_uint32_t after_hits)
{
    g_malloc_fault_enabled = RT_TRUE;
    g_malloc_fault_remaining = after_hits;
}

void qboot_host_fault_set(qboot_host_fault_op_t op,
                          qboot_host_fault_target_t target,
                          rt_uint32_t after_hits)
{
    if (op >= QBOOT_HOST_FAULT_COUNT || target >= QBOOT_HOST_FAULT_TARGET_COUNT)
    {
        return;
    }
    g_faults[op].enabled = RT_TRUE;
    g_faults[op].target = target;
    g_faults[op].remaining_passes = after_hits;
}

rt_bool_t qboot_host_fault_check_id(qboot_host_fault_op_t op, qbt_target_id_t id)
{
    return qboot_host_fault_check_target(op, qboot_host_id_to_fault_target(id));
}

rt_bool_t qboot_host_flash_load(qbt_target_id_t id, const rt_uint8_t *data, rt_uint32_t size)
{
    void *handle = RT_NULL;
    rt_uint32_t part_size = 0;
    rt_bool_t ok = RT_FALSE;

    if (data == RT_NULL || size > qboot_host_target_size(id) ||
        !qbt_target_open(id, &handle, &part_size, QBT_OPEN_WRITE | QBT_OPEN_CREATE | QBT_OPEN_TRUNC))
    {
        return RT_FALSE;
    }
    if (part_size < qboot_host_target_size(id))
    {
        part_size = qboot_host_target_size(id);
    }
    if (size <= part_size && qbt_erase_with_feed(handle, 0, part_size) == RT_EOK)
    {
#ifdef QBOOT_HOST_BACKEND_FS
        if (part_size > 0u)
        {
            rt_uint8_t erased = 0xFFu;
            if (_header_io_ops->write(handle, part_size - 1u, &erased, 1u) != RT_EOK)
            {
                qbt_target_close(handle);
                return RT_FALSE;
            }
        }
#endif /* QBOOT_HOST_BACKEND_FS */
        if (_header_io_ops->write(handle, 0, data, size) == RT_EOK)
        {
            ok = RT_TRUE;
        }
    }
    qbt_target_close(handle);
    return ok;
}

rt_bool_t qboot_host_flash_read_target(qbt_target_id_t id, rt_uint32_t off, rt_uint8_t *data, rt_uint32_t size)
{
    void *handle = RT_NULL;
    rt_uint32_t part_size = 0;
    rt_bool_t ok = RT_FALSE;
    if (data == RT_NULL || !qbt_target_open(id, &handle, &part_size, QBT_OPEN_READ))
    {
        return RT_FALSE;
    }
    if (off <= part_size && size <= (part_size - off) &&
        _header_io_ops->read(handle, off, data, size) == RT_EOK)
    {
        ok = RT_TRUE;
    }
    qbt_target_close(handle);
    return ok;
}

rt_bool_t qboot_host_receive_download(const rt_uint8_t *data, rt_uint32_t size, rt_uint32_t chunk_size)
{
    return qboot_host_receive_download_mode(data, size, chunk_size, "normal");
}

rt_bool_t qboot_host_receive_download_mode(const rt_uint8_t *data,
                                           rt_uint32_t size,
                                           rt_uint32_t chunk_size,
                                           const char *mode)
{
    rt_uint32_t pos = 0;
    rt_bool_t active = RT_FALSE;
    rt_bool_t ok = RT_FALSE;

    if (data == RT_NULL || size == 0u || chunk_size == 0u || mode == RT_NULL)
    {
        return RT_FALSE;
    }
    if (rt_strcmp(mode, "protocol-total-size-short") == 0 ||
        rt_strcmp(mode, "protocol-total-size-long") == 0 ||
        rt_strcmp(mode, "protocol-gap-rejected") == 0 ||
        rt_strcmp(mode, "protocol-overlap-rejected") == 0 ||
        rt_strcmp(mode, "protocol-duplicate-different-data") == 0)
    {
        /* Strict protocol-adapter cases deliberately reject the transfer
         * before qbt_update_mgr_download_finish(RT_TRUE). The download
         * helper remains a storage primitive and does not own these
         * transport-level length or ordering checks.
         */
        return RT_FALSE;
    }
#if defined(QBOOT_HOST_BACKEND_FS) && !defined(QBOOT_HOST_BACKEND_CUSTOM) && !defined(QBOOT_HOST_BACKEND_FAL)
    if (rt_strcmp(mode, "normal") == 0)
    {
        void *handle = RT_NULL;
        rt_uint32_t part_size = 0u;

        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size,
                             QBT_OPEN_WRITE | QBT_OPEN_CREATE | QBT_OPEN_TRUNC))
        {
            return RT_FALSE;
        }
        if (part_size < qboot_host_target_size(QBOOT_TARGET_DOWNLOAD))
        {
            part_size = qboot_host_target_size(QBOOT_TARGET_DOWNLOAD);
        }
        if (size <= part_size && qbt_erase_with_feed(handle, 0, part_size) == RT_EOK)
        {
            while (pos < size)
            {
                rt_uint32_t chunk = size - pos;

                if (chunk > chunk_size)
                {
                    chunk = chunk_size;
                }
                if (_header_io_ops->write(handle, pos, data + pos, chunk) != RT_EOK)
                {
                    break;
                }
                pos += chunk;
            }
            ok = (pos == size) ? RT_TRUE : RT_FALSE;
        }
        qbt_target_close(handle);
        return ok;
    }
#endif /* defined(QBOOT_HOST_BACKEND_FS) && !defined(QBOOT_HOST_BACKEND_CUSTOM) && !defined(QBOOT_HOST_BACKEND_FAL) */
    if (!qbt_update_mgr_download_begin())
    {
        return RT_FALSE;
    }
    active = RT_TRUE;
    while (pos < size)
    {
        rt_uint32_t chunk = size - pos;
        rt_uint32_t write_pos = pos;

        if (chunk > chunk_size)
        {
            chunk = chunk_size;
        }
        if (rt_strcmp(mode, "offset-repeat") == 0 && pos >= chunk_size)
        {
            write_pos = 0u;
        }
        else if (rt_strcmp(mode, "offset-gap") == 0 && pos >= chunk_size)
        {
            write_pos = pos + 1u;
        }
        else if (rt_strcmp(mode, "out-of-order") == 0 && pos == 0u && size > chunk)
        {
            write_pos = chunk;
        }
        else if (rt_strcmp(mode, "offset-overlap") == 0 && pos >= chunk_size)
        {
            write_pos = pos - 1u;
        }
        else if (rt_strcmp(mode, "same-offset-different-data") == 0 && pos >= chunk_size)
        {
            write_pos = 0u;
        }
        else if (rt_strcmp(mode, "duplicate-first") != 0 &&
                 rt_strcmp(mode, "normal") != 0 &&
                 rt_strcmp(mode, "offset-repeat") != 0 &&
                 rt_strcmp(mode, "offset-gap") != 0 &&
                 rt_strcmp(mode, "out-of-order") != 0 &&
                 rt_strcmp(mode, "offset-overlap") != 0 &&
                 rt_strcmp(mode, "same-offset-different-data") != 0 &&
                 rt_strcmp(mode, "protocol-resume-after-reset") != 0)
        {
            goto exit;
        }
        if (!qbt_update_mgr_download_write(write_pos, (rt_uint8_t *)(data + pos), chunk))
        {
            active = RT_FALSE;
            goto exit;
        }
        pos += chunk;
        if (rt_strcmp(mode, "duplicate-first") == 0 && pos == chunk)
        {
            if (!qbt_update_mgr_download_write(0u, (rt_uint8_t *)data, chunk))
            {
                active = RT_FALSE;
                goto exit;
            }
        }
    }
    ok = (qbt_update_mgr_download_finish(RT_TRUE) == RT_EOK) ? RT_TRUE : RT_FALSE;
    active = RT_FALSE;
exit:
    if (active)
    {
        (void)qbt_update_mgr_download_finish(RT_FALSE);
    }
    return ok;
}

rt_bool_t qboot_host_download_has_release_sign(void)
{
    void *handle = RT_NULL;
    rt_uint32_t part_size = 0;
    fw_info_t info = {0};
    rt_bool_t released = RT_FALSE;
    if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ))
    {
        return RT_FALSE;
    }
    if (qbt_fw_info_read(handle, part_size, &info, RT_FALSE))
    {
        released = qbt_release_sign_check(handle, QBOOT_DOWNLOAD_PART_NAME, &info);
    }
    qbt_target_close(handle);
    return released;
}

rt_bool_t qboot_host_download_corrupt_release_sign(void)
{
    void *handle = RT_NULL;
    rt_uint32_t part_size = 0;
    fw_info_t info = {0};
    rt_uint32_t corrupt_sign = 0xA5A55A5Au;
    rt_bool_t ok = RT_FALSE;

    if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_WRITE | QBT_OPEN_READ))
    {
        return RT_FALSE;
    }
    if (qbt_fw_info_read(handle, part_size, &info, RT_FALSE))
    {
#ifdef QBOOT_HOST_BACKEND_FS
        FILE *fp = fopen(QBOOT_DOWNLOAD_SIGN_FILE_PATH, "wb");
        if (fp != RT_NULL)
        {
            ok = (fwrite(&corrupt_sign, 1u, sizeof(corrupt_sign), fp) == sizeof(corrupt_sign)) ? RT_TRUE : RT_FALSE;
            fclose(fp);
        }
#else
        rt_uint32_t pos = (((qboot_src_read_pos() + info.pkg_size) +
                            (QBOOT_RELEASE_SIGN_ALIGN_SIZE - 1u)) &
                           ~(QBOOT_RELEASE_SIGN_ALIGN_SIZE - 1u));
        if (_header_io_ops->write(handle, pos, &corrupt_sign, sizeof(corrupt_sign)) == RT_EOK)
        {
            ok = RT_TRUE;
        }
#endif /* QBOOT_HOST_BACKEND_FS */
    }
    qbt_target_close(handle);
    return ok;
}

rt_bool_t qboot_host_corrupt_target_byte(qbt_target_id_t id, rt_uint32_t off)
{
    void *handle = RT_NULL;
    rt_uint32_t part_size = 0;
    rt_uint8_t value = 0xA5u;
    rt_bool_t ok = RT_FALSE;

    if (!qbt_target_open(id, &handle, &part_size, QBT_OPEN_WRITE))
    {
        return RT_FALSE;
    }
    if (off >= part_size)
    {
        qbt_target_close(handle);
        return RT_FALSE;
    }
    ok = (_header_io_ops->write(handle, off, &value, sizeof(value)) == RT_EOK) ? RT_TRUE : RT_FALSE;
    qbt_target_close(handle);
    return ok;
}

void qboot_host_jump_stub_reset(void)
{
    rt_memset(&g_jump_trace, 0, sizeof(g_jump_trace));
}

/**
 * @brief Simulate the host Cortex-M jump preparation sequence.
 *
 * @param stack_ptr Candidate APP initial stack pointer.
 * @param reset_vector Candidate APP reset vector.
 * @param vector_table Candidate APP vector table base.
 * @return RT_TRUE when the vector tuple is accepted and preparation is traced.
 */
rt_bool_t qboot_host_jump_stub_run(rt_uint32_t stack_ptr,
                                   rt_uint32_t reset_vector,
                                   rt_uint32_t vector_table)
{
    if (((stack_ptr & 0x2FF00000u) != 0x20000000u &&
         (stack_ptr & 0x2FF00000u) != 0x24000000u) ||
        ((reset_vector & 0xFF000000u) != 0x08000000u) ||
        ((reset_vector & 1u) == 0u) ||
        ((vector_table & 0x000000FFu) != 0u))
    {
        return RT_FALSE;
    }

    g_jump_trace.disable_irq_count++;
    g_jump_trace.deinit_count++;
    g_jump_trace.systick_clear_count++;
    g_jump_trace.nvic_clear_count += 128;
    g_jump_trace.set_control_count++;
    g_jump_trace.set_vtor_count++;
    g_jump_trace.set_msp_count++;
    g_jump_trace.barrier_count += 2;
    g_jump_trace.fpu_cleanup_count++;
    g_jump_trace.msp_value = stack_ptr;
    g_jump_trace.reset_vector = reset_vector;
    g_jump_trace.vector_table = vector_table;
    g_jump_trace.app_call_count++;
    g_jump_count++;
    return RT_TRUE;
}

const qboot_host_jump_trace_t *qboot_host_jump_stub_trace(void)
{
    return &g_jump_trace;
}

void qboot_host_jump_reset(void)
{
    g_jump_count = 0;
    qboot_host_jump_stub_reset();
}

int qboot_host_jump_count(void)
{
    return g_jump_count;
}

#ifdef QBOOT_HOST_BACKEND_CUSTOM
rt_err_t qbt_custom_flash_read(rt_uint32_t addr, void *buf, rt_uint32_t len)
{
    rt_uint32_t off = 0;
    qboot_host_fault_target_t target = qboot_host_addr_to_fault_target(addr, len);
    if (buf == RT_NULL || !qboot_host_addr_to_offset(addr, len, &off) ||
        qboot_host_fault_check_target(QBOOT_HOST_FAULT_READ, target))
    {
        return -RT_ERROR;
    }
    rt_memcpy(buf, g_host_flash + off, len);
    return RT_EOK;
}

rt_err_t qbt_custom_flash_write(rt_uint32_t addr, const void *buf, rt_uint32_t len)
{
    rt_uint32_t off = 0;
    const rt_uint8_t *bytes = (const rt_uint8_t *)buf;
    qboot_host_fault_target_t target = qboot_host_addr_to_fault_target(addr, len);

    if (!qboot_host_addr_to_offset(addr, len, &off))
    {
        return -RT_ERROR;
    }
    if (len == 0u)
    {
        return RT_EOK;
    }
    if (buf == RT_NULL || qboot_host_fault_check_target(QBOOT_HOST_FAULT_WRITE, target))
    {
        return -RT_ERROR;
    }
    if (g_flash_physical_enabled)
    {
        if (g_flash_program_unit != 0u)
        {
            rt_uint32_t rel = addr - QBOOT_HOST_FLASH_BASE;
            rt_uint32_t end_rel = rel + len - 1u;

            if ((rel % g_flash_program_unit) != 0u ||
                (len % g_flash_program_unit) != 0u ||
                (rel / QBOOT_HOST_FLASH_SECTOR_SIZE) !=
                    (end_rel / QBOOT_HOST_FLASH_SECTOR_SIZE))
            {
                return -RT_ERROR;
            }
        }
        for (rt_uint32_t i = 0; i < len; i++)
        {
            if ((bytes[i] & (rt_uint8_t)~g_host_flash[off + i]) != 0u)
            {
                return -RT_ERROR;
            }
        }
    }
    rt_memcpy(g_host_flash + off, buf, len);
    return RT_EOK;
}

rt_err_t qbt_custom_flash_erase(rt_uint32_t addr, rt_uint32_t len)
{
    rt_uint32_t off = 0;
    qboot_host_fault_target_t target = qboot_host_addr_to_fault_target(addr, len);
    if (!qboot_host_addr_to_offset(addr, len, &off) ||
        qboot_host_fault_check_target(QBOOT_HOST_FAULT_ERASE, target))
    {
        return -RT_ERROR;
    }
    if (g_flash_physical_enabled &&
        (((addr - QBOOT_HOST_FLASH_BASE) % QBOOT_HOST_FLASH_SECTOR_SIZE) != 0u ||
         (len % QBOOT_HOST_FLASH_SECTOR_SIZE) != 0u))
    {
        return -RT_ERROR;
    }
    rt_memset(g_host_flash + off, 0xFF, len);
    return RT_EOK;
}
#endif /* QBOOT_HOST_BACKEND_CUSTOM */

void qbt_jump_to_app(void)
{
    g_jump_count++;
}
