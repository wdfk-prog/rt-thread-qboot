/*
 * qboot_fastlz.c
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-07-06     qiyongzhong       first version
 */
 
#include <qboot_fastlz.h>
     
#ifdef QBOOT_USING_FASTLZ

#include <fastlz.h>
     
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

#endif

