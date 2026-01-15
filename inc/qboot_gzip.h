/**
 * @file qboot_gzip.h
 * @brief Gzip decompression adapter interface for Qboot.
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-07-08     qiyongzhong       first version
 * 2020-09-18     qiyongzhong       add deinit function
 */
#ifndef __QBOOT_GZIP_H__
#define __QBOOT_GZIP_H__

#include <qboot.h>

#ifdef QBOOT_USING_GZIP

/** Size for the gzip remainder buffer used for alignment. */
#define GZIP_REMAIN_BUF_SIZE 32

/**
 * @brief Initialize gzip decompression state.
 */
void qbt_gzip_init(void);

/**
 * @brief Set gzip input buffer for the next inflate call.
 *
 * @param in_buf  Input buffer pointer.
 * @param in_size Input buffer length.
 */
void qbt_gzip_set_in(const rt_uint8_t *in_buf, rt_uint32_t in_size);

/**
 * @brief Decompress gzip data into the output buffer.
 *
 * @param out_buf      Output buffer pointer.
 * @param out_buf_size Output buffer capacity.
 *
 * @return Produced length on success, negative zlib error code on failure.
 */
int qbt_gzip_decompress(rt_uint8_t *out_buf, rt_uint32_t out_buf_size);

/**
 * @brief Deinitialize gzip decompression state.
 */
void qbt_gzip_deinit(void);

/**
 * @brief Register gzip algorithm ops into Qboot.
 *
 * @return RT_EOK on success, negative error code on failure.
 */
int qbt_algo_gzip_register(void);

#endif

#endif

