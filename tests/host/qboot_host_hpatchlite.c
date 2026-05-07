/**
 * @file qboot_host_hpatchlite.c
 * @brief Host adapter for QBoot CI no-compress HPatchLite full-diff fixtures.
 *
 * @details This file validates QBoot integration with deterministic
 * no-compress single-cover full-diff and old-dependent delta fixtures.
 * It is not a production HPatchLite decoder replacement and must not be
 * used to claim full HPatchLite algorithm coverage.
 */
#include <qboot.h>

#define HPATCH_HOST_MAGIC_H 0x68u
#define HPATCH_HOST_MAGIC_I 0x49u
#define HPATCH_HOST_COMPRESS_NONE 0u
#define HPATCH_HOST_VERSION 1u
/** Raw CRC position relative to the HPatchLite body offset in an RBL package. */
#define HPATCH_HOST_RBL_RAW_CRC_BACK_OFFSET 16u

static rt_err_t hpatch_host_decompress(const qbt_stream_buf_t *buf,
                                       qbt_stream_status_t *out,
                                       const qbt_stream_ctx_t *ctx)
{
    RT_UNUSED(buf);
    RT_UNUSED(out);
    RT_UNUSED(ctx);
    return -RT_ERROR;
}

static const qboot_cmprs_ops_t s_hpatch_host_ops = {
    .cmprs_name = "HPatchLite",
    .cmprs_id = QBOOT_ALGO_CMPRS_HPATCHLITE,
    .init = RT_NULL,
    .decompress = hpatch_host_decompress,
    .deinit = RT_NULL,
};

static rt_bool_t read_le_size(const rt_uint8_t *data, rt_uint32_t len, rt_uint32_t *off,
                              rt_uint32_t byte_count, rt_uint32_t *out)
{
    rt_uint32_t value = 0;
    if (*off + byte_count > len || byte_count > sizeof(rt_uint32_t))
    {
        return RT_FALSE;
    }
    for (rt_uint32_t i = 0; i < byte_count; i++)
    {
        value |= ((rt_uint32_t)data[*off + i]) << (8u * i);
    }
    *off += byte_count;
    *out = value;
    return RT_TRUE;
}

static rt_bool_t read_var_uint(const rt_uint8_t *data, rt_uint32_t len, rt_uint32_t *off,
                               rt_uint32_t initial, rt_bool_t has_next,
                               rt_uint32_t *out)
{
    rt_uint32_t value = initial;
    while (has_next)
    {
        rt_uint8_t code;
        if (*off >= len)
        {
            return RT_FALSE;
        }
        code = data[*off];
        *off += 1u;
        value = (value << 7u) | (rt_uint32_t)(code & 0x7Fu);
        has_next = (code >> 7u) != 0u ? RT_TRUE : RT_FALSE;
    }
    *out = value;
    return RT_TRUE;
}

/**
 * @brief Restore a single no-compress HPatchLite cover in place.
 *
 * @param old_part Destination partition handle that still contains old APP data.
 * @param cover_out Output buffer for restored cover bytes.
 * @param old_pos Old image offset referenced by the cover.
 * @param cover_len Cover length in bytes.
 * @param sub_diff Sub-diff data for the cover.
 *
 * @return RT_TRUE when the cover is restored, RT_FALSE otherwise.
 */
static rt_bool_t restore_cover_from_old(void *old_part, rt_uint8_t *cover_out,
                                        rt_uint32_t old_pos,
                                        rt_uint32_t cover_len,
                                        const rt_uint8_t *sub_diff)
{
    rt_uint8_t old_byte;

    if (cover_len == 0u)
    {
        return RT_TRUE;
    }
    if (old_part == RT_NULL || cover_out == RT_NULL || sub_diff == RT_NULL)
    {
        return RT_FALSE;
    }
    for (rt_uint32_t i = 0; i < cover_len; i++)
    {
        if (_header_io_ops->read(old_part, old_pos + i, &old_byte, 1u) != RT_EOK)
        {
            return RT_FALSE;
        }
        cover_out[i] = (rt_uint8_t)(old_byte + sub_diff[i]);
    }
    return RT_TRUE;
}

rt_err_t qbt_algo_hpatchlite_register(void)
{
    return qboot_cmprs_register(&s_hpatch_host_ops);
}

