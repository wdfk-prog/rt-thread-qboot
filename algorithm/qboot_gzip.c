/**
 * @file qboot_gzip.c
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
 * 2020-07-08  1.0      qiyongzhong first version
 * 2020-09-18  1.1      qiyongzhong add deinit function
 * 2026-01-15  1.2      wdfk-prog   add one-shot decompress handler for blocks
 */

#include <qboot.h>

#ifdef QBOOT_USING_GZIP

#include <zlib.h>
#include <string.h>

#define DBG_TAG "qb_gzip"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/** Size for the gzip remainder buffer used for alignment. */
#define GZIP_REMAIN_BUF_SIZE 32

/** Gzip inflate stream context. */
static z_stream qbt_strm;
/** Remainder buffer for alignment handling. */
static rt_uint8_t qbt_gzip_remain_buf[GZIP_REMAIN_BUF_SIZE];
/** Length of valid data in @ref qbt_gzip_remain_buf. */
static rt_uint32_t qbt_gzip_remain_len = 0;
/** Whether zlib has reached the gzip stream end marker. */
static rt_bool_t qbt_gzip_stream_end = RT_FALSE;

/**
 * @brief Initialize gzip decompression state.
 */
static void qbt_gzip_init(void)
{
    memset((rt_uint8_t *)&qbt_strm, 0, sizeof(qbt_strm));
    inflateInit2(&qbt_strm, 47);
    qbt_gzip_remain_len = 0;
    qbt_gzip_stream_end = RT_FALSE;
}

/**
 * @brief Set gzip input buffer for the next inflate call.
 *
 * @param in_buf  Input buffer pointer.
 * @param in_size Input buffer length.
 */
static void qbt_gzip_set_in(const rt_uint8_t *in_buf, rt_uint32_t in_size)
{
    qbt_strm.next_in = (void *)in_buf;
    qbt_strm.avail_in = in_size;
}

/**
 * @brief Decompress gzip data into the output buffer.
 *
 * @param out_buf      Output buffer pointer.
 * @param out_buf_size Output buffer capacity.
 * @param produced     Output length produced by zlib.
 *
 * @return RT_EOK on success, or -RT_ERROR on zlib failure.
 */
static rt_err_t qbt_gzip_decompress(rt_uint8_t *out_buf,
                                    rt_uint32_t out_buf_size,
                                    rt_uint32_t *produced)
{
    int ret;

    qbt_strm.next_out = out_buf;
    qbt_strm.avail_out = out_buf_size;

    ret = inflate(&qbt_strm, Z_NO_FLUSH);
    switch (ret)
    {
    case Z_STREAM_END:
        qbt_gzip_stream_end = RT_TRUE;
        break;
    case Z_OK:
        break;
    case Z_NEED_DICT:
        ret = Z_DATA_ERROR;
        /* fall through */
    case Z_DATA_ERROR:
    case Z_MEM_ERROR:
    case Z_BUF_ERROR:
        inflateEnd(&qbt_strm);
        return -RT_ERROR;
    default:
        inflateEnd(&qbt_strm);
        return -RT_ERROR;
    }

    *produced = out_buf_size - qbt_strm.avail_out;
    return RT_EOK;
}

/**
 * @brief Gzip decompress handler with alignment and padding support.
 *
 * This function follows the legacy behavior: it outputs data in multiples of
 * GZIP_REMAIN_BUF_SIZE and keeps a remainder buffer for the next call. When the
 * stream ends, it can optionally pad the remainder (for write) or flush it as-is
 * (for CRC) based on the stream context.
 *
 * @par Flow Steps (1-4)
 * 1) Run inflate and require zlib to report end-of-stream.
 * 2) Align output to 32 bytes, manage remainder buffer, optional padding.
 * 3) Fill result/flags and validate progress/error conditions.
 *
 * @param buf Input/output buffers.
 * @param out [out] Stream results.
 * @param ctx Decompression stream context.
 *
 * @return RT_EOK on success, -RT_ENOSPC when more input is needed, or -RT_ERROR on failure.
 */
