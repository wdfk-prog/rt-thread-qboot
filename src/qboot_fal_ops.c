/**
 * @file qboot_fal_ops.c
 * @brief FAL-backed package source/target operations for qboot.
 * @author wdfk-prog ()
 * @version 1.0
 * @date 2026-01-07
 * 
 * @copyright Copyright (c) 2026  
 * 
 * @note :
 * @par Change Log:
 * Date       Version Author      Description
 * 2026-01-07 1.0     wdfk-prog   first version
 */
#include <rtthread.h>
#include <fal.h>
#include <qboot.h>
#include <errno.h>
#include <rtdbg.h>

#ifdef QBOOT_PKG_SOURCE_FAL

#define DBG_TAG "qb_fal"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>
/**
 * @brief Open FAL partition by name.
 *
 * @param handle   Output partition handle.
 * @param path     Partition name.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
static rt_err_t qbt_fal_open(void **handle, const char *path)
{
    fal_partition_t part = (fal_partition_t)fal_partition_find(path);
    if (part == RT_NULL)
    {
        LOG_E("FAL open partition %s fail.", path);
        return -RT_ERROR;
    }
    *handle = part;
    return RT_EOK;
}

/**
 * @brief Close FAL partition handle (no-op).
 *
 * @param handle Partition handle (unused).
 *
 * @return RT_EOK always.
 */
static rt_err_t qbt_fal_close(void *handle)
{
    RT_UNUSED(handle);
    return RT_EOK;
}

/**
 * @brief Read from FAL partition.
 *
 * @param handle Partition handle.
 * @param off    Byte offset.
 * @param buf    Output buffer.
 * @param len    Bytes to read.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
static rt_err_t qbt_fal_read(void *handle, size_t off, void *buf, size_t len)
{
    if (fal_partition_read((fal_partition_t)handle, off, buf, len) < 0)
    {
        LOG_E("FAL read fail, off=%u len=%u", (unsigned int)off, (unsigned int)len);
        return -RT_ERROR;
    }
    return RT_EOK;
}

/**
 * @brief Erase FAL partition region.
 *
 * @param handle Partition handle.
 * @param off    Byte offset.
 * @param len    Bytes to erase.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
static rt_err_t qbt_fal_erase(void *handle, size_t off, size_t len)
{
    if (fal_partition_erase((fal_partition_t)handle, off, len) < 0)
    {
        LOG_E("FAL erase fail, off=%u len=%u", (unsigned int)off, (unsigned int)len);
        return -RT_ERROR;
    }
    return RT_EOK;
}

/**
 * @brief Write to FAL partition.
 *
 * @param handle Partition handle.
 * @param off    Byte offset.
 * @param buf    Input data buffer.
 * @param len    Bytes to write.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
static rt_err_t qbt_fal_write(void *handle, size_t off, const void *buf, size_t len)
{
    if (fal_partition_write((fal_partition_t)handle, off, buf, len) < 0)
    {
        LOG_E("FAL write fail, off=%u len=%u", (unsigned int)off, (unsigned int)len);
        return -RT_ERROR;
    }
    return RT_EOK;
}

/**
 * @brief Get FAL partition size.
 *
 * @param handle   Partition handle.
 * @param out_size Output size in bytes.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
static rt_err_t qbt_fal_size(void *handle, size_t *out_size)
{
    if (out_size == RT_NULL)
    {
        LOG_E("FAL size fail, out_size null");
        return -RT_ERROR;
    }
    *out_size = ((fal_partition_t)handle)->len;
    return RT_EOK;
}

/**
 * @brief Read release sign from FAL package partition.
 *
 * @param handle    Partition handle.
 * @param released  Output flag set to true if sign matches.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
static rt_err_t qbt_fal_sign_read(void *handle, bool *released, const fw_info_t *fw_info)
{
    fal_partition_t part = (fal_partition_t)handle;
    size_t pos = (((sizeof(fw_info_t) + fw_info->pkg_size) + (QBOOT_RELEASE_SIGN_ALIGN_SIZE - 1)) & ~(QBOOT_RELEASE_SIGN_ALIGN_SIZE - 1));
    u32 release_sign = 0;
    if (fal_partition_read(part, pos, (u8 *)&release_sign, sizeof(u32)) < 0)
    {
        LOG_E("FAL sign read fail at pos=%u.", (unsigned int)pos);
        return -RT_ERROR;
    }
    *released = (release_sign == QBOOT_RELEASE_SIGN_WORD);
    return RT_EOK;
}

static rt_err_t qbt_fal_sign_write(void *handle, const fw_info_t *fw_info)
{
    fal_partition_t part = (fal_partition_t)handle;
    u32 release_sign = QBOOT_RELEASE_SIGN_WORD;
    size_t pos = (((sizeof(fw_info_t) + fw_info->pkg_size) + (QBOOT_RELEASE_SIGN_ALIGN_SIZE - 1)) & ~(QBOOT_RELEASE_SIGN_ALIGN_SIZE - 1));
    if (fal_partition_write(part, pos, (u8 *)&release_sign, sizeof(u32)) < 0)
    {
        LOG_E("FAL sign write fail at pos=%u.", (unsigned int)pos);
        return -RT_ERROR;
    }
    return RT_EOK;
}

static const qboot_io_ops_t g_qboot_io_fal = {
    .open = qbt_fal_open,
    .close = qbt_fal_close,
    .read = qbt_fal_read,
    .erase = qbt_fal_erase,
    .write = qbt_fal_write,
    .size = qbt_fal_size,
};

static const qboot_header_parser_ops_t g_qboot_header_parser_fal = {
    .sign_read = qbt_fal_sign_read,
    .sign_write = qbt_fal_sign_write,
};

/**
 * @brief Register FAL-backed header parser and release target ops.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
int qboot_register_storage_ops(void)
{
    if (fal_init() <= 0)
    {
        LOG_E("Qboot initialize fal fail.");
        return -RT_ERROR;
    }

    int rst = qboot_register_header_io_ops(&g_qboot_io_fal);
    if (rst != RT_EOK)
    {
        LOG_E("Register header IO ops fail: %d", rst);
        return rst;
    }
    rst = qboot_register_header_parser_ops(&g_qboot_header_parser_fal);
    if (rst != RT_EOK)
    {
        LOG_E("Register header parser ops fail: %d", rst);
        return rst;
    }
    return RT_EOK;
}

#endif /* QBOOT_PKG_SOURCE_FAL */
