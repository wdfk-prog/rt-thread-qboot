/*
 * qboot.h
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-07-06     qiyongzhong       first version
 */
     
#ifndef __QBOOT_H__
#define __QBOOT_H__
     
#include <typedef.h>
#include <stdbool.h>
#include <stddef.h>
#include <rtconfig.h>
#include <fal_cfg.h>

//#define QBOOT_USING_PRODUCT_CODE
//#define QBOOT_USING_AES
//#define QBOOT_USING_GZIP
//#define QBOOT_USING_QUICKLZ
//#define QBOOT_USING_FASTLZ
//#define QBOOT_USING_SHELL
//#define QBOOT_USING_SYSWATCH
//#define QBOOT_USING_OTA_DOWNLOAD
//#define QBOOT_USING_PRODUCT_INFO
//#define QBOOT_USING_STATUS_LED
//#define QBOOT_USING_FACTORY_KEY
#define QBOOT_USING_APP_CHECK
#define QBOOT_RELEASE_SIGN_ALIGN_SIZE   8//can is 4, 8, 16
#define QBOOT_RELEASE_SIGN_WORD         0x5555AAAA

#ifdef QBOOT_USING_STATUS_LED
#ifndef QBOOT_STATUS_LED_PIN
#define QBOOT_STATUS_LED_PIN            0
#endif
#ifndef QBOOT_STATUS_LED_LEVEL
#define QBOOT_STATUS_LED_LEVEL          1 //led on level, 0--low, 1--high
#endif
#endif

#ifdef QBOOT_USING_FACTORY_KEY
#ifndef QBOOT_FACTORY_KEY_PIN
#define QBOOT_FACTORY_KEY_PIN           -1
#endif
#ifndef QBOOT_FACTORY_KEY_LEVEL
#define QBOOT_FACTORY_KEY_LEVEL         0 //key press level, 0--low, 1--high
#endif
#ifndef QBOOT_FACTORY_KEY_CHK_TMO
#define QBOOT_FACTORY_KEY_CHK_TMO       10
#endif
#endif

#ifdef QBOOT_USING_SHELL
#ifndef QBOOT_SHELL_KEY_CHK_TMO
#define QBOOT_SHELL_KEY_CHK_TMO         5
#endif
#endif

#ifdef  RT_APP_PART_ADDR
#define QBOOT_APP_ADDR                  RT_APP_PART_ADDR
#else
#define QBOOT_APP_ADDR                  0x08020000
#endif

#ifdef QBOOT_USING_PRODUCT_CODE
#ifndef QBOOT_PRODUCT_CODE
#define QBOOT_PRODUCT_CODE              "00010203040506070809"
#endif
#endif

#ifndef QBOOT_APP_PART_NAME
#define QBOOT_APP_PART_NAME             "app"
#endif

#ifndef QBOOT_DOWNLOAD_PART_NAME
#define QBOOT_DOWNLOAD_PART_NAME        "download"
#endif

#ifndef QBOOT_FACTORY_PART_NAME
#define QBOOT_FACTORY_PART_NAME         "factory"
#endif

#ifdef QBOOT_USING_AES
#ifndef QBOOT_AES_IV
#define QBOOT_AES_IV                    "0123456789ABCDEF"
#endif
#ifndef QBOOT_AES_KEY
#define QBOOT_AES_KEY                   "0123456789ABCDEF0123456789ABCDEF"
#endif
#endif

#ifdef QBOOT_USING_PRODUCT_INFO
#ifndef QBOOT_PRODUCT_NAME
#define QBOOT_PRODUCT_NAME              "Qboot test device"
#endif
#ifndef QBOOT_PRODUCT_VER
#define QBOOT_PRODUCT_VER               "v1.00 2020.07.27"
#endif
#ifndef QBOOT_PRODUCT_MCU
#define QBOOT_PRODUCT_MCU               "stm32l4r5zi"
#endif
#endif

#ifndef QBOOT_THREAD_STACK_SIZE
#define QBOOT_THREAD_STACK_SIZE         4096
#endif

#ifndef QBOOT_THREAD_PRIO
#define QBOOT_THREAD_PRIO               5
#endif

/**
 * @brief Firmware package header shared between boot and application.
 */
typedef struct
{
    u8  type[4];          /**< Magic string, usually "RBL". */
    u16 algo;             /**< Compress/encrypt algorithm bits. */
    u16 algo2;            /**< Secondary algorithm flags (e.g. verify). */
    u32 time_stamp;       /**< Build timestamp. */
    u8  part_name[16];    /**< Target partition name. */
    u8  fw_ver[24];       /**< Firmware version string. */
    u8  prod_code[24];    /**< Product code string. */
    u32 pkg_crc;          /**< CRC32 of the package body. */
    u32 raw_crc;          /**< CRC32 of the raw firmware after decompress/decrypt. */
    u32 raw_size;         /**< Raw firmware size in bytes. */
    u32 pkg_size;         /**< Package size (excluding header). */
    u32 hdr_crc;          /**< CRC32 of this header (excluding hdr_crc field). */
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
    rt_err_t (*sign_read)(void *handle, bool *released, const fw_info_t *fw_info); /**< Check release sign (FS for released tags); return -RT_ENOSYS if unsupported. */
    rt_err_t (*sign_write)(void *handle, const fw_info_t *fw_info);              /**< Write release sign (FS for released tags); return -RT_ENOSYS if unsupported. */
} qboot_header_parser_ops_t;

/**
 * @brief User-provided hook to control when boot jumps to application.
 */
typedef struct
{
    void (*init)(void);                                                         /**< Optional: initialize gate state. */
    bool (*allow_jump)(void);                                                   /**< Return true to allow jump; can block/poll inside. */
    void (*notify_update_result)(bool success);                                 /**< Optional: callback after release success/failure. */
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
bool qbt_fw_info_check(fw_info_t *fw_info);

/**
 * @brief Register package source/target operations (weak default).
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
int qboot_register_storage_ops(void);

#endif