static rt_err_t qbt_algo_gzip_decompress(const qbt_stream_buf_t *buf, qbt_stream_status_t *out, const qbt_stream_ctx_t *ctx)
{
    rt_bool_t pad_output = RT_FALSE; /**< Whether to pad tail output. */
    rt_bool_t last_chunk = RT_FALSE; /**< Whether current input reaches stream end. */

    pad_output = (ctx->purpose == QBT_STREAM_WRITE); /**< Enable padding for write path. */
    if (ctx->total > 0)                              /**< Determine if this is the last chunk. */
    {
        rt_uint32_t available_end = ctx->consumed + buf->in_len; /**< End offset of available input. */
        last_chunk = (available_end >= ctx->total);              /**< Mark final chunk when reaching total. */
    }

    if (qbt_gzip_remain_len > buf->out_len) /**< Ensure output buffer fits remainder. */
    {
        LOG_E("Qboot gzip decompress error. remain_len=%u > out_len=%u", (rt_uint32_t)qbt_gzip_remain_len, (rt_uint32_t)buf->out_len);
        return -RT_ERROR;
    }

    /* (1) Inflate or empty-input flush. */
    /* Prefill output with any remainder from the previous call. */
    if (qbt_gzip_remain_len > 0)
    {
        rt_memcpy(buf->out, qbt_gzip_remain_buf, qbt_gzip_remain_len);
    }
    rt_uint32_t decomp_len = 0;             /**< Bytes decompressed by inflate. */
    rt_uint32_t consumed_in = 0;            /**< Bytes consumed from this call input. */
    rt_bool_t is_end = qbt_gzip_stream_end; /**< Track gzip stream end. */
    if (buf->in_len == 0)                   /**< No input bytes provided. */
    {
        if (!qbt_gzip_stream_end) /**< Trailer was not completed. */
        {
            return last_chunk ? -RT_ERROR : -RT_ENOSPC;
        }
        if (qbt_gzip_remain_len == 0) /**< Nothing left to flush. */
        {
            return -RT_ENOSPC;
        }
        is_end = RT_TRUE; /**< Flush buffered tail bytes. */
    }
    else /**< Input data available. */
    {
        qbt_gzip_set_in(buf->in, (rt_uint32_t)buf->in_len); /**< Feed input to zlib stream. */
        if (qbt_gzip_decompress(buf->out + qbt_gzip_remain_len, buf->out_len - qbt_gzip_remain_len, &decomp_len) != RT_EOK)
        {
            LOG_E("Qboot gzip decompress error.");
            return -RT_ERROR;
        }
        consumed_in = (qbt_strm.avail_in <= buf->in_len) ? (buf->in_len - qbt_strm.avail_in) : 0;
        is_end = qbt_gzip_stream_end; /**< Use zlib stream end only. */
    }
    /* (2) Align output, manage remainder, optional padding. */
    rt_uint32_t total_len = decomp_len + qbt_gzip_remain_len;  /**< Total output including remainder. */
    rt_uint32_t new_remain = total_len % GZIP_REMAIN_BUF_SIZE; /**< Bytes not aligned to 32. */
    rt_uint32_t out_len_aligned = total_len - new_remain;      /**< Aligned output length. */

    if (new_remain > 0) /**< Store new remainder for next call. */
    {
        rt_memcpy(qbt_gzip_remain_buf, buf->out + out_len_aligned, new_remain); /**< Save remainder bytes. */
    }

    rt_uint32_t out_len_final = out_len_aligned; /**< Final output size for this call. */
    if (is_end)                                  /**< Tail handling on stream end. */
    {
        if (new_remain > 0) /**< There is leftover data. */
        {
            rt_uint32_t padded_len;

            if (pad_output && last_chunk &&
                qbt_u32_add_checked(out_len_aligned, GZIP_REMAIN_BUF_SIZE, &padded_len) &&
                (padded_len <= buf->out_len)) /**< Pad tail if requested. */
            {
                rt_memcpy(buf->out + out_len_aligned, qbt_gzip_remain_buf, new_remain);                      /**< Copy tail data. */
                rt_memset(buf->out + out_len_aligned + new_remain, 0xFF, GZIP_REMAIN_BUF_SIZE - new_remain); /**< Fill padding. */
                out_len_final = padded_len;                                                                  /**< Account for padded block. */
            }
            else if (!qbt_u32_add_checked(out_len_final, new_remain, &out_len_final)) /**< No padding required. */
            {
                LOG_E("Qboot gzip decompress error. output length overflow.");
                return -RT_ERROR;
            }
            new_remain = 0; /**< Remainder consumed. */
        }
    }

    if (out_len_final > buf->out_len)
    {
        LOG_E("Qboot gzip decompress error. produced=%u > out_len=%u", out_len_final, (rt_uint32_t)buf->out_len);
        return -RT_ERROR;
    }
    if ((ctx->raw_remaining > 0) && (out_len_final > ctx->raw_remaining))
    {
        LOG_E("Qboot gzip decompress error. produced=%u > raw_remaining=%u", out_len_final, ctx->raw_remaining);
        return -RT_ERROR;
    }

    qbt_gzip_remain_len = new_remain;
    out->consumed = consumed_in;
    out->produced = out_len_final;

    if (!is_end && last_chunk && (out->consumed == buf->in_len) && (ctx->raw_remaining > 0) && (out->produced == ctx->raw_remaining))
    {
        LOG_E("Qboot gzip decompress error. stream end marker was not reached.");
        return -RT_ERROR;
    }

    if ((out->consumed == 0) && (out->produced == 0) && (!is_end)) /**< No progress and not done. */
    {
        return -RT_ENOSPC; /**< Request more input. */
    }
    if ((out->consumed == 0) && (out->produced == 0)) /**< Finished without flushed data. */
    {
        LOG_E("Qboot gzip decompress error. consumed=%u produced=%u end=%d", (rt_uint32_t)out->consumed, (rt_uint32_t)out->produced, is_end);
        return -RT_ERROR;
    }
    if ((out->consumed == 0) && (buf->in_len > 0)) /**< Consumed nothing with non-empty input. */
    {
        LOG_E("Qboot gzip decompress error. consumed=%u produced=%u end=%d", (rt_uint32_t)out->consumed, (rt_uint32_t)out->produced, is_end);
        return -RT_ERROR;
    }

    return RT_EOK;
}

