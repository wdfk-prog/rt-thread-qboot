#ifndef QBOOT_HOST_STUB_FASTLZ_H
#define QBOOT_HOST_STUB_FASTLZ_H

int fastlz_decompress(const void *input,
                      int length,
                      void *output,
                      int maxout);

#endif /* QBOOT_HOST_STUB_FASTLZ_H */
