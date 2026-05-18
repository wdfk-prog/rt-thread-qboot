/**
 * @file qboot_host_quickfast.c
 * @brief Link-only host codec placeholders for QuickLZ/FastLZ compile-matrix builds.
 */
#include <stddef.h>
#include <quicklz.h>
#include <fastlz.h>

/**
 * @brief Placeholder QuickLZ decompressor used only by compile-matrix builds.
 *
 * @param source      Encoded input block.
 * @param destination Output buffer.
 * @param state       QuickLZ decompression state.
 * @return Always zero; runtime codec validation uses external package sources.
 */
unsigned int qlz_decompress(const char *source,
                            void *destination,
                            qlz_state_decompress *state)
{
    (void)source;
    (void)destination;
    (void)state;
    return 0u;
}

/**
 * @brief Placeholder FastLZ decompressor used only by compile-matrix builds.
 *
 * @param input  Encoded input block.
 * @param length Encoded input length.
 * @param output Output buffer.
 * @param maxout Output buffer capacity.
 * @return Always zero; runtime codec validation uses external package sources.
 */
int fastlz_decompress(const void *input, int length, void *output, int maxout)
{
    (void)input;
    (void)length;
    (void)output;
    (void)maxout;
    return 0;
}
