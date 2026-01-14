/*
 * qboot.c
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-07-06     qiyongzhong       first version
 * 2020-08-31     qiyongzhong       fix qbt_jump_to_app type from static to weak
 * 2020-09-01     qiyongzhong       add app verify when checking firmware
 * 2020-09-18     qiyongzhong       fix bug of gzip decompression
 * 2020-09-22     qiyongzhong       add erase firmware function, update version to v1.04
 * 2020-10-05     qiyongzhong       fix to support stm32h7xx, update version to v1.05
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <fal.h>
#include <qboot.h>
#include <string.h>
#ifdef QBOOT_USING_SHELL
#include "shell.h"
#endif

#include "crc32.h"

#ifdef QBOOT_USING_STATUS_LED
#include <qled.h>
#endif

#ifdef QBOOT_USING_AES
#include <qboot_aes.h>
#endif /* QBOOT_USING_AES */
#ifdef QBOOT_USING_GZIP
#include <qboot_gzip.h>
#endif /* QBOOT_USING_GZIP */
#ifdef QBOOT_USING_QUICKLZ
#include <qboot_quicklz.h>
#endif /* QBOOT_USING_QUICKLZ */
#ifdef QBOOT_USING_HPATCHLITE
#include <qboot_hpatchlite.h>
#endif /* QBOOT_USING_HPATCHLITE */

//#define QBOOT_DEBUG
#define QBOOT_USING_LOG
#define DBG_TAG "Qboot"

#ifdef QBOOT_DEBUG
#define DBG_LVL DBG_LOG
#else
#define DBG_LVL DBG_INFO
#endif

#ifdef QBOOT_USING_LOG
#ifndef DBG_ENABLE
#define DBG_ENABLE
#endif
#ifndef DBG_COLOR
#define DBG_COLOR
#endif
#endif

#include <rtdbg.h>

#define QBOOT_VER_MSG      "V1.1.0 2026.01.01"
#define QBOOT_SHELL_PROMPT "Qboot>"

#define QBOOT_BUF_SIZE 4096//must is 4096
#if (defined(QBOOT_USING_QUICKLZ) || defined(QBOOT_USING_FASTLZ))
#define QBOOT_CMPRS_READ_SIZE 4096 //it can is 512, 1024, 2048, 4096,
#define QBOOT_CMPRS_BUF_SIZE  (QBOOT_BUF_SIZE + QBOOT_CMPRS_READ_SIZE + 32)
#else
#define QBOOT_CMPRS_READ_SIZE QBOOT_BUF_SIZE
#define QBOOT_CMPRS_BUF_SIZE  QBOOT_BUF_SIZE
#endif


#define QBOOT_ALGO2_VERIFY_NONE 0
#define QBOOT_ALGO2_VERIFY_CRC  1
#define QBOOT_ALGO2_VERIFY_MASK 0x0F

static fw_info_t fw_info;
static u8 g_cmprs_buf[QBOOT_CMPRS_BUF_SIZE];    /* Decompression buffer. */
#ifdef QBOOT_USING_COMPRESSION
static u8 g_crypt_buf[QBOOT_BUF_SIZE];          /* Decryption buffer. */
#else
#define g_crypt_buf g_cmprs_buf
#endif
/* Share a buffer to reduce memory allocation */
#define g_decmprs_buf g_crypt_buf               /* Decompression output buffer. */

static const qboot_header_parser_ops_t *_header_parser_ops = RT_NULL;
static const qboot_io_ops_t *_header_io_ops = RT_NULL;
static const qboot_update_ops_t *_update_ops = RT_NULL;
static const qboot_algo_ops_t *g_algo_table[QBOOT_ALGO_TABLE_SIZE];

