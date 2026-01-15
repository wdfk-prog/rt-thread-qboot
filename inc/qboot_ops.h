/**
 * @file qboot_ops.h
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
 * 2026-01-15 1.0     wdfk-prog   split type definitions
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
    rt_err_t (*open)(void **handle,               /**< [out] Output handle for the opened target. */
                     const char *path);           /**< Target identifier (partition name/path). */
    rt_err_t (*close)(void *handle);              /**< Handle to close. */
    rt_err_t (*read)(void *handle,                /**< Handle to read from. */
                     rt_uint32_t off,             /**< Byte offset to read. */
                     void *buf,                   /**< Destination buffer. */
                     rt_uint32_t len);            /**< Number of bytes to read. */
    rt_err_t (*erase)(void *handle,               /**< Handle to erase on. */
                      rt_uint32_t off,            /**< Byte offset to erase. */
                      rt_uint32_t len);           /**< Number of bytes to erase. */
    rt_err_t (*write)(void *handle,               /**< Handle to write to. */
                      rt_uint32_t off,            /**< Byte offset to write. */
                      const void *buf,            /**< Source buffer to write. */
                      rt_uint32_t len);           /**< Number of bytes to write. */
    rt_err_t (*size)(void *handle,                /**< Handle to query size for. */
                     rt_uint32_t *out_size);      /**< [out] Total size in bytes. */
} qboot_io_ops_t;

/**
 * @brief Header parser and package source operations. must be non-NULL
 */
typedef struct
{
    rt_err_t (*sign_read)(void *handle,                 /**< Handle to read from. */
                          rt_bool_t *released,          /**< [out] RT_TRUE when released sign is present. */
                          const fw_info_t *fw_info);    /**< Firmware header context. */
    rt_err_t (*sign_write)(void *handle,                /**< Handle to write to. */
                           const fw_info_t *fw_info);   /**< Firmware header context. */
} qboot_header_parser_ops_t;

/**
 * @brief User-provided hook to control when boot jumps to application.
 */
typedef struct
{
    void (*init)(void);                                 /**< Optional: initialize gate state. */
    rt_bool_t (*allow_jump)(void);                      /**< Return true to allow jump; can block/poll inside. */
    void (*notify_update_result)(rt_bool_t success);    /**< Result flag: RT_TRUE on success, RT_FALSE on failure. */
} qboot_update_ops_t;

rt_err_t qboot_register_storage_ops(void);
rt_err_t qboot_register_header_parser_ops(const qboot_header_parser_ops_t *ops);
rt_err_t qboot_register_header_io_ops(const qboot_io_ops_t *ops);
rt_err_t qboot_register_update(const qboot_update_ops_t *ops);
rt_bool_t qbt_fw_info_read(void *handle, rt_uint32_t part_len, fw_info_t *fw_info, rt_bool_t from_tail);
rt_bool_t qbt_fw_info_write(void *handle, rt_uint32_t part_len, fw_info_t *fw_info, rt_bool_t to_tail);
rt_bool_t qbt_release_sign_check(void *handle, const char *name, fw_info_t *fw_info);
rt_bool_t qbt_release_sign_write(void *handle, const char *name, fw_info_t *fw_info);
rt_bool_t qbt_target_open(const char *name, void **handle, rt_uint32_t *out_size);
void qbt_target_close(void *handle);

extern const qboot_io_ops_t *_header_io_ops;

#endif
