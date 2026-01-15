/**
 * @file qboot_algo.c
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
 * 2026-01-15 1.0     wdfk-prog   split ops helpers from qboot.c
 */
#include <qboot_algo.h>

#define DBG_TAG "qb_algo"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

const qboot_algo_ops_t *g_algo_table[QBOOT_ALGO_TABLE_SIZE];

#define QBT_REGISTER_ALGO(fn_call)                                \
    do                                                            \
    {                                                             \
        if ((fn_call) != RT_EOK)                                  \
        {                                                         \
            LOG_E("Qboot register algorithm fail: %s", #fn_call); \
            return -RT_ERROR;                                     \
        }                                                         \
    } while (0)

/**
 * @brief Map algorithm id to table index.
 *
 * @param algo_id Algorithm identifier.
 *
 * @return Index within the algorithm table, or QBOOT_ALGO_TABLE_SIZE when invalid.
 */
static rt_uint32_t qboot_algo_id_to_index(rt_uint16_t algo_id)
{
    if ((algo_id & QBOOT_ALGO_CMPRS_MASK) != QBOOT_ALGO_CMPRS_NONE)
    {
        rt_uint16_t cmprs_idx = (algo_id >> 8);
        if (cmprs_idx >= QBOOT_ALGO_CMPRS_COUNT)
        {
            return QBOOT_ALGO_TABLE_SIZE;
        }
        return QBOOT_ALGO_CMPRS_INDEX(algo_id);
    }

    rt_uint16_t crypt_id = algo_id & QBOOT_ALGO_CRYPT_MASK;
    if (crypt_id >= QBOOT_ALGO_CRYPT_COUNT)
    {
        return QBOOT_ALGO_TABLE_SIZE;
    }

    return QBOOT_ALGO_CRYPTO_INDEX(crypt_id);
}

/**
 * @brief Register algorithm handlers for a specific algorithm identifier.
 *
 * A handler entry may omit crypt/cmprs/apply functions when the algorithm
 * simply declares availability (e.g. raw storage). The build-in table enforces
 * uniqueness for each identifier and rejects invalid ids.
 *
 * @param ops      Algorithm handler table; required to be non-null.
 * @param algo_id  Identifier derived from compression/encryption mode.
 * @return RT_EOK on success, -RT_ERROR on invalid inputs or duplicate ids.
 */
rt_err_t qboot_algo_register(const qboot_algo_ops_t *ops, rt_uint16_t algo_id)
{
    if ((ops == RT_NULL) || (ops->cmprs == RT_NULL) || (ops->cmprs->decompress == RT_NULL))
    {
        return -RT_ERROR;
    }

    if (ops->algo_id != algo_id)
    {
        return -RT_ERROR;
    }

    rt_uint32_t idx = qboot_algo_id_to_index(algo_id);
    if (idx >= QBOOT_ALGO_TABLE_SIZE || g_algo_table[idx] != RT_NULL)
    {
        return -RT_ERROR;
    }

    g_algo_table[idx] = ops;
    return RT_EOK;
}

/**
 * @brief Find algorithm table entry by id.
 *
 * @param algo_id Algorithm identifier.
 *
 * @return ops pointer or RT_NULL when not registered.
 */
static const qboot_algo_ops_t *qboot_algo_find(rt_uint16_t algo_id)
{
    rt_uint32_t idx = qboot_algo_id_to_index(algo_id);
    if (idx >= QBOOT_ALGO_TABLE_SIZE)
    {
        return RT_NULL;
    }
    return g_algo_table[idx];
}

/**
 * @brief Resolve algorithm ops from firmware header.
 *
 * @param fw_info     Firmware header.
 * @param out_algo_id [out] Selected algorithm id; may be RT_NULL.
 *
 * @return ops pointer or RT_NULL when not registered.
 */
const qboot_algo_ops_t *qbt_fw_get_algo_ops(const fw_info_t *fw_info, rt_uint16_t *out_algo_id)
{
    rt_uint16_t cmprs_id = fw_info->algo & QBOOT_ALGO_CMPRS_MASK;
    rt_uint16_t algo_id = (cmprs_id != QBOOT_ALGO_CMPRS_NONE) ? cmprs_id : (fw_info->algo & QBOOT_ALGO_CRYPT_MASK);
    if (out_algo_id != RT_NULL)
    {
        *out_algo_id = algo_id;
    }
    return qboot_algo_find(algo_id);
}

/**
 * @brief Initialize decrypt/decompress handlers for the selected algorithm.
 *
 * @param algo_ops Algorithm ops to initialize.
 *
 * @return RT_TRUE on success, RT_FALSE on failure.
 */
rt_bool_t qbt_fw_algo_init(const qboot_algo_ops_t *algo_ops)
{
    rt_bool_t ret = RT_TRUE;
    if (algo_ops->cmprs != RT_NULL && algo_ops->cmprs->init != RT_NULL)
    {
        ret = (algo_ops->cmprs->init() == RT_EOK);
    }
    if (algo_ops->crypt != RT_NULL && algo_ops->crypt->init != RT_NULL)
    {
        ret = (algo_ops->crypt->init() == RT_EOK);
    }
    return ret;
}

/**
 * @brief Deinitialize decrypt/decompress handlers for the selected algorithm.
 *
 * @param algo_ops Algorithm ops to deinitialize; may be RT_NULL.
 */
void qbt_fw_algo_deinit(const qboot_algo_ops_t *algo_ops)
{
    if (algo_ops != RT_NULL)
    {
        if ((algo_ops->crypt != RT_NULL) && (algo_ops->crypt->deinit != RT_NULL))
        {
            algo_ops->crypt->deinit();
        }
        if ((algo_ops->cmprs != RT_NULL) && (algo_ops->cmprs->deinit != RT_NULL))
        {
            algo_ops->cmprs->deinit();
        }
    }
}

/**
 * @brief Register built-in algorithms based on build-time options.
 *
 * @return RT_EOK on success, negative error code on failure.
 */
rt_err_t qbot_algo_startup(void)
{
#ifdef QBOOT_ALGO_CRYPT_NONE
    extern rt_err_t qbt_algo_none_register(void);
    QBT_REGISTER_ALGO(qbt_algo_none_register());
#endif // QBOOT_ALGO_CRYPT_NONE
#ifdef QBOOT_USING_GZIP
    extern rt_err_t qbt_algo_gzip_register(void);
    QBT_REGISTER_ALGO(qbt_algo_gzip_register());
#endif // QBOOT_USING_GZIP
#ifdef QBOOT_USING_QUICKLZ
    extern rt_err_t qbt_algo_quicklz_register(void);
    QBT_REGISTER_ALGO(qbt_algo_quicklz_register());
#endif // QBOOT_USING_QUICKLZ
    return RT_EOK;
}
