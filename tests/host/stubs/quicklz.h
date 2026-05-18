#ifndef QBOOT_HOST_STUB_QUICKLZ_H
#define QBOOT_HOST_STUB_QUICKLZ_H

typedef struct qlz_state_decompress
{
    unsigned char opaque;
} qlz_state_decompress;

unsigned int qlz_decompress(const char *source,
                            void *destination,
                            qlz_state_decompress *state);

#endif /* QBOOT_HOST_STUB_QUICKLZ_H */
