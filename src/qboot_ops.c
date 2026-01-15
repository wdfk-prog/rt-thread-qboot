/**
 * @file qboot_ops.c
 * @brief 
 * @author wdfk-prog ()
 * @version 1.0
 * @date 2026-01-15
 * 
 * @copyright Copyright (c) 2026  
 * 
 * @note :
 * @par Change Log:
 * Date       Version Author      Description
 * 2026-01-15 1.0     wdfk-prog   first version
 */
#include "qboot_ops.h"
 
#define DBG_TAG "qb_ops"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

const qboot_header_parser_ops_t *_header_parser_ops = RT_NULL;
const qboot_io_ops_t *_header_io_ops = RT_NULL;
const qboot_update_ops_t *_update_ops = RT_NULL;

/**
 * @brief Default jump decision; always allow.
 *
 * @return RT_TRUE when jump to application is allowed.
 */
static rt_bool_t qboot_default_allow_jump(void)
{
    return RT_TRUE;
}

const qboot_update_ops_t g_qboot_update_default = {
    RT_NULL,
    qboot_default_allow_jump,
    RT_NULL,
};

/**
 * @brief Weak hook to register storage ops; override in backend implementations.
 */
rt_weak rt_err_t qboot_register_storage_ops(void)
{
    return RT_EOK;
}

/**
 * @brief Register header parser/package source operations.
 *
 * @param ops       Operation table; required fields must be non-NULL.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
rt_err_t qboot_register_header_parser_ops(const qboot_header_parser_ops_t *ops)
{
    if ((ops == RT_NULL) || (ops->sign_read == RT_NULL) || (ops->sign_write == RT_NULL))
    {
        return -RT_ERROR;
    }
    _header_parser_ops = ops;
    return RT_EOK;
}

/**
 * @brief Register header/package source IO operations.
 *
 * @param ops IO operation table; required fields must be non-NULL.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
rt_err_t qboot_register_header_io_ops(const qboot_io_ops_t *ops)
{
    if ((ops == RT_NULL) || (ops->open == RT_NULL) || (ops->close == RT_NULL) || (ops->read == RT_NULL) || (ops->erase == RT_NULL) || (ops->write == RT_NULL) || (ops->size == RT_NULL))
    {
        return -RT_ERROR;
    }
    _header_io_ops = ops;
    return RT_EOK;
}

/**
 * @brief Register update callbacks.
 *
 * @param ops Operation table; required fields must be non-NULL.
 *
 * @return RT_EOK on success.
 */
rt_err_t qboot_register_update(const qboot_update_ops_t *ops)
{
    if (ops == RT_NULL || ops->allow_jump == RT_NULL)
    {
        return -RT_ERROR;
    }

    _update_ops = ops;
    return RT_EOK;
}

/**
 * @brief Read firmware header from head or tail of target.
 *
 * @param handle   Target handle from qbt_target_open.
 * @param part_len Target total size (required when @p from_tail is RT_TRUE).
 * @param fw_info  [out] Firmware header buffer.
 * @param from_tail RT_TRUE to read at tail, RT_FALSE at offset 0.
 *
 * @return RT_TRUE on success, RT_FALSE on failure.
 */

rt_bool_t qbt_fw_info_read(void *handle, rt_uint32_t part_len, fw_info_t *fw_info, rt_bool_t from_tail)
{
    if (from_tail && part_len < sizeof(fw_info_t))
    {
        LOG_E("Qboot read firmware info fail. part size %u < hdr size.", (unsigned int)part_len);
        return RT_FALSE;
    }
    rt_uint32_t addr = from_tail ? (part_len - sizeof(fw_info_t)) : 0;
    if (_header_io_ops->read(handle, addr, (rt_uint8_t *)fw_info, sizeof(fw_info_t)) != RT_EOK)
    {
        return (RT_FALSE);
    }
    return (RT_TRUE);
}

/**
 * @brief Write firmware header to head or tail of target.
 *
 * @param handle   Target handle from qbt_target_open.
 * @param part_len Target total size (required when @p to_tail is RT_TRUE).
 * @param fw_info  Firmware header to write.
 * @param to_tail  RT_TRUE to write at tail, RT_FALSE at offset 0.
 *
 * @return RT_TRUE on success, RT_FALSE on failure.
 */
rt_bool_t qbt_fw_info_write(void *handle, rt_uint32_t part_len, fw_info_t *fw_info, rt_bool_t to_tail)
{
    rt_uint32_t addr = to_tail ? (part_len - sizeof(fw_info_t)) : 0;
    if (_header_io_ops->write(handle, addr, (rt_uint8_t *)fw_info, sizeof(fw_info_t)) != RT_EOK)
    {
        return (RT_FALSE);
    }
    return (RT_TRUE);
}

/**
 * @brief Check release sign from target.
 *
 * @param handle Target handle.
 * @param name   Target name for log output.
 * @param fw_info Firmware header context.
 *
 * @return RT_TRUE when released sign is present, RT_FALSE otherwise.
 */
rt_bool_t qbt_release_sign_check(void *handle, const char *name, fw_info_t *fw_info)
{
    rt_bool_t released = RT_FALSE;
    rt_err_t rst = _header_parser_ops->sign_read(handle, &released, fw_info);
    if (rst != RT_EOK)
    {
        LOG_E("Qboot read release sign fail from %s partition. rst=%d", name, rst);
        return (RT_FALSE);
    }
    return released;
}

/**
 * @brief Write release sign to target.
 *
 * @param handle Target handle.
 * @param name   Target name for log output.
 * @param fw_info Firmware header context.
 *
 * @return RT_TRUE on success, RT_FALSE on failure.
 */
rt_bool_t qbt_release_sign_write(void *handle, const char *name, fw_info_t *fw_info)
{
    rt_err_t rst = _header_parser_ops->sign_write(handle, fw_info);
    if (rst != RT_EOK)
    {
        LOG_E("Qboot write release sign fail from %s partition. rst=%d", name, rst);
        return (RT_FALSE);
    }
    return RT_TRUE;
}

/**
 * @brief Open target by name and optionally query size.
 *
 * @param name      Target identifier (partition name/path).
 * @param handle    Output opaque handle.
 * @param out_size  Output total size; ignored if NULL.
 *
 * @return RT_TRUE on success, RT_FALSE otherwise.
 */
rt_bool_t qbt_target_open(const char *name, void **handle, rt_uint32_t *out_size)
{
    if (_header_io_ops->open(handle, name) != RT_EOK)
    {
        return RT_FALSE;
    }
    if (out_size != RT_NULL)
    {
        if (_header_io_ops->size(*handle, out_size) != RT_EOK)
        {
            _header_io_ops->close(*handle);
            return RT_FALSE;
        }
    }
    return RT_TRUE;
}

/**
 * @brief Close target handle opened by qbt_target_open.
 *
 * @param handle Target handle.
 */
void qbt_target_close(void *handle)
{
    if (handle != RT_NULL)
    {
        _header_io_ops->close(handle);
    }
}
