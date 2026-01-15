/**
 * @file qboot_fastlz.c
 * @brief 
 * @author wdfk-prog ()
 * @version 1.0
 * @date 2026-01-15
 * 
 * @copyright Copyright (c) 2026  
 * 
 * @note :
 * @par Change Log:
 * Date       Version   Author      Description
 * 2020-07-06 1.0       qiyongzhong first version
 * 2026-01-15 1.1       wdfk-prog   add one-shot decompress handler for blocks
 */
 
#include <qboot.h>

#ifdef QBOOT_USING_FASTLZ
#include <qboot_fastlz.h>
#include <fastlz.h>

#define DBG_TAG "qb_fastlz"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define QBOOT_FASTLZ_BLOCK_HDR_SIZE    4
rt_uint32_t qbt_fastlz_get_block_size(const rt_uint8_t *comp_datas)
{
    rt_uint32_t block_size = 0;
    for(int i = 0; i < QBOOT_FASTLZ_BLOCK_HDR_SIZE; i++)
    {
         block_size <<= 8;
         block_size += comp_datas[i];
    }
    return(block_size);
}

rt_uint32_t qbt_fastlz_decompress(rt_uint8_t *out_buf, rt_uint32_t out_buf_size, const rt_uint8_t *in_buf, rt_uint32_t block_size)
{
    return(fastlz_decompress(in_buf, block_size, out_buf, out_buf_size));
}

/**
 * @brief One-shot decompress handler for FastLZ blocks.
 *
 * @param buf Input/output buffers.
 * @param out [out] Stream results.
 * @param ctx Decompression stream context (unused for FastLZ).
 *
 * @return RT_EOK on success, -RT_ENOSPC when more input is needed, or -RT_ERROR on failure.
 */
static rt_err_t qbt_algo_fastlz_decompress(const qbt_stream_buf_t *buf, qbt_stream_status_t *out, const qbt_stream_ctx_t *ctx)
{
    RT_UNUSED(ctx);
    rt_uint32_t block_size;
    rt_uint32_t need_len;
    rt_uint32_t decomp_len;
    rt_uint32_t out_cap = (rt_uint32_t)buf->out_len;

    if (buf->in_len < QBOOT_FASTLZ_BLOCK_HDR_SIZE)
    {
        return -RT_ENOSPC;
    }

    block_size = qbt_fastlz_get_block_size(buf->in);
    need_len = block_size + QBOOT_FASTLZ_BLOCK_HDR_SIZE;
    if (block_size == 0)
    {
        return -RT_ERROR;
    }

    if (buf->in_len < need_len)
    {
        return -RT_ENOSPC;
    }

    decomp_len = qbt_fastlz_decompress(buf->out, out_cap, buf->in + QBOOT_FASTLZ_BLOCK_HDR_SIZE, block_size);
    if (decomp_len == 0)
    {
        LOG_E("Qboot fastlz decompress error. block=%u", block_size);
        return -RT_ERROR;
    }
    if (decomp_len > out_cap)
    {
        LOG_W("Qboot fastlz decompress warn. decomp_len=%u > out_len=%u", decomp_len, out_cap);
        decomp_len = out_cap;
    }

    out->consumed = need_len;
    out->produced = decomp_len;
    return RT_EOK;
}

/** FastLZ compression ops for Qboot. */
static const qboot_cmprs_ops_t qbt_algo_fastlz_cmprs_ops = {
    .init = RT_NULL,
    .decompress = qbt_algo_fastlz_decompress,
    .deinit = RT_NULL,
};

/** FastLZ algorithm ops descriptor. */
static const qboot_algo_ops_t qbt_algo_fastlz_ops = {
    .algo_name = "fastlz",
    .algo_id = QBOOT_ALGO_CMPRS_FASTLZ,
    .crypt = RT_NULL,
    .cmprs = &qbt_algo_fastlz_cmprs_ops,
    .apply = RT_NULL,
};

/**
 * @brief Register FastLZ algorithm ops into Qboot.
 *
 * @return RT_EOK on success, negative error code on failure.
 */
rt_err_t qbt_algo_fastlz_register(void)
{
    return qboot_algo_register(&qbt_algo_fastlz_ops, QBOOT_ALGO_CMPRS_FASTLZ);
}

#endif

