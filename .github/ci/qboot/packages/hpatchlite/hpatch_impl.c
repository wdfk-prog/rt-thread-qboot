#include "hpatch_impl.h"

hpi_patch_result_t hpi_patch(hpatchi_listener_t *listener,
                             int patch_cache_size,
                             int decompress_cache_size,
                             hpi_TInputStream_read read_diff,
                             hpi_TReadOld read_old,
                             hpi_TWriteNew write_new)
{
    (void)listener;
    (void)patch_cache_size;
    (void)decompress_cache_size;
    (void)read_diff;
    (void)read_old;
    (void)write_new;

    return HPATCHI_PATCH_ERROR;
}