/**
 * @brief Deinitialize gzip decompression state.
 */
static void qbt_gzip_deinit(void)
{
    inflateEnd(&qbt_strm);
    qbt_gzip_remain_len = 0;
    qbt_gzip_stream_end = RT_FALSE;
}

/**
 * @brief Initialize gzip algorithm state for decompression.
 *
 * @return RT_EOK on success.
 */
static rt_err_t qbt_algo_gzip_init(void)
{
    qbt_gzip_init();
    return RT_EOK;
}

/**
 * @brief Deinitialize gzip algorithm state.
 *
 * @return RT_EOK on success.
 */
static rt_err_t qbt_algo_gzip_deinit(void)
{
    qbt_gzip_deinit();
    return RT_EOK;
}

/** Gzip compression ops for Qboot. */
static const qboot_cmprs_ops_t qbt_algo_gzip_cmprs_ops = {
    .cmprs_name = "GZIP",
    .cmprs_id = QBOOT_ALGO_CMPRS_GZIP,
    .init = qbt_algo_gzip_init,
    .decompress = qbt_algo_gzip_decompress,
    .deinit = qbt_algo_gzip_deinit,
};

/**
 * @brief Register gzip algorithm ops into Qboot.
 *
 * @return RT_EOK on success, negative error code on failure.
 */
rt_err_t qbt_algo_gzip_register(void)
{
    return qboot_cmprs_register(&qbt_algo_gzip_cmprs_ops);
}
#endif
