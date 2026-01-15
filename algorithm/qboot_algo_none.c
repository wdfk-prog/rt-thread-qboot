/**
 * @file qboot_algo_none.c
 * @brief 
 * @author wdfk-prog ()
 * @version 1.0
 * @date 2026-01-12
 * 
 * @copyright Copyright (c) 2026  
 * 
 * @note :
 * @par Change Log:
 * Date       Version Author      Description
 * 2026-01-12 1.0     wdfk-prog   first version
 */
#include <rtthread.h>
#include <qboot.h>
#include <string.h>

/**
 * @brief Pass-through decompress handler for NONE algorithm.
 *
 * Simply forwards input to output. When input and output buffers are the same,
 * no copy is performed.
 *
 * @param buf Input/output buffers.
 * @param ctx Decompression stream context (unused for NONE).
 * @param buf Input/output buffers.
 * @param out [out] Stream results.
 * @param ctx Decompression stream context.
 *
 * @return RT_EOK on success, -RT_ENOSPC when more input is needed, or -RT_ERROR on failure.
 */
static rt_err_t qbt_algo_none_decompress(const qbt_stream_buf_t *buf, qbt_stream_status_t *out, const qbt_stream_ctx_t *ctx)
{
    RT_UNUSED(ctx);
    if (buf->in_len == 0)
    {
        return -RT_ENOSPC;
    }
#ifdef QBOOT_USING_COMPRESSION
    size_t copy_len = (buf->in_len < buf->out_len) ? buf->in_len : buf->out_len;
    if (copy_len > 0)
    {
        rt_memcpy(buf->out, buf->in, copy_len);
    }
    out->consumed = copy_len;
    out->produced = copy_len;
#else
    if (buf->in == buf->out)
    {
        out->consumed = buf->in_len;
        out->produced = buf->in_len;
    }
    else
    {
        return -RT_ERROR;
    }
#endif /* QBOOT_USING_COMPRESSION */
    return RT_EOK;
}

static const qboot_cmprs_ops_t qbt_algo_none_cmprs_ops = {
    .init = RT_NULL,
    .decompress = qbt_algo_none_decompress,
    .deinit = RT_NULL,
};

/**
 * @brief Algorithm table entry representing the no-compression/no-encryption fallback.
 */
static const qboot_algo_ops_t qbt_algo_none_ops = {
    .algo_id = QBOOT_ALGO_CMPRS_NONE,
    .crypt = RT_NULL,
    .cmprs = &qbt_algo_none_cmprs_ops,
    .apply = RT_NULL,
};

/**
 * @brief Register the NONE algorithm entry during initialization.
 *
 * @return RT_EOK if the registration succeeds, or -RT_ERROR on failure.
 */
int qbt_algo_none_register(void)
{
    return qboot_algo_register(&qbt_algo_none_ops, QBOOT_ALGO_CMPRS_NONE);
}
