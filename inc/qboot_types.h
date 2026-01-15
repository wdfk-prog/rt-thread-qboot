/*
 * qboot_types.h
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-01-XX     wdfk-prog         split type definitions
 */

#ifndef __QBOOT_TYPES_H__
#define __QBOOT_TYPES_H__

#include <rtthread.h>

/**
 * @brief Firmware package header shared between boot and application.
 */
typedef struct
{
    rt_uint8_t type[4];         /**< Magic string, usually "RBL". */
    rt_uint16_t algo;           /**< Compress/encrypt algorithm bits. */
    rt_uint16_t algo2;          /**< Secondary algorithm flags (e.g. verify). */
    rt_uint32_t time_stamp;     /**< Build timestamp. */
    rt_uint8_t part_name[16];   /**< Target partition name. */
    rt_uint8_t fw_ver[24];      /**< Firmware version string. */
    rt_uint8_t prod_code[24];   /**< Product code string. */
    rt_uint32_t pkg_crc;        /**< CRC32 of the package body. */
    rt_uint32_t raw_crc;        /**< CRC32 of the raw firmware after decompress/decrypt. */
    rt_uint32_t raw_size;       /**< Raw firmware size in bytes. */
    rt_uint32_t pkg_size;       /**< Package size (excluding header). */
    rt_uint32_t hdr_crc;        /**< CRC32 of this header (excluding hdr_crc field). */
} fw_info_t;

/**
 * @brief Common IO operations shared by sources and targets. must be non-NULL
 */
typedef struct
{
    rt_err_t (*open)(void **handle, const char *path);                          /**< Prepare handle; path can be partition name or file path. */
    rt_err_t (*close)(void *handle);                                            /**< Close handle. */
    rt_err_t (*read)(void *handle, size_t off, void *buf, size_t len);          /**< Read data from offset. */
    rt_err_t (*erase)(void *handle, size_t off, size_t len);                    /**< Erase destination region before write. */
    rt_err_t (*write)(void *handle, size_t off, const void *buf, size_t len);   /**< Write data to destination. */
    rt_err_t (*size)(void *handle, size_t *out_size);                           /**< Query total size. */
} qboot_io_ops_t;

/**
 * @brief Header parser and package source operations. must be non-NULL
 */
typedef struct
{
    rt_err_t (*sign_read)(void *handle, rt_bool_t *released, const fw_info_t *fw_info); /**< Check release sign (FS for released tags); return -RT_ENOSYS if unsupported. */
    rt_err_t (*sign_write)(void *handle, const fw_info_t *fw_info);                     /**< Write release sign (FS for released tags); return -RT_ENOSYS if unsupported. */
} qboot_header_parser_ops_t;

/**
 * @brief User-provided hook to control when boot jumps to application.
 */
typedef struct
{
    void (*init)(void);                                 /**< Optional: initialize gate state. */
    rt_bool_t (*allow_jump)(void);                      /**< Return true to allow jump; can block/poll inside. */
    void (*notify_update_result)(rt_bool_t success);    /**< Optional: callback after release success/failure. */
} qboot_update_ops_t;

/**
 * @brief Register header parser/package source operations.
 *
 * @param ops Operation table; all callbacks must be non-NULL.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
int qboot_register_header_parser_ops(const qboot_header_parser_ops_t *ops);

/**
 * @brief Register header/package source IO operations.
 *
 * @param ops IO operation table; all callbacks must be non-NULL.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
int qboot_register_header_io_ops(const qboot_io_ops_t *ops);

/**
 * @brief Register update callbacks.
 *
 * @param ops Operation table; required fields must be non-NULL.
 *
 * @return RT_EOK on success.
 */
int qboot_register_update(const qboot_update_ops_t *ops);

/**
 * @brief Validate firmware header CRC and magic.
 *
 * @param fw_info Firmware header to check.
 *
 * @return true if valid, false otherwise.
 */
rt_bool_t qbt_fw_info_check(fw_info_t *fw_info);

/**
 * @brief Register package source/target operations (weak default).
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
int qboot_register_storage_ops(void);

#endif
