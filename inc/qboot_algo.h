/**
 * @file qboot_algo.h
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
 * 2026-01-15 1.0     wdfk-prog   split algorithm definitions
 */
#ifndef __QBOOT_ALGO_H__
#define __QBOOT_ALGO_H__

#include "qboot_stream.h"

#define QBOOT_ALGO_CRYPT_NONE  0
#define QBOOT_ALGO_CRYPT_XOR   1
#define QBOOT_ALGO_CRYPT_AES   2
#define QBOOT_ALGO_CRYPT_LAST  QBOOT_ALGO_CRYPT_AES
#define QBOOT_ALGO_CRYPT_COUNT (QBOOT_ALGO_CRYPT_LAST + 1)
#define QBOOT_ALGO_CRYPT_MASK  0x0F

#define QBOOT_ALGO_CMPRS_NONE       (0 << 8)
#define QBOOT_ALGO_CMPRS_GZIP       (1 << 8)
#define QBOOT_ALGO_CMPRS_QUICKLZ    (2 << 8)
#define QBOOT_ALGO_CMPRS_FASTLZ     (3 << 8)
#define QBOOT_ALGO_CMPRS_HPATCHLITE (4 << 8)
#define QBOOT_ALGO_CMPRS_MASK       (0x1F << 8)
#define QBOOT_ALGO_CMPRS_LAST       QBOOT_ALGO_CMPRS_HPATCHLITE
#define QBOOT_ALGO_CMPRS_COUNT      ((QBOOT_ALGO_CMPRS_LAST >> 8) + 1)

#define QBOOT_ALGO_TABLE_SIZE       (QBOOT_ALGO_CRYPT_COUNT + QBOOT_ALGO_CMPRS_COUNT)
#define QBOOT_ALGO_CRYPTO_INDEX(id) ((id))
#define QBOOT_ALGO_CMPRS_INDEX(id)  (QBOOT_ALGO_CRYPT_COUNT + ((id) >> 8))

typedef struct
{
    rt_err_t (*init)(void);                     /**< Optional initializer for decryption. */
    rt_err_t (*decrypt)(rt_uint8_t *out,        /**< Output buffer for plaintext. */
                        const rt_uint8_t *in,   /**< Input ciphertext buffer. */
                        rt_uint32_t len);       /**< Number of bytes to decrypt. */
    rt_err_t (*deinit)(void);                   /**< Optional cleanup called after decrypt stage. */
} qboot_crypto_ops_t;

/**
 * @brief Decompression operation table.
 */
typedef struct
{
    rt_err_t (*init)(void);                                   /**< Optional initializer for decompression. */
    rt_err_t (*decompress)(const qbt_stream_buf_t *buf,       /**< Input/output buffers. */
                           qbt_stream_status_t *out,          /**< [out] Stream results. */
                           const qbt_stream_ctx_t *ctx);      /**< Stream context for this call. */
    rt_err_t (*deinit)(void);                                 /**< Optional cleanup after decompression. */
} qboot_cmprs_ops_t;

typedef struct qboot_algo_ops
{
    rt_uint16_t algo_id;                                    /**< Algorithm identifier (compression/encryption). */
    const qboot_crypto_ops_t *crypt;                        /**< NULL when no decryption stage required. */
    const qboot_cmprs_ops_t *cmprs;                         /**< NULL when no decompression stage required. */
    rt_err_t (*apply)(void *src_handle,                     /**< Source handle to read from. */
                      void *dst_handle,                     /**< Destination handle to write to. */
                      fw_info_t *fw_info,                   /**< Firmware header context. */
                      rt_uint32_t patch_offset);            /**< Patch data offset within the package. */
} qboot_algo_ops_t;

rt_err_t qboot_algo_register(const qboot_algo_ops_t *ops, rt_uint16_t algo_id);
const qboot_algo_ops_t *qbt_fw_get_algo_ops(const fw_info_t *fw_info, rt_uint16_t *out_algo_id);
rt_bool_t qbt_fw_algo_init(const qboot_algo_ops_t *algo_ops);
void qbt_fw_algo_deinit(const qboot_algo_ops_t *algo_ops);
rt_err_t qbot_algo_startup(void);

#endif
