#ifndef HPATCH_IMPL_H__
#define HPATCH_IMPL_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char hpi_byte;
typedef int hpi_BOOL;
typedef unsigned int hpi_pos_t;
typedef unsigned int hpi_size_t;
typedef void *hpi_TInputStreamHandle;
typedef int hpi_patch_result_t;

#define hpi_TRUE                  1
#define hpi_FALSE                 0
#define HPATCHI_SUCCESS           0
#define HPATCHI_PATCH_ERROR      -1
#define HPATCHI_PATCH_OPEN_ERROR -2
#define HPATCHI_OPTIONS_ERROR    -3
#define HPATCHI_FILEWRITE_ERROR  -4

struct hpatchi_listener_t;

typedef hpi_BOOL (*hpi_TInputStream_read)(hpi_TInputStreamHandle input_stream,
                                          hpi_byte *data,
                                          hpi_size_t *size);
typedef hpi_BOOL (*hpi_TReadOld)(struct hpatchi_listener_t *listener,
                                 hpi_pos_t addr,
                                 hpi_byte *data,
                                 hpi_size_t size);
typedef hpi_BOOL (*hpi_TWriteNew)(struct hpatchi_listener_t *listener,
                                  const hpi_byte *data,
                                  hpi_size_t size);

typedef struct hpatchi_listener_t
{
    hpi_TReadOld read_old;
    hpi_TWriteNew write_new;
} hpatchi_listener_t;

hpi_patch_result_t hpi_patch(hpatchi_listener_t *listener,
                             int patch_cache_size,
                             int decompress_cache_size,
                             hpi_TInputStream_read read_diff,
                             hpi_TReadOld read_old,
                             hpi_TWriteNew write_new);

#ifdef __cplusplus
}
#endif

#endif /* HPATCH_IMPL_H__ */