#define QBT_REGISTER_ALGO(fn_call)                                \
    do                                                            \
    {                                                             \
        if ((fn_call) != RT_EOK)                                  \
        {                                                         \
            LOG_E("Qboot register algorithm fail: %s", #fn_call); \
            return -RT_ERROR;                                     \
        }                                                         \
    } while (0)

/**
 * @brief Default jump decision; always allow.
 *
 * @return true jump to application is allowed.
 */
static bool qboot_default_allow_jump(void)
{
    return true;
}

const qboot_update_ops_t g_qboot_update_default = {
    RT_NULL,
    qboot_default_allow_jump,
    RT_NULL,
};

/**
 * @brief Weak hook to register storage ops; override in backend implementations.
 */
__WEAK int qboot_register_storage_ops(void)
{
    return RT_EOK;
}

/**
 * @brief Register header parser/package source operations.
 *
 * @param ops       Operation table; required fields must be non-NULL.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
int qboot_register_header_parser_ops(const qboot_header_parser_ops_t *ops)
{
    if ((ops == RT_NULL) || (ops->sign_read == RT_NULL) || (ops->sign_write == RT_NULL))
    {
        return -RT_ERROR;
    }
    _header_parser_ops = ops;
    return RT_EOK;
}

int qboot_register_header_io_ops(const qboot_io_ops_t *ops)
{
    if ((ops == RT_NULL) || (ops->open == RT_NULL) || (ops->close == RT_NULL) || (ops->read == RT_NULL) || (ops->erase == RT_NULL) || (ops->write == RT_NULL) || (ops->size == RT_NULL))
    {
        return -RT_ERROR;
    }
    _header_io_ops = ops;
    return RT_EOK;
}

/**
 * @brief Register update callbacks.
 *
 * @param ops Operation table; required fields must be non-NULL.
 *
 * @return RT_EOK on success.
 */
int qboot_register_update(const qboot_update_ops_t *ops)
{
    if (ops == RT_NULL || ops->allow_jump == RT_NULL)
    {
        return -RT_ERROR;
    }

    _update_ops = ops;
    return RT_EOK;
}

static size_t qboot_algo_id_to_index(u16 algo_id)
{
    if ((algo_id & QBOOT_ALGO_CMPRS_MASK) != QBOOT_ALGO_CMPRS_NONE)
    {
        u16 cmprs_idx = (algo_id >> 8);
        if (cmprs_idx >= QBOOT_ALGO_CMPRS_COUNT)
        {
            return QBOOT_ALGO_TABLE_SIZE;
        }
        return QBOOT_ALGO_CMPRS_INDEX(algo_id);
    }

    u16 crypt_id = algo_id & QBOOT_ALGO_CRYPT_MASK;
    if (crypt_id >= QBOOT_ALGO_CRYPT_COUNT)
    {
        return QBOOT_ALGO_TABLE_SIZE;
    }

    return QBOOT_ALGO_CRYPTO_INDEX(crypt_id);
}

/**
 * @brief Register algorithm handlers for a specific algorithm identifier.
 *
 * A handler entry may omit crypt/cmprs/apply functions when the algorithm
 * simply declares availability (e.g. raw storage).  The build-in table enforces
 * uniqueness for each identifier and rejects invalid ids.
 *
 * @param ops      Algorithm handler table; required to be non-null.
 * @param algo_id  Identifier derived from compression/encryption mode.
 * @return RT_EOK on success, -RT_ERROR on invalid inputs or duplicate ids.
 */
rt_err_t qboot_algo_register(const qboot_algo_ops_t *ops, u16 algo_id)
{
    if ((ops == RT_NULL) || (ops->cmprs == RT_NULL) || (ops->cmprs->decompress == RT_NULL))
    {
        return -RT_ERROR;
    }

    if (ops->algo_id != algo_id)
    {
        return -RT_ERROR;
    }

    size_t idx = qboot_algo_id_to_index(algo_id);
    if (idx >= QBOOT_ALGO_TABLE_SIZE || g_algo_table[idx] != RT_NULL)
    {
        return -RT_ERROR;
    }

    g_algo_table[idx] = ops;
    return RT_EOK;
}

const qboot_algo_ops_t *qboot_algo_find(u16 algo_id)
{
    size_t idx = qboot_algo_id_to_index(algo_id);
    if (idx >= QBOOT_ALGO_TABLE_SIZE)
    {
        return RT_NULL;
    }
    return g_algo_table[idx];
}

static const qboot_algo_ops_t *qbt_fw_get_algo_ops(const fw_info_t *fw_info, u16 *out_algo_id)
{
    u16 cmprs_id = fw_info->algo & QBOOT_ALGO_CMPRS_MASK;
    u16 algo_id = (cmprs_id != QBOOT_ALGO_CMPRS_NONE) ? cmprs_id : (fw_info->algo & QBOOT_ALGO_CRYPT_MASK);
    if (out_algo_id != RT_NULL)
    {
        *out_algo_id = algo_id;
    }
    return qboot_algo_find(algo_id);
}

/**
 * @brief Open target by name and optionally query size.
 *
 * @param name      Target identifier (partition name/path).
 * @param handle    Output opaque handle.
 * @param out_size  Output total size; ignored if NULL.
 *
 * @return true on success, false otherwise.
 */
static bool qbt_target_open(const char *name, void **handle, size_t *out_size)
{
    if (_header_io_ops->open(handle, name) != RT_EOK)
    {
        return false;
    }
    if (out_size != RT_NULL)
    {
        if (_header_io_ops->size(*handle, out_size) != RT_EOK)
        {
            _header_io_ops->close(*handle);
            return false;
        }
    }
    return true;
}

static void qbt_target_close(void *handle)
{
    if (handle != RT_NULL)
    {
        _header_io_ops->close(handle);
    }
}

static bool qbt_fw_info_read(void *handle, size_t part_len, fw_info_t *fw_info, bool from_tail)
{
    if (from_tail && part_len < sizeof(fw_info_t))
    {
        LOG_E("Qboot read firmware info fail. part size %u < hdr size.", (unsigned int)part_len);
        return false;
    }
    size_t addr = from_tail ? (part_len - sizeof(fw_info_t)) : 0;
    if (_header_io_ops->read(handle, addr, (u8 *)fw_info, sizeof(fw_info_t)) != RT_EOK)
    {
        return (false);
    }
    return (true);
}

/**
 * @brief Write firmware header to head or tail of target.
 *
 * @param handle   Target handle from qbt_target_open.
 * @param part_len Target total size (required when @p to_tail is true).
 * @param fw_info  Firmware header to write.
 * @param to_tail  true to write at tail, false at offset 0.
 *
 * @return true on success, false on failure.
 */
static bool qbt_fw_info_write(void *handle, size_t part_len, fw_info_t *fw_info, bool to_tail)
{
    size_t addr = to_tail ? (part_len - sizeof(fw_info_t)) : 0;
    if (_header_io_ops->write(handle, addr, (u8 *)fw_info, sizeof(fw_info_t)) != RT_EOK)
    {
        return (false);
    }
    return (true);
}

bool qbt_fw_info_check(fw_info_t *fw_info)
{
    if (strcmp((const char *)(fw_info->type), "RBL") != 0)
    {
        return (false);
    }

    return (crc32_cal((u8 *)fw_info, (sizeof(fw_info_t) - sizeof(u32))) == fw_info->hdr_crc);
}

static bool qbt_fw_crc_check(void *handle, const char *name, u32 addr, u32 size, u32 crc)
{
    u32 pos = 0;
    u32 crc32 = 0xFFFFFFFF;
    while (pos < size)
    {
        int read_len = QBOOT_BUF_SIZE;
        int gzip_remain_len = size - pos;
        if (read_len > gzip_remain_len)
        {
            read_len = gzip_remain_len;
        }
        if (_header_io_ops->read(handle, addr + pos, g_cmprs_buf, read_len) != RT_EOK)
        {
            LOG_E("Qboot read firmware datas fail. part = %s, addr = %08X, length = %d", name, pos, read_len);
            return (false);
        }
        crc32 = crc32_cyc_cal(crc32, g_cmprs_buf, read_len);
        pos += read_len;
    }
    crc32 ^= 0xFFFFFFFF;

    if (crc32 != crc)
    {
        LOG_E("Qboot verify CRC32 error, cal.crc: %08X != body.crc: %08X", crc32, crc);
        return (false);
    }

    return (true);
}

static bool qbt_release_sign_check(void *handle, const char *name, fw_info_t *fw_info)
{
    bool released = false;
    rt_err_t rst = _header_parser_ops->sign_read(handle, &released, fw_info);
    if (rst != RT_EOK)
    {
        LOG_E("Qboot read release sign fail from %s partition. rst=%d", name, rst);
        return (false);
    }
    return released;
}

static bool qbt_release_sign_write(void *handle, const char *name, fw_info_t *fw_info)
{
    rt_err_t rst = _header_parser_ops->sign_write(handle, fw_info);
    if (rst != RT_EOK)
    {
        LOG_E("Qboot write release sign fail from %s partition. rst=%d", name, rst);
        return (false);
    }
    return true;
}

static bool qbt_fw_algo_init(const qboot_algo_ops_t *algo_ops)
{
    bool ret = true;
    if (algo_ops->cmprs != RT_NULL && algo_ops->cmprs->init != RT_NULL)
    {
        ret = (algo_ops->cmprs->init() == RT_EOK);
    }
    if (algo_ops->crypt != RT_NULL && algo_ops->crypt->init != RT_NULL)
    {
        ret = (algo_ops->crypt->init() == RT_EOK);
    }
    return ret;
}

static void qbt_fw_algo_deinit(const qboot_algo_ops_t *algo_ops)
{
    if (algo_ops != RT_NULL)
    {
        if ((algo_ops->crypt != RT_NULL) && (algo_ops->crypt->deinit != RT_NULL))
        {
            algo_ops->crypt->deinit();
        }
        if ((algo_ops->cmprs != RT_NULL) && (algo_ops->cmprs->deinit != RT_NULL))
        {
            algo_ops->cmprs->deinit();
        }
    }
}

/**
 * @brief Read a package fragment and optionally decrypt into caller buffer.
 *
 * @param src_handle  Source handle (partition/file) to read from.
 * @param src_off     Byte offset within the source.
 * @param out_buf     Output buffer for plaintext.
 * @param crypt_buf   Scratch buffer for cipher text (may alias @p out_buf when no decrypt).
 * @param read_len    Number of bytes to read.
 * @param algo_ops    Algorithm ops describing decrypt handler; may be NULL for raw.
 *
 * @return true on success, false on read/decrypt failure.
 */
static bool qbt_fw_pkg_read(void *src_handle, u32 src_off, u8 *out_buf, u8 *crypt_buf, u32 read_len, const qboot_algo_ops_t *algo_ops)
{
    if (algo_ops->crypt != RT_NULL)
    {
        if (_header_io_ops->read(src_handle, src_off, crypt_buf, read_len) != RT_EOK)
        {
            return (false);
        }

        if (algo_ops->crypt->decrypt == RT_NULL)
        {
            memcpy(out_buf, crypt_buf, read_len);
            return (true);
        }
        else
        {
            return (algo_ops->crypt->decrypt(out_buf, crypt_buf, read_len) == RT_EOK);
        }
    }
    else
    {
        return (_header_io_ops->read(src_handle, src_off, out_buf, read_len) == RT_EOK);
    }
}

/**
 * @brief Consumer callback for a decompressed output chunk.
 *
 * @param buf Output data buffer.
 * @param len Output data length.
 * @param ctx User context provided by the caller.
 *
 * @return RT_EOK on success, negative error code otherwise.
 */
typedef rt_err_t (*qbt_cmprs_consumer_t)(const u8 *buf, size_t len, void *ctx);

/**
 * @brief Decompress compressed input and forward each produced chunk to a consumer.
 *
 * @par Flow Steps (1-4)
 * 1) Build per-iteration context and decide whether an empty-input flush is allowed.
 * 2) Invoke the decompressor and interpret -RT_ENOSPC vs errors vs progress.
 * 3) Dispatch produced data to the consumer and update counters/output budget.
 * 4) Compact input buffer, track flush state, and report remaining input on exit.
 * 
 * @param algo_ops     Algorithm ops providing decompress handler.
 * @param cmprs_buf    Input compressed buffer.
 * @param p_cmprs_len  [in/out] Available compressed length; leftover after return.
 * @param out_buf      Output buffer for decompressed data.
 * @param out_cap      Capacity of @p out_buf.
 * @param remaining    Maximum bytes to produce for this call.
 * @param consumer     Callback invoked for each produced chunk.
 * @param consumer_ctx User data passed to @p consumer.
 * @param cmprs_ctx    Stream context for this call (may be NULL).
 *
 * @return Total produced length on success, -1 on error.
 */
static int qbt_decompress_with_consumer(const qboot_algo_ops_t *algo_ops, u8 *cmprs_buf, u32 *p_cmprs_len,
                                        u8 *out_buf, size_t out_cap, size_t remaining,
                                        qbt_cmprs_consumer_t consumer, void *consumer_ctx,
                                        const qbt_cmprs_ctx_t *cmprs_ctx)
{
    size_t cmprs_len = *p_cmprs_len;
    size_t total_out = 0;
    size_t total_consumed = 0;
    bool flushed_empty = false; /**< Track empty-input flush usage. */

    /* Keep looping while output budget remains. */
    while (remaining > 0)
    {
        size_t cur_cap = (remaining < out_cap) ? remaining : out_cap;   /**< Output cap for this iteration. */
        const qbt_cmprs_ctx_t *ctx_ptr = RT_NULL;                       /**< Context pointer for decompressor. */
        qbt_cmprs_ctx_t call_ctx;                                       /**< Local context copy. */
        bool allow_empty = false;                                       /**< Allow one empty-input flush. */

        /* (1) Build per-iteration context and decide empty-input flush. */
        if (cmprs_ctx != RT_NULL)
        {
            call_ctx = *cmprs_ctx;                          /**< Copy base context. */
            call_ctx.cmprs_consumed = cmprs_ctx->cmprs_consumed + total_consumed; /**< Update consumed offset. */
            call_ctx.raw_remaining = remaining;             /**< Refresh remaining raw budget. */
            ctx_ptr = &call_ctx;                            /**< Pass updated context to decompressor. */
        }

        if (cmprs_len == 0)                                 /**< No buffered compressed input. */
        {
            if ((cmprs_ctx != RT_NULL) && (call_ctx.cmprs_total > 0) && (call_ctx.cmprs_consumed >= call_ctx.cmprs_total)) /**< Stream ended. */
            {
                allow_empty = true;                         /**< Allow a single flush with empty input. */
            }
            if (!allow_empty || flushed_empty)              /**< Disallow repeated empty flush. */
            {
                break;                                      /**< Stop processing. */
            }
        }

        /* (2) Invoke decompressor and validate progress paths. */
        /* Ask algorithm to decompress one unit of input. */
        qbt_cmprs_result_t result = { 0 };                    /**< Initialize result container. */
        qbt_cmprs_buf_t io = { cmprs_buf, cmprs_len, out_buf, cur_cap }; /**< Prepare IO buffers. */
        rt_err_t rst = algo_ops->cmprs->decompress(&io, &result, ctx_ptr); /**< Invoke decompressor. */
        if (rst == -RT_ENOSPC)                               /**< Need more input data. */
        {
            break;                                          /**< Preserve input for next round. */
        }
        if ((rst != RT_EOK) || (result.consumed > cmprs_len) || (result.produced > cur_cap))
        {
            *p_cmprs_len = (u32)cmprs_len;
            return -1;
        }

        /* No progress and not finished means the algorithm needs more input. */
        if ((result.consumed == 0) && (result.produced == 0) && (!result.finished)) /**< No progress and not finished. */
        {
            break;                                          /**< Wait for more input. */
        }
        if ((result.produced == 0) || ((result.consumed == 0) && (cmprs_len > 0))) /**< Inconsistent progress. */
        {
            *p_cmprs_len = (u32)cmprs_len;
            return -1;
        }

        /* (3) Dispatch output and update counters/budget. */
        if ((consumer != RT_NULL) && (consumer(out_buf, result.produced, consumer_ctx) != RT_EOK))
        {
            *p_cmprs_len = (u32)cmprs_len;
            return -1;
        }

        /* Accumulate output and enforce the optional total output cap. */
        total_out += result.produced;
        total_consumed += result.consumed;
        if (result.produced > remaining) /**< Check budget overflow. */
        {
            *p_cmprs_len = (u32)cmprs_len;
            return -1;
        }
        remaining -= result.produced;                       /**< Decrease remaining budget. */

        /* (4) Compact input buffer and track empty flush usage. */
        cmprs_len -= result.consumed;                       /**< Remove consumed input. */
        if (cmprs_len > 0)                                  /**< Compact buffer when needed. */
        {
            rt_memmove(cmprs_buf, cmprs_buf + result.consumed, cmprs_len); /**< Move remaining input forward. */
        }
        if (result.consumed == 0)                           /**< Track empty-input flush. */
        {
            flushed_empty = true;                           /**< Prevent repeated flush. */
        }
    }

    /* Report remaining compressed data to the caller. */
    *p_cmprs_len = (u32)cmprs_len;                          /**< Return buffered input length. */
    if (total_out == 0)                                     /**< Warn when no output produced. */
    {
        LOG_W("Qboot decompress produce no data.");
    }
    return (int)total_out;
}

/**
 * @brief Stream processor callback for decompression output.
 *
 * @param ctx        User context provided by the caller.
 * @param raw_pos    Current raw output offset (bytes).
 * @param remaining  Remaining raw bytes allowed to be produced.
 * @param out_buf    Output buffer for decompressed data.
 * @param cmprs_buf  Input compressed buffer.
 * @param p_cmprs_len [in/out] Available compressed length; leftover after return.
 * @param algo_ops   Algorithm ops providing decompress handler.
 * @param cmprs_ctx  Stream context for this call.
 *
 * @return Produced length on success, negative on error.
 */
typedef int (*qbt_stream_proc_t)(void *ctx, u32 raw_pos, u32 remaining, u8 *out_buf, u8 *cmprs_buf,
                                 u32 *p_cmprs_len, const qboot_algo_ops_t *algo_ops, const qbt_cmprs_ctx_t *cmprs_ctx);

/**
 * @brief Progress callback for streaming operations.
 *
 * @param ctx       User context provided by the caller.
 * @param raw_pos   Current raw output offset (bytes).
 * @param raw_total Total raw size to be produced.
 */
typedef void (*qbt_stream_progress_t)(void *ctx, u32 raw_pos, u32 raw_total);

/**
 * @brief Fixed configuration for a stream processing operation.
 */
typedef struct
{
    void *src_handle;                 /**< Package source handle. */
    const fw_info_t *fw_info;         /**< Firmware info header. */
    const qboot_algo_ops_t *algo_ops; /**< Algorithm handler table. */
    u8 *cmprs_buf;                    /**< Buffer to accumulate compressed input. */
    u8 *out_buf;                      /**< Buffer to hold decompressed output. */
    u8 *crypt_buf;                    /**< Scratch buffer for encrypted input. */
} qbt_stream_cfg_t;

/**
 * @brief Stream package data, decompress, and consume output with strict raw-size limits.
 *
 * This function loops over the package content, reads compressed data into a staging
 * buffer, calls the decompressor, and forwards produced data to @p proc. Each call
 * enforces a maximum output length equal to the remaining raw size to avoid overruns.
 *
 * @param cfg          Stream configuration (package source and working buffers).
 * @param proc         Output consumer invoked for each produced chunk.
 * @param proc_ctx     User context for @p proc.
 * @param progress     Optional progress callback.
 * @param progress_ctx User context for @p progress.
 * @param purpose      Stream purpose (write/CRC).
 *
 * @return true on success, false on read or decompression/consume error.
 */
static bool qbt_fw_stream_process(const qbt_stream_cfg_t *cfg, qbt_stream_proc_t proc, void *proc_ctx,
                                  qbt_stream_progress_t progress, void *progress_ctx, qbt_stream_purpose_t purpose)
{
    /* Track package read position, raw output position, and buffered input length. */
    u32 src_read_pos = sizeof(fw_info_t);
    u32 raw_pos = 0;
    u32 cmprs_len = 0;

    /* Continue until the expected raw size has been produced. */
    while (raw_pos < cfg->fw_info->raw_size)
    {
        /* Limit read length to remaining package bytes. */
        int remain_len = (cfg->fw_info->pkg_size + sizeof(fw_info_t) - src_read_pos);
        if (remain_len > 0)
        {
            int read_len = QBOOT_CMPRS_READ_SIZE;
            if (read_len > remain_len)
            {
                read_len = remain_len;
            }

            /* Read (and decrypt if required) into the tail of the compressed buffer. */
            if (!qbt_fw_pkg_read(cfg->src_handle, src_read_pos, cfg->cmprs_buf + cmprs_len, cfg->crypt_buf, read_len, cfg->algo_ops))
            {
                LOG_E("Qboot stream read pkg error. addr=%08X, len=%d", src_read_pos, read_len);
                return false;
            }

            /* Advance read position and update buffered compressed length. */
            src_read_pos += read_len;
            cmprs_len += (u32)read_len;
        }
        else if (remain_len < 0)
        {
            LOG_E("Qboot stream read pkg error. read beyond pkg size %d.", remain_len);
            return false;
        }
        else if (cmprs_len == 0)
        {
            LOG_E("Qboot stream read pkg error. no compressed input left.");
            return false;
        }

        /* Enforce strict raw output cap based on remaining expected size. */
        u32 remaining = cfg->fw_info->raw_size - raw_pos;
        qbt_cmprs_ctx_t cmprs_ctx = {
            .cmprs_total = cfg->fw_info->pkg_size,
            .cmprs_consumed = (size_t)(src_read_pos - sizeof(fw_info_t)) - cmprs_len,
            .raw_remaining = remaining,
            .purpose = purpose,
        };
        int out_len = proc(proc_ctx, raw_pos, remaining, cfg->out_buf, cfg->cmprs_buf, &cmprs_len, cfg->algo_ops, &cmprs_ctx);
        if (out_len <= 0)
        {
            LOG_E("Qboot stream process error. addr=%08X, out_len = %d", raw_pos, out_len);
            return false;
        }

        /* Advance raw output position and report progress. */
        raw_pos += (u32)out_len;
        if (progress != RT_NULL)
        {
            progress(progress_ctx, raw_pos, cfg->fw_info->raw_size);
        }
    }

    return true;
}

#ifdef QBOOT_USING_APP_CHECK
/**
 * @brief CRC consumer for a decompressed output chunk.
 *
 * @param buf Output data buffer.
 * @param len Output data length.
 * @param ctx CRC accumulator pointer (u32 *).
 *
 * @return RT_EOK on success.
 */
static rt_err_t qbt_crc_chunk_consumer(const u8 *buf, size_t len, void *ctx)
{
    u32 *crc_acc = (u32 *)ctx;
    *crc_acc = crc32_cyc_cal(*crc_acc, (u8 *)buf, len);
    return RT_EOK;
}

/**
 * @brief Stream processor to update CRC from decompressed data.
 *
 * @param ctx        CRC accumulator pointer (u32 *).
 * @param raw_pos    Current raw output offset (unused).
 * @param remaining  Remaining raw bytes allowed to be produced.
 * @param out_buf    Output buffer for decompressed data.
 * @param cmprs_buf  Input compressed buffer.
 * @param p_cmprs_len [in/out] Available compressed length; leftover after return.
 * @param algo_ops   Algorithm ops providing decompress handler.
 * @param cmprs_ctx  Stream context for this call.
 *
 * @return Produced length on success, negative on error.
 */
static int qbt_stream_crc_proc(void *ctx, u32 raw_pos, u32 remaining, u8 *out_buf, u8 *cmprs_buf,
                               u32 *p_cmprs_len, const qboot_algo_ops_t *algo_ops, const qbt_cmprs_ctx_t *cmprs_ctx)
{
    RT_UNUSED(raw_pos);
    u32 *crc_acc = (u32 *)ctx;
    return qbt_decompress_with_consumer(algo_ops, cmprs_buf, p_cmprs_len, out_buf, QBOOT_BUF_SIZE, remaining,
                                        qbt_crc_chunk_consumer, crc_acc, cmprs_ctx);
}

/**
 * @brief Stream and verify application CRC using the selected algorithm.
 *
 * @param src_handle Package source handle.
 * @param src_name   Source name (unused, retained for signature compatibility).
 * @param fw_info    Firmware info header.
 *
 * @return true on success, false on error or CRC mismatch.
 */
static bool qbt_app_crc_check(void *src_handle, const char *src_name, fw_info_t *fw_info)
{
    bool ret = true;
    u32 crc32 = 0xFFFFFFFF;
    u16 algo_id;
    const qboot_algo_ops_t *algo_ops = qbt_fw_get_algo_ops(fw_info, &algo_id);

    RT_UNUSED(src_name);

    if (algo_ops == RT_NULL)
    {
        LOG_E("Qboot app crc check fail. algo 0x%04X not registered.", algo_id);
        return false;
    }

    if (!qbt_fw_algo_init(algo_ops))
    {
        LOG_E("Qboot app crc check fail. algo init fail");
        return (false);
    }

    qbt_stream_cfg_t stream_cfg = {
        .src_handle = src_handle,
        .fw_info = fw_info,
        .algo_ops = algo_ops,
        .cmprs_buf = g_cmprs_buf,
        .out_buf = g_decmprs_buf,
        .crypt_buf = g_crypt_buf,
    };

    if (!qbt_fw_stream_process(&stream_cfg, qbt_stream_crc_proc, &crc32, RT_NULL, RT_NULL, QBT_STREAM_CRC))
    {
        ret = false;
        LOG_E("Qboot app crc check fail. decompress error.");
    }
    else
    {
        crc32 ^= 0xFFFFFFFF;
        if (crc32 != fw_info->raw_crc)
        {
            LOG_E("Qboot app crc check fail. cal.crc: %08X != raw.crc: %08X", crc32, fw_info->raw_crc);
            ret = false;
        }
        else
        {
            ret = true;
        }
    }

    qbt_fw_algo_deinit(algo_ops);
    return ret;
}
#endif

/**
 * @brief Write context for output consumer.
 */
typedef struct
{
    void *handle; /**< Destination handle. */
    u32 offset;   /**< Current write offset. */
} qbt_write_ctx_t;

/**
 * @brief Output consumer that writes produced data to destination storage.
 *
 * @param buf Output data buffer.
 * @param len Output data length.
 * @param ctx Write context (qbt_write_ctx_t *).
 *
 * @return RT_EOK on success, negative error code on failure.
 */
static rt_err_t qbt_write_chunk_consumer(const u8 *buf, size_t len, void *ctx)
{
    qbt_write_ctx_t *w = (qbt_write_ctx_t *)ctx;
    if (_header_io_ops->write(w->handle, w->offset, buf, len) != RT_EOK)
    {
        return -RT_ERROR;
    }
    w->offset += (u32)len;
    return RT_EOK;
}

/**
 * @brief Stream processor to write decompressed data to destination storage.
 *
 * @param ctx        Destination handle.
 * @param raw_pos    Current raw output offset (write position).
 * @param remaining  Remaining raw bytes allowed to be produced.
 * @param out_buf    Output buffer for decompressed data.
 * @param cmprs_buf  Input compressed buffer.
 * @param p_cmprs_len [in/out] Available compressed length; leftover after return.
 * @param algo_ops   Algorithm ops providing decompress handler.
 * @param cmprs_ctx  Stream context for this call.
 *
 * @return Produced length on success, negative on error.
 */
static int qbt_stream_write_proc(void *ctx, u32 raw_pos, u32 remaining, u8 *out_buf, u8 *cmprs_buf,
                                 u32 *p_cmprs_len, const qboot_algo_ops_t *algo_ops, const qbt_cmprs_ctx_t *cmprs_ctx)
{
    qbt_write_ctx_t write_ctx = { ctx, raw_pos };
    return qbt_decompress_with_consumer(algo_ops, cmprs_buf, p_cmprs_len, out_buf, QBOOT_BUF_SIZE, remaining,
                                        qbt_write_chunk_consumer, &write_ctx, cmprs_ctx);
}

/**
 * @brief Progress callback for firmware release.
 *
 * @param ctx       Unused context.
 * @param raw_pos   Current raw output offset (bytes).
 * @param raw_total Total raw size to be produced.
 */
static void qbt_release_progress(void *ctx, u32 raw_pos, u32 raw_total)
{
    RT_UNUSED(ctx);
    rt_kprintf("\b\b\b%02d%%", (raw_pos * 100 / raw_total));
}

/**
 * @brief Release firmware package to the destination partition.
 *
 * @param dst_handle Destination handle (partition/file).
 * @param dst_size   Destination total size.
 * @param dst_name   Destination name.
 * @param src_handle Source package handle.
 * @param src_name   Source name (unused, retained for signature compatibility).
 * @param fw_info    Firmware info header.
 *
 * @return true on success, false on error.
 */
static bool qbt_fw_release(void *dst_handle, size_t dst_size, const char *dst_name, void *src_handle, const char *src_name, fw_info_t *fw_info)
{
    u16 algo_id;
    const qboot_algo_ops_t *algo_ops = qbt_fw_get_algo_ops(fw_info, &algo_id);

    RT_UNUSED(src_name);

    if (algo_ops == RT_NULL)
    {
        LOG_E("Qboot release firmware fail. algo 0x%04X not registered.", algo_id);
        return false;
    }

    if (!qbt_fw_algo_init(algo_ops))
    {
        LOG_E("Qboot release firmware fail. algo init fail");
        return (false);
    }

#ifdef QBOOT_USING_HPATCHLITE
    if (algo_ops->algo_id == QBOOT_ALGO_CMPRS_HPATCHLITE)
    {
        if (qbt_hpatchlite_release_from_part((fal_partition_t)src_handle, (fal_partition_t)dst_handle, fw_info->pkg_size, fw_info->raw_size, sizeof(fw_info_t)) == true)
        {
            goto done;
        }
        else
        {
            return (false);
        }
    }
#endif
    rt_kprintf("Start erase partition %s ...\n", dst_name);
    if ((_header_io_ops->erase(dst_handle, 0, fw_info->raw_size) != RT_EOK) || (_header_io_ops->erase(dst_handle, dst_size - sizeof(fw_info_t), sizeof(fw_info_t)) != RT_EOK))
    {
        qbt_fw_algo_deinit(algo_ops);
        LOG_E("Qboot release firmware fail. erase %s error.", dst_name);
        return (false);
    }

    rt_kprintf("Start release firmware to %s ...     ", dst_name);
    qbt_stream_cfg_t stream_cfg = {
        .src_handle = src_handle,
        .fw_info = fw_info,
        .algo_ops = algo_ops,
        .cmprs_buf = g_cmprs_buf,
        .out_buf = g_decmprs_buf,
        .crypt_buf = g_crypt_buf,
    };

    if (!qbt_fw_stream_process(&stream_cfg, qbt_stream_write_proc, dst_handle, qbt_release_progress, RT_NULL, QBT_STREAM_WRITE))
    {
        qbt_fw_algo_deinit(algo_ops);
        LOG_E("Qboot release firmware fail. stream process to %s fail.", dst_name);
        return (false);
    }
    rt_kprintf("\n");

done:
#endif
    qbt_fw_algo_deinit(algo_ops);
    if (!qbt_fw_info_write(dst_handle, dst_size, fw_info, true))
    {
        LOG_E("Qboot release firmware fail. write firmware to %s fail.", dst_name);
        return (false);
    }

    return (true);
}

static bool qbt_dest_part_verify(void *handle, size_t part_len, const char *name)
{
    if (!qbt_fw_info_read(handle, part_len, &fw_info, true))
    {
        LOG_E("Qboot verify fail, read firmware from %s partition", name);
        return (false);
    }

    if (!qbt_fw_info_check(&fw_info))
    {
        LOG_E("Qboot verify fail. firmware infomation check fail.");
        return (false);
    }

    switch (fw_info.algo2 & QBOOT_ALGO2_VERIFY_MASK)
    {
    case QBOOT_ALGO2_VERIFY_CRC:
        if (!qbt_fw_crc_check(handle, name, 0, fw_info.raw_size, fw_info.raw_crc))
        {
            return (false);
        }
        break;

    default:
        break;
    }

    return (true);
}

static bool qbt_fw_check(void *fw_handle, size_t part_len, const char *name, fw_info_t *fw_info, bool output_log)
{
    if (!qbt_fw_info_read(fw_handle, part_len, fw_info, false))
    {
        if (output_log)
            LOG_E("Qboot firmware check fail. partition \"%s\" read fail.", name);
        return (false);
    }

    if (!qbt_fw_info_check(fw_info))
    {
        if (output_log)
            LOG_E("Qboot firmware check fail. partition \"%s\" infomation check fail.", name);
        return (false);
    }

    if (!qbt_fw_crc_check(fw_handle, name, sizeof(fw_info_t), fw_info->pkg_size, fw_info->pkg_crc))
    {
        if (output_log)
            LOG_E("Qboot firmware check fail. partition \"%s\" body check fail.", name);
        return (false);
    }

#ifdef QBOOT_USING_APP_CHECK
    if ((fw_info->algo2 & QBOOT_ALGO2_VERIFY_MASK) == QBOOT_ALGO2_VERIFY_CRC)
    {
        if (!qbt_app_crc_check(fw_handle, name, fw_info))
        {
            if (output_log)
                LOG_E("Qboot firmware check fail. partition \"%s\" app check fail.", name);
            return (false);
        }
    }
#endif

    if (output_log)
        LOG_D("Qboot partition \"%s\" firmware check success.", name);

    return (true);
}

static bool qbt_fw_update(void *dst_handle, size_t dst_size, const char *dst_name, void *src_handle, const char *src_name, fw_info_t *fw_info)
{
    bool rst;

#ifdef QBOOT_USING_STATUS_LED
    qled_set_blink(QBOOT_STATUS_LED_PIN, 50, 50);
#endif

    rst = qbt_fw_release(dst_handle, dst_size, dst_name, src_handle, src_name, fw_info);

#ifdef QBOOT_USING_STATUS_LED
    qled_set_blink(QBOOT_STATUS_LED_PIN, 50, 450);
#endif

    if (!rst)
    {
        LOG_E("Qboot firmware update fail. firmware release fail.");
        return (false);
    }

    if (!qbt_dest_part_verify(dst_handle, dst_size, dst_name))
    {
        LOG_E("Qboot firmware update fail. destination partition verify fail.");
        return (false);
    }

    LOG_I("Qboot firmware update success.");
    return (true);
}

#if 0
__WEAK void qbt_jump_to_app(void)
{
    typedef void (*app_func_t)(void);
    u32 app_addr = QBOOT_APP_ADDR;
    u32 stk_addr = *((__IO uint32_t *)app_addr);
    app_func_t app_func = (app_func_t)(*((__IO uint32_t *)(app_addr + 4)));

    if ((((u32)app_func & 0xff000000) != 0x08000000) || ((stk_addr & 0x2ff00000) != 0x20000000))
    {
        LOG_E("No legitimate application.");
        return;
    }

    rt_kprintf("Jump to application running ... \n");
    rt_thread_mdelay(200);
    
    __disable_irq();
    HAL_DeInit();
    HAL_RCC_DeInit();
    
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    for(int i=0; i<128; i++)
    {
        HAL_NVIC_DisableIRQ(i);
        HAL_NVIC_ClearPendingIRQ(i);
    }
    
    __set_CONTROL(0);
    __set_MSP(stk_addr);
    
    app_func();//Jump to application running
    
    LOG_E("Qboot jump to application fail.");
}
#else
extern void qbt_jump_to_app(void);
#endif

#ifdef QBOOT_USING_STATUS_LED
static void qbt_status_led_init(void)
{
    qled_add(QBOOT_STATUS_LED_PIN, QBOOT_STATUS_LED_LEVEL);
    qled_set_blink(QBOOT_STATUS_LED_PIN, 50, 450);
}
#endif

#ifdef QBOOT_USING_FACTORY_KEY
static bool qbt_factory_key_check(void)
{
    bool rst = true;

#if (QBOOT_FACTORY_KEY_LEVEL)//press level is high
    rt_pin_mode(QBOOT_FACTORY_KEY_PIN, PIN_MODE_INPUT_PULLDOWN);
#else
    rt_pin_mode(QBOOT_FACTORY_KEY_PIN, PIN_MODE_INPUT_PULLUP);
#endif

    rt_thread_mdelay(500);

#ifdef QBOOT_USING_STATUS_LED
    qled_set_blink(QBOOT_STATUS_LED_PIN, 50, 200);
#endif

    for (int i = 0; i < ((QBOOT_FACTORY_KEY_CHK_TMO * 10) + 1); i++)
    {
        rt_thread_mdelay(100);
        if (rt_pin_read(QBOOT_FACTORY_KEY_PIN) != QBOOT_FACTORY_KEY_LEVEL)
        {
            rst = false;
            break;
        }
    }

#ifdef QBOOT_USING_STATUS_LED
    qled_set_blink(QBOOT_STATUS_LED_PIN, 50, 450);
#endif

    return (rst);
}
#endif

#ifdef QBOOT_USING_SHELL
static rt_sem_t qbt_shell_sem = NULL;
static rt_device_t qbt_shell_dev = NULL;

static void qbt_close_sys_shell(void)
{
    rt_thread_t thread = rt_thread_find(FINSH_THREAD_NAME);
    if (thread == NULL)
    {
        return;
    }
    if (rt_object_is_systemobject((rt_object_t)thread))
    {
        rt_thread_detach(thread);
    }
    else
    {
        rt_thread_delete(thread);
    }

    rt_sem_t sem = (rt_sem_t)rt_object_find("shrx", RT_Object_Class_Semaphore);
    if (sem == NULL)
    {
        return;
    }
    if (rt_object_is_systemobject((rt_object_t)sem))
    {
        rt_sem_detach(sem);
    }
    else
    {
        rt_sem_delete(sem);
    }
}

static void qbt_open_sys_shell(void)
{
    rt_thread_t thread = rt_thread_find(FINSH_THREAD_NAME);
    if (thread == NULL)
    {
#ifdef QBOOT_USING_STATUS_LED
        qled_set_blink(QBOOT_STATUS_LED_PIN, 50, 950);
#endif
        finsh_set_prompt(QBOOT_SHELL_PROMPT);
        extern int finsh_system_init(void);
        finsh_system_init();
    }
}

static rt_err_t qbt_shell_rx_ind(rt_device_t dev, rt_size_t size)
{
    rt_sem_release(qbt_shell_sem);
    return (RT_EOK);
}

static bool qbt_shell_init(const char *shell_dev_name)
{
    rt_device_t dev = rt_device_find(shell_dev_name);
    if (dev == NULL)
    {
        LOG_E("Qboot shell initialize fail. no find device: %s.", shell_dev_name);
        return (false);
    }

    if (qbt_shell_sem == NULL)
    {
        qbt_shell_sem = rt_sem_create("qboot_shell", 0, RT_IPC_FLAG_FIFO);
        if (qbt_shell_sem == NULL)
        {
            LOG_E("Qboot shell initialize fail. sem create fail.");
            return (false);
        }
    }

    if (dev == qbt_shell_dev)
    {
        return (true);
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_STREAM) != RT_EOK)
    {
        LOG_E("Qboot shell initialize fail. device %s open failed.", shell_dev_name);
        return (false);
    }

    if (qbt_shell_dev != RT_NULL)
    {
        rt_device_close(qbt_shell_dev);
        rt_device_set_rx_indicate(qbt_shell_dev, NULL);
    }

    qbt_shell_dev = dev;
    rt_device_set_rx_indicate(dev, qbt_shell_rx_ind);

    LOG_D("shell device %s open success.", shell_dev_name);

    return (true);
}

