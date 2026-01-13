/**
 * @file qboot_quicklz.c
 * @brief QuickLZ decompression adapter for Qboot.
 * @author wdfk-prog ()
 * @version 1.0
 * @date 2026-01-13
 *
 * @copyright Copyright (c) 2026
 *
 * @note
 * - Provides one-shot block decompression for QuickLZ packaged data.
 * @par Change Log:
 * Date       Version Author      Description
 * 2020-07-06     qiyongzhong     first version
 * 2026-01-13 1.1 wdfk-prog       add one-shot decompress handler for QUICKLZ blocks
 */
#include <qboot_quicklz.h>

#ifdef QBOOT_USING_QUICKLZ

#include <quicklz.h>
#include <string.h>

#define DBG_TAG "qb_quicklz"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/** QuickLZ decompression state for streaming API. */
static qlz_state_decompress qbt_quicklz_state;

/**
 * @brief Initialize QuickLZ decompression state.
 */
void qbt_quicklz_state_init(void)
{
    memset((u8 *)&qbt_quicklz_state, 0, sizeof(qbt_quicklz_state));
}

/**
 * @brief Parse the compressed block size from the QuickLZ block header.
 *
 * @param comp_datas Input buffer containing the block header.
 *
 * @return Block size parsed from the header (big-endian).
 */
u32 qbt_quicklz_get_block_size(const u8 *comp_datas)
{
    u32 block_size = 0;
    for (int i = 0; i < QBOOT_QUICKLZ_BLOCK_HDR_SIZE; i++)
    {
        block_size <<= 8;
        block_size += comp_datas[i];
    }
    return (block_size);
}

/**
 * @brief Decompress a QuickLZ block payload (no header).
 *
 * @param out_buf Output buffer for decompressed data.
 * @param in_buf  Input buffer pointing to the compressed payload.
 *
 * @return Decompressed length.
 */
u32 qbt_quicklz_decompress(u8 *out_buf, const u8 *in_buf)
{
    return (qlz_decompress((char *)in_buf, out_buf, &qbt_quicklz_state));
}

/**
 * @brief One-shot decompress handler for QuickLZ blocks.
 *
 * @param in         Input buffer containing a whole QuickLZ block (with header).
 * @param in_len     Length of input buffer.
 * @param out        Output buffer for decompressed data.
 * @param out_len    Capacity of output buffer.
 * @param consumed   [out] Bytes consumed from input.
 * @param produced   [out] Bytes produced to output.
 * @param finished   Output flag set true when the full block is consumed.
 *
 * @return RT_EOK on success, -RT_ENOSPC when more input is needed, or -RT_ERROR on failure.
 */
static rt_err_t qbt_algo_quicklz_decompress(const u8 *in, size_t in_len, u8 *out, size_t out_len,
                                            size_t *consumed, size_t *produced, bool *finished)
{
    u32 block_size;
    u32 need_len;
    int decomp_len;

    if ((in == RT_NULL) || (out == RT_NULL) || (finished == RT_NULL) || (consumed == RT_NULL) || (produced == RT_NULL))
    {
        return -RT_ERROR;
    }

    if (in_len < QBOOT_QUICKLZ_BLOCK_HDR_SIZE)
    {
        *consumed = 0;
        *produced = 0;
        *finished = false;
        return -RT_ENOSPC;
    }

    block_size = qbt_quicklz_get_block_size(in);
    need_len = block_size + QBOOT_QUICKLZ_BLOCK_HDR_SIZE;
    if (block_size == 0)
    {
        return -RT_ERROR;
    }
    else if (need_len > (int)out_len)
    {
        LOG_W("Qboot quicklz decompress warn. need_len=%u > out_len=%u", need_len, (u32)out_len);
        decomp_len = (int)out_len;
    }

    if (in_len < need_len)
    {
        *consumed = 0;
        *produced = 0;
        *finished = false;
        return -RT_ENOSPC;
    }

    decomp_len = (int)qbt_quicklz_decompress(out, in + QBOOT_QUICKLZ_BLOCK_HDR_SIZE);
    if (decomp_len <= 0)
    {
        LOG_E("Qboot quicklz decompress error. decomp_len=%d <= 0", decomp_len);
        return -RT_ERROR;
    }
    else if (decomp_len > (int)out_len)
    {
        LOG_W("Qboot quicklz decompress warn. decomp_len=%d > out_len=%u", decomp_len, (u32)out_len);
        decomp_len = (int)out_len;
    }

    *consumed = need_len;
    *produced = (size_t)decomp_len;
    *finished = true;
    return RT_EOK;
}

/**
 * @brief Initialize QuickLZ algorithm state for decompression.
 *
 * @return RT_EOK on success.
 */
static rt_err_t qbt_algo_quicklz_init(void)
{
    qbt_quicklz_state_init();
    return RT_EOK;
}

/** QuickLZ compression ops for Qboot. */
static const qboot_cmprs_ops_t qbt_algo_quicklz_cmprs_ops = {
    .init = qbt_algo_quicklz_init,
    .decompress = qbt_algo_quicklz_decompress,
    .deinit = RT_NULL,
};

/** QuickLZ algorithm ops descriptor. */
static const qboot_algo_ops_t qbt_algo_quicklz_ops = {
    .algo_id = QBOOT_ALGO_CMPRS_QUICKLZ,
    .crypt = RT_NULL,
    .cmprs = &qbt_algo_quicklz_cmprs_ops,
    .apply = RT_NULL,
};

/**
 * @brief Register QuickLZ algorithm ops into Qboot.
 *
 * @return RT_EOK on success, negative error code on failure.
 */
int qbt_algo_quicklz_register(void)
{
    return qboot_algo_register(&qbt_algo_quicklz_ops, QBOOT_ALGO_CMPRS_QUICKLZ);
}

#endif
