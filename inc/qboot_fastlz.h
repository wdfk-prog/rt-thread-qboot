/*
 * qboot_fastlz.h
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-07-06     qiyongzhong       first version
 */
#ifndef __QBOOT_FASTLZ_H__
#define __QBOOT_FASTLZ_H__

#include <qboot.h>

#ifdef QBOOT_USING_FASTLZ
     
#define QBOOT_FASTLZ_BLOCK_HDR_SIZE    4
     
rt_uint32_t qbt_fastlz_get_block_size(const rt_uint8_t *comp_datas);
rt_uint32_t qbt_fastlz_decompress(rt_uint8_t *out_buf, rt_uint32_t out_buf_size, const rt_uint8_t *in_buf, rt_uint32_t block_size);

#endif

#endif