static bool qbt_shell_key_check(void)
{
    char ch;
    rt_tick_t tick_start = rt_tick_get();
    rt_tick_t tmo = rt_tick_from_millisecond(QBOOT_SHELL_KEY_CHK_TMO * 1000);

    while (rt_tick_get() - tick_start < tmo)
    {
        if (rt_sem_take(qbt_shell_sem, 100) != RT_EOK)
        {
            continue;
        }
        if (rt_device_read(qbt_shell_dev, -1, &ch, 1) > 0)
        {
            if (ch == 0x0d)
            {
                return (true);
            }
            continue;
        }
    }

    return (false);
}

static bool qbt_startup_shell(bool wait_press_key)
{
    if (!qbt_shell_init(RT_CONSOLE_DEVICE_NAME))
    {
        LOG_E("Qboot initialize shell fail.");
        return (false);
    }

    if (wait_press_key)
    {
        bool rst;
        rt_kprintf("Press [Enter] key into shell in %d s : ", QBOOT_SHELL_KEY_CHK_TMO);
        rst = qbt_shell_key_check();
        rt_kprintf("\n");
        if (!rst)
        {
            return (false);
        }
    }

    qbt_open_sys_shell();

    return (true);
}
#endif

static void qbt_show_msg(void)
{
    //rt_kprintf("\033[2J\033[0;0H"); //clear screen
    rt_kprintf("\n");
    rt_kprintf("Qboot startup ... \n");
    rt_kprintf("Qboot version   : %s \n", QBOOT_VER_MSG);

#ifdef QBOOT_USING_PRODUCT_INFO
    rt_kprintf("Product name    : %s \n", QBOOT_PRODUCT_NAME);
    rt_kprintf("Product version : %s \n", QBOOT_PRODUCT_VER);
    rt_kprintf("Product mcu     : %s \n", QBOOT_PRODUCT_MCU);
#endif
}

