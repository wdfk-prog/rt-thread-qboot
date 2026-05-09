#ifndef TINYCRYPT_H
#define TINYCRYPT_H

#include <rtthread.h>

#define AES_DECRYPT 0

/** @brief Minimal AES context used by the host TinyCrypt-compatible stub. */
typedef struct
{
    rt_uint8_t round_key[240]; /**< Expanded AES key schedule. */
    int nr;                    /**< AES round count. */
} tiny_aes_context;

void tiny_aes_setkey_dec(tiny_aes_context *ctx, rt_uint8_t *key, int bits);
void tiny_aes_crypt_cbc(tiny_aes_context *ctx, int mode, rt_uint32_t length,
                        rt_uint8_t iv[16], rt_uint8_t *input, rt_uint8_t *output);

#endif /* TINYCRYPT_H */
