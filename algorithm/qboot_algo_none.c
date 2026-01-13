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
 * Simply forwards input to output. When @p in and @p out are the same buffer,
 * no copy is performed.
 *
 * @param in        Compressed input buffer (raw data for NONE).
 * @param in_len    Length of @p in.
 * @param out       Output buffer for decompressed data.
 * @param out_len   Capacity of @p out.
 * @param consumed  [out] Bytes consumed from @p in.
 * @param produced  [out] Bytes produced to @p out.
 * @param finished  Output flag: set true when all input is consumed.
 *
 * @return Produced length on success; negative error code on invalid arguments.
 */
static rt_err_t qbt_algo_none_decompress(const u8 *in, size_t in_len, u8 *out, size_t out_len, size_t *consumed, size_t *produced, bool *finished)
{
#ifdef QBOOT_USING_COMPRESSION
    size_t copy_len = (in_len < out_len) ? in_len : out_len;
    if (copy_len > 0)
    {
        memcpy(out, in, copy_len);
    }
    *finished = (copy_len == in_len);
    *consumed = copy_len;
    *produced = copy_len;
#else
    if (in == out)
    {
        *consumed = in_len;
        *produced = in_len;
        *finished = true;
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