static bool qbt_app_resume_from(const char *src_part_name)
{
    void *src_handle = RT_NULL;
    void *dst_handle = RT_NULL;
    size_t src_size = 0;
    size_t dst_size = 0;
    bool rst = false;

    if (!qbt_target_open(src_part_name, &src_handle, &src_size))
    {
        LOG_E("Qboot resume fail. partition \"%s\" is not exist.", src_part_name);
        return (false);
    }

    if (!qbt_target_open(QBOOT_APP_PART_NAME, &dst_handle, &dst_size))
    {
        LOG_E("Qboot resume fail from %s.", src_part_name);
        LOG_E("Destination partition %s is not exist.", QBOOT_APP_PART_NAME);
        qbt_target_close(src_handle);
        return (false);
    }

    if (!qbt_fw_check(src_handle, src_size, src_part_name, &fw_info, true))
    {
        goto exit;
    }

#ifdef QBOOT_USING_PRODUCT_CODE
    if (strcmp((char *)fw_info.prod_code, QBOOT_PRODUCT_CODE) != 0)
    {
        LOG_E("Qboot resume fail from %s.", src_part_name);
        LOG_E("The product code error. ");
        goto exit;
    }
#endif

    if (strcmp((char *)fw_info.part_name, QBOOT_APP_PART_NAME) != 0)
    {
        LOG_E("Qboot resume fail from %s.", src_part_name);
        LOG_E("The firmware of %s partition is not application. fw_info.part_name(%s) != %s", src_part_name, fw_info.part_name, QBOOT_APP_PART_NAME);
        goto exit;
    }

    if (!qbt_fw_update(dst_handle, dst_size, QBOOT_APP_PART_NAME, src_handle, src_part_name, &fw_info))
    {
        goto exit;
    }

    LOG_I("Qboot resume success from %s.", src_part_name);
    rst = true;

exit:
    qbt_target_close(src_handle);
    qbt_target_close(dst_handle);
    return rst;
}