int qbt_hpatchlite_release_from_part(void *patch_part, void *old_part,
                                     const char *patch_name, const char *old_name,
                                     int patch_file_len, int newer_file_len,
                                     int patch_file_offset)
{
    rt_uint8_t *patch;
    rt_uint8_t *output = RT_NULL;
    rt_uint32_t patch_len = (rt_uint32_t)patch_file_len;
    rt_uint32_t off = 0;
    rt_uint32_t new_size = 0;
    rt_uint32_t uncompress_size = 0;
    rt_uint32_t cover_count = 0;
    rt_uint32_t cover_len = 0;
    rt_uint32_t old_pos = 0;
    rt_uint32_t new_pos = 0;
    rt_uint32_t diff_size = 0;
    rt_uint32_t expected_crc = 0;
    rt_uint32_t old_part_len = 0;
    rt_uint32_t erase_len = 0;
    rt_uint8_t tag;
    rt_bool_t copy_old_only;
    rt_bool_t ok = RT_FALSE;

    RT_UNUSED(patch_name);
    RT_UNUSED(old_name);

    if (patch_part == RT_NULL || old_part == RT_NULL || patch_file_len <= 0 || newer_file_len < 0)
    {
        return RT_FALSE;
    }
    patch = (rt_uint8_t *)rt_malloc(patch_len);
    if (patch == RT_NULL)
    {
        return RT_FALSE;
    }
    if (_header_io_ops->read(patch_part, (rt_uint32_t)patch_file_offset, patch, patch_len) != RT_EOK)
    {
        goto exit;
    }
    if (patch_len < 4u || patch[0] != HPATCH_HOST_MAGIC_H || patch[1] != HPATCH_HOST_MAGIC_I ||
        patch[2] != HPATCH_HOST_COMPRESS_NONE || (patch[3] >> 6u) != HPATCH_HOST_VERSION)
    {
        goto exit;
    }
    off = 4u;
    if (!read_le_size(patch, patch_len, &off, patch[3] & 7u, &new_size) ||
        !read_le_size(patch, patch_len, &off, (patch[3] >> 3u) & 7u, &uncompress_size) ||
        uncompress_size != 0u || new_size != (rt_uint32_t)newer_file_len)
    {
        goto exit;
    }
    if (!read_var_uint(patch, patch_len, &off, 0u, RT_TRUE, &cover_count) || cover_count != 1u ||
        !read_var_uint(patch, patch_len, &off, 0u, RT_TRUE, &cover_len) || off >= patch_len)
    {
        goto exit;
    }
    tag = patch[off++];
    copy_old_only = ((tag & 0x80u) != 0u) ? RT_TRUE : RT_FALSE;
    if (!read_var_uint(patch, patch_len, &off, (rt_uint32_t)(tag & 0x1Fu), (tag & 0x20u) != 0u, &old_pos) ||
        (tag & 0x40u) != 0u || !read_var_uint(patch, patch_len, &off, 0u, RT_TRUE, &new_pos) ||
        new_pos > new_size || cover_len > (new_size - new_pos) ||
        (new_pos + cover_len) != new_size)
    {
        goto exit;
    }
    diff_size = new_pos;
    if (diff_size > (patch_len - off) || cover_len > (patch_len - off - diff_size))
    {
        goto exit;
    }
    if (copy_old_only && cover_len > 0u)
    {
        goto exit;
    }
    if (off + diff_size + cover_len != patch_len)
    {
        goto exit;
    }
    output = patch + off;
    off += diff_size;
    if (!restore_cover_from_old(old_part, output + new_pos, old_pos,
                                cover_len, patch + off))
    {
        goto exit;
    }
    if (patch_file_offset < (int)HPATCH_HOST_RBL_RAW_CRC_BACK_OFFSET ||
        _header_io_ops->read(patch_part,
                             (rt_uint32_t)patch_file_offset - HPATCH_HOST_RBL_RAW_CRC_BACK_OFFSET,
                             &expected_crc, sizeof(expected_crc)) != RT_EOK ||
        crc32_cal(output, new_size) != expected_crc)
    {
        goto exit;
    }
    if (_header_io_ops->size(old_part, &old_part_len) != RT_EOK)
    {
        goto exit;
    }
    erase_len = (old_part_len > new_size) ? old_part_len : new_size;
    if (qbt_erase_with_feed(old_part, 0, erase_len) != RT_EOK)
    {
        goto exit;
    }
    if (_header_io_ops->write(old_part, 0, output, new_size) != RT_EOK)
    {
        goto exit;
    }
    ok = RT_TRUE;

exit:
    rt_free(patch);
    return ok;
}
