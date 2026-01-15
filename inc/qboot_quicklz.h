/*
 * qboot_quicklz.h
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-07-06     qiyongzhong       first version
 */

#ifndef __QBOOT_QUICKLZ_H__
#define __QBOOT_QUICKLZ_H__

#include <qboot.h>

#ifdef QBOOT_USING_QUICKLZ

#define QBOOT_QUICKLZ_BLOCK_HDR_SIZE    4

void qbt_quicklz_state_init(void);
rt_uint32_t qbt_quicklz_get_block_size(const rt_uint8_t *comp_datas);
rt_uint32_t qbt_quicklz_decompress(rt_uint8_t *out_buf, const rt_uint8_t *in_buf);
int qbt_algo_quicklz_register(void);

#endif

#endif