static bool qbt_release_from_part(const char *part_name, bool check_sign)
{
    void *src_handle = RT_NULL;
    void *dst_handle = RT_NULL;
    size_t src_size = 0;
    size_t dst_size = 0;
    bool rst = false;

    if (!qbt_target_open(part_name, &src_handle, &src_size))
    {
        LOG_E("Qboot release fail. partition \"%s\" is not exist.", part_name);
        return (false);
    }

    if (!qbt_fw_check(src_handle, src_size, part_name, &fw_info, true))
    {
        goto exit;
    }

#ifdef QBOOT_USING_PRODUCT_CODE
    if (strcmp((char *)fw_info.prod_code, QBOOT_PRODUCT_CODE) != 0)
    {
        LOG_E("The product code error.");
        goto exit;
    }
#endif

    if (check_sign)
    {
        if (qbt_release_sign_check(src_handle, part_name, &fw_info))//not need release
        {
            rst = true;
            goto exit;
        }
    }

    if (!qbt_target_open((char *)fw_info.part_name, &dst_handle, &dst_size))
    {
        LOG_E("The destination %s partition is not exist.", fw_info.part_name);
        goto exit;
    }

    if (!qbt_fw_update(dst_handle, dst_size, (char *)fw_info.part_name, src_handle, part_name, &fw_info))
    {
        goto exit;
    }

    if (!qbt_release_sign_check(src_handle, part_name, &fw_info))
    {
        qbt_release_sign_write(src_handle, part_name, &fw_info);
    }

    LOG_I("Release firmware success from %s to %s.", part_name, fw_info.part_name);
    rst = true;

exit:
    if (dst_handle != RT_NULL)
    {
        qbt_target_close(dst_handle);
    }
    if (src_handle != RT_NULL)
    {
        qbt_target_close(src_handle);
    }
    return rst;
}

