/*
 * qboot_aes.c
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-07-06     qiyongzhong       first version
 */

#include <qboot_aes.h>

#ifdef QBOOT_USING_AES

#include <tinycrypt.h>
#include <string.h>

static rt_uint8_t qbt_aes_iv[16];
static tiny_aes_context qbt_aes_ctx;

void qbt_aes_decrypt_init(void)
{
    int len = strlen(QBOOT_AES_IV);
    if (len > sizeof(qbt_aes_iv))
    {
        len = sizeof(qbt_aes_iv);
    }
    memset(qbt_aes_iv, 0, sizeof(qbt_aes_iv));
    memcpy(qbt_aes_iv, QBOOT_AES_IV, len);
    tiny_aes_setkey_dec(&qbt_aes_ctx, (rt_uint8_t *)QBOOT_AES_KEY, 256);
}

void qbt_aes_decrypt(rt_uint8_t *dst_buf, const rt_uint8_t *src_buf, rt_uint32_t len)
{
    tiny_aes_crypt_cbc(&qbt_aes_ctx, AES_DECRYPT, len, qbt_aes_iv, (rt_uint8_t *)src_buf, dst_buf);    
}

#endif

