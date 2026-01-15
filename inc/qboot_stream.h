/**
 * @file qboot_stream.h
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
 * 2026-01-15 1.0     wdfk-prog   split stream interfaces
 */
#ifndef __QBOOT_STREAM_H__
#define __QBOOT_STREAM_H__

#include "qboot.h"

/**
 * @brief Purpose of a stream decompression pass.
 */
typedef enum
{
    QBT_STREAM_WRITE = 0,   /**< Produce output for writing to destination. */
    QBT_STREAM_CRC = 1,     /**< Produce output only for CRC calculation. */
} qbt_stream_purpose_t;

/**
 * @brief Stream input/output buffers for a decompress call.
 */
typedef struct
{
    rt_uint8_t *in;         /**< Input buffer pointer (in/out). */
    rt_uint32_t in_len;     /**< Input buffer length (in/out). */
    rt_uint8_t *out;        /**< Output buffer pointer. */
    size_t out_len; /**< Output buffer capacity. */
} qbt_stream_buf_t;

/**
 * @brief Stream call results.
 * @note:
 * - On RT_EOK, the implementation must make progress: consumed > 0 or produced > 0.
 * - When more input is required, return -RT_ENOSPC and set consumed = 0 and produced = 0.
 */
typedef struct
{
    size_t consumed;            /**< Bytes consumed from input. */
    size_t produced;            /**< Bytes produced to output. */
    rt_uint32_t remaining_in;   /**< Remaining input bytes after this call (0 when unknown). */
} qbt_stream_status_t;

/**
 * @brief Decompression stream context passed to each decompress call.
 */
typedef struct
{
    size_t total;                   /**< Total compressed length; 0 when unknown. */
    size_t consumed;                /**< Compressed bytes consumed before this call. */
    size_t raw_remaining;           /**< Raw output budget for this call (0 for unlimited). */
    qbt_stream_purpose_t purpose;   /**< Stream purpose (write/CRC). */
} qbt_stream_ctx_t;

#endif