static void qbt_thread_entry(void *params)
{
#define QBOOT_REBOOT_DELAY_MS 5000

#ifdef QBOOT_USING_SHELL
    rt_thread_mdelay(2);
    qbt_close_sys_shell();
#endif

    qbt_show_msg();

#ifdef QBOOT_USING_STATUS_LED
    qbt_status_led_init();
#endif

    if (qboot_register_storage_ops() != RT_EOK)
    {
        LOG_E("Qboot register storage ops fail.");
#ifdef QBOOT_USING_SHELL
        if (qbt_startup_shell(false))
        {
            return;
        }
#endif
        LOG_I("Qboot will reboot after %d ms.", QBOOT_REBOOT_DELAY_MS);
        rt_thread_mdelay(QBOOT_REBOOT_DELAY_MS);
        rt_hw_cpu_reset();
    }

#ifdef QBOOT_USING_FACTORY_KEY
    if (qbt_factory_key_check())
    {
        if (qbt_app_resume_from(QBOOT_FACTORY_PART_NAME))
        {
            qbt_jump_to_app();
        }
    }
#endif

#ifdef QBOOT_USING_SHELL
    if (qbt_startup_shell(true))
    {
        return;
    }
#endif

    qbt_release_from_part(QBOOT_DOWNLOAD_PART_NAME, true);
    qbt_jump_to_app();

    LOG_I("Try resume application from %s", QBOOT_DOWNLOAD_PART_NAME);
    if (qbt_app_resume_from(QBOOT_DOWNLOAD_PART_NAME))
    {
        qbt_jump_to_app();
    }

    LOG_I("Try resume application from %s", QBOOT_FACTORY_PART_NAME);
    if (qbt_app_resume_from(QBOOT_FACTORY_PART_NAME))
    {
        qbt_jump_to_app();
    }

#ifdef QBOOT_USING_SHELL
    if (qbt_startup_shell(false))
    {
        return;
    }
#endif

    LOG_I("Qboot will reboot after %d ms.", QBOOT_REBOOT_DELAY_MS);
    rt_thread_mdelay(QBOOT_REBOOT_DELAY_MS);
    rt_hw_cpu_reset();
}

static int qbt_startup(void)
{
#ifdef QBOOT_ALGO_CRYPT_NONE
    extern int qbt_algo_none_register(void);
    QBT_REGISTER_ALGO(qbt_algo_none_register());
#endif // QBOOT_ALGO_CRYPT_NONE
#ifdef QBOOT_USING_GZIP
    QBT_REGISTER_ALGO(qbt_algo_gzip_register());
#endif // QBOOT_USING_GZIP
#ifdef QBOOT_USING_QUICKLZ
    QBT_REGISTER_ALGO(qbt_algo_quicklz_register());
#endif // QBOOT_USING_QUICKLZ

    rt_thread_t tid = rt_thread_create("Qboot", qbt_thread_entry, NULL, QBOOT_THREAD_STACK_SIZE, QBOOT_THREAD_PRIO, 20);
    if (tid == NULL)
    {
        LOG_E("Qboot thread create fail.");
        return -RT_ERROR;
    }

    rt_thread_startup(tid);

    return RT_EOK;
}
INIT_APP_EXPORT(qbt_startup);

#ifdef QBOOT_USING_SHELL
static bool qbt_fw_clone(void *dst_handle, const char *dst_name, void *src_handle, const char *src_name, u32 fw_pkg_size)
{
    u32 pos = 0;

    rt_kprintf("Erasing %s partition ... \n", dst_name);
    if (_header_io_ops->erase(dst_handle, 0, fw_pkg_size) != RT_EOK)
    {
        LOG_E("Qboot clone firmware fail. erase %s error.", dst_name);
        return (false);
    }

    rt_kprintf("Cloning firmware from %s to %s ...    ", src_name, dst_name);
    while (pos < fw_pkg_size)
    {
        int read_len = QBOOT_BUF_SIZE;
        int remain_len = fw_pkg_size - pos;
        if (read_len > remain_len)
        {
            read_len = remain_len;
        }
        if (_header_io_ops->read(src_handle, pos, g_cmprs_buf, read_len) != RT_EOK)
        {
            LOG_E("Qboot clone firmware fail. read error, part = %s, addr = %08X, length = %d", src_name, pos, read_len);
            return (false);
        }
        if (_header_io_ops->write(dst_handle, pos, g_cmprs_buf, read_len) != RT_EOK)
        {
            LOG_E("Qboot clone firmware fail. write error, part = %s, addr = %08X, length = %d", dst_name, pos, read_len);
            return (false);
        }
        pos += read_len;
        rt_kprintf("\b\b\b%02d%%", (pos * 100 / fw_pkg_size));
    }
    rt_kprintf("\n");

    return (true);
}
static void qbt_fw_info_show(const char *part_name)
{
    char str[20];
    void *handle = RT_NULL;
    size_t part_size = 0;

    if (!qbt_target_open(part_name, &handle, &part_size))
    {
        return;
    }

    if (!qbt_fw_check(handle, part_size, part_name, &fw_info, false))
    {
        qbt_target_close(handle);
        return;
    }

    rt_memset(str, 0x0, sizeof(str));
    switch (fw_info.algo & QBOOT_ALGO_CRYPT_MASK)
    {
    case QBOOT_ALGO_CRYPT_NONE:
        strcpy(str, "NONE");
        break;
    case QBOOT_ALGO_CRYPT_XOR:
        strcpy(str, "XOR");
        break;
    case QBOOT_ALGO_CRYPT_AES:
        strcpy(str, "AES");
        break;
    default:
        strcpy(str, "UNKNOW");
        break;
    }
    switch (fw_info.algo & QBOOT_ALGO_CMPRS_MASK)
    {
    case QBOOT_ALGO_CMPRS_NONE:
        strcpy(str + strlen(str), " && NONE");
        break;
    case QBOOT_ALGO_CMPRS_GZIP:
        strcpy(str + strlen(str), " && GZIP");
        break;
    case QBOOT_ALGO_CMPRS_QUICKLZ:
        strcpy(str + strlen(str), " && QUCKLZ");
        break;
    case QBOOT_ALGO_CMPRS_FASTLZ:
        strcpy(str + strlen(str), " && FASTLZ");
        break;
    case QBOOT_ALGO_CMPRS_HPATCHLITE:
        strcpy(str + strlen(str), " && HPATCHLITE");
        break;
    default:
        strcpy(str + strlen(str), " && UNKNOW");
        break;
    }
    rt_kprintf("==== Firmware infomation of %s partition ====\n", part_name);
    rt_kprintf("| Product code          | %*.s |\n", 20, fw_info.prod_code);
    rt_kprintf("| Algorithm mode        | %*.s |\n", 20, str);
    rt_kprintf("| Destition partition   | %*.s |\n", 20, fw_info.part_name);
    rt_kprintf("| Version               | %*.s |\n", 20, fw_info.fw_ver);
    rt_kprintf("| Package size          | %20d |\n", fw_info.pkg_size);
    rt_kprintf("| Raw code size         | %20d |\n", fw_info.raw_size);
    rt_kprintf("| Package crc           | %20X |\n", fw_info.pkg_crc);
    rt_kprintf("| Raw code verify       | %20X |\n", fw_info.raw_crc);
    rt_kprintf("| Header crc            | %20X |\n", fw_info.hdr_crc);
    rt_kprintf("| Build timestamp       | %20d |\n", fw_info.time_stamp);
    rt_kprintf("\n");
    qbt_target_close(handle);
}
static bool qbt_fw_delete(void *handle, const char *name, u32 part_size)
{
    rt_kprintf("Erasing %s partition ... \n", name);
    if (_header_io_ops->erase(handle, 0, part_size) != RT_EOK)
    {
        LOG_E("Qboot delete firmware fail. erase %s error.", name);
        return (false);
    }

    rt_kprintf("Qboot delete firmware success.\n");

    return (true);
}
static void qbt_shell_cmd(rt_uint8_t argc, char **argv)
{
    const char *cmd_info[] = {
        "Usage:\n",
        "qboot probe                    - probe firmware packages\n",
        "qboot resume src_part          - resume application from src partiton\n",
        "qboot clone src_part dst_part  - clone src partition to dst partiton\n",
        "qboot release part             - release firmware from partiton\n",
        "qboot verify part              - verify released code of partition\n",
        "qboot del part                 - delete firmware of partiton\n",
        "qboot jump                     - jump to application\n",
        "\n"
    };

    if (argc < 2)
    {
        for (int i = 0; i < sizeof(cmd_info) / sizeof(char *); i++)
        {
            rt_kprintf(cmd_info[i]);
        }
        return;
    }

    if (strcmp(argv[1], "probe") == 0)
    {
        qbt_fw_info_show(QBOOT_DOWNLOAD_PART_NAME);
        qbt_fw_info_show(QBOOT_FACTORY_PART_NAME);
        return;
    }

    if (strcmp(argv[1], "resume") == 0)
    {
        if (argc < 3)
        {
            rt_kprintf(cmd_info[2]);
            return;
        }
        qbt_app_resume_from(argv[2]);

#ifdef QBOOT_USING_STATUS_LED
        qled_set_blink(QBOOT_STATUS_LED_PIN, 50, 950);
#endif

        return;
    }

    if (strcmp(argv[1], "clone") == 0)
    {
        char *src, *dst;
        void *src_handle = RT_NULL;
        void *dst_handle = RT_NULL;
        size_t src_size = 0;
        if (argc < 4)
        {
            rt_kprintf(cmd_info[3]);
            return;
        }
        src = argv[2];
        dst = argv[3];
        if (!qbt_target_open(dst, &dst_handle, NULL))
        {
            rt_kprintf("Desttition %s partition is not exist.\n", dst);
            return;
        }
        if (!qbt_target_open(src, &src_handle, &src_size))
        {
            rt_kprintf("Soure %s partition is not exist.\n", src);
            qbt_target_close(dst_handle);
            return;
        }

        if (!qbt_fw_check(src_handle, src_size, src, &fw_info, true))
        {
            rt_kprintf("Soure %s partition firmware error.\n", src);
            qbt_target_close(src_handle);
            qbt_target_close(dst_handle);
            return;
        }
        if (qbt_fw_clone(dst_handle, dst, src_handle, src, sizeof(fw_info_t) + fw_info.pkg_size))
        {
            rt_kprintf("Clone firmware success from %s to %s.\n", src, dst);
        }
        qbt_target_close(src_handle);
        qbt_target_close(dst_handle);
        return;
    }

    if (strcmp(argv[1], "release") == 0)
    {
        char *part_name;
        if (argc < 3)
        {
            rt_kprintf(cmd_info[4]);
            return;
        }
        part_name = argv[2];
        if (qbt_release_from_part(part_name, false))
        {
            rt_kprintf("Release firmware success from %s partition.\n", part_name);
        }
        else
        {
            rt_kprintf("Release firmware fail from %s partition.\n", part_name);
        }
        return;
    }

    if (strcmp(argv[1], "verify") == 0)
    {
        char *part_name;
        void *handle = RT_NULL;
        size_t part_size = 0;
        if (argc < 3)
        {
            rt_kprintf(cmd_info[5]);
            return;
        }
        part_name = argv[2];
        if (!qbt_target_open(part_name, &handle, &part_size))
        {
            rt_kprintf("%s partition is not exist.\n", part_name);
            return;
        }

        if (qbt_dest_part_verify(handle, part_size, part_name))
        {
            rt_kprintf("%s partition code verify ok.\n", part_name);
        }
        else
        {
            rt_kprintf("%s partition code verify error.\n", part_name);
        }
        qbt_target_close(handle);
        return;
    }

    if (strcmp(argv[1], "del") == 0)
    {
        char *part_name;
        void *handle = RT_NULL;
        size_t part_size = 0;

        if (argc < 3)
        {
            rt_kprintf(cmd_info[6]);
            return;
        }

        part_name = argv[2];
        if (!qbt_target_open(part_name, &handle, &part_size))
        {
            rt_kprintf("%s partition is not exist.\n", part_name);
            return;
        }

        if (!qbt_fw_check(handle, part_size, part_name, &fw_info, false))
        {
            rt_kprintf("%s partition without firmware.\n", part_name);
            qbt_target_close(handle);
            return;
        }

        qbt_fw_delete(handle, part_name, fw_info.pkg_size);
        qbt_target_close(handle);

        return;
    }

    if (strcmp(argv[1], "jump") == 0)
    {
        qbt_jump_to_app();
        return;
    }

    rt_kprintf("No supported command.\n");
}
MSH_CMD_EXPORT_ALIAS(qbt_shell_cmd, qboot, Quick bootloader test commands);
#endif
