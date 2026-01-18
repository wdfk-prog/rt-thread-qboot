/**
 * @file qboot_hpatchlite.c
 * @brief In-place OTA update implementation using HPatchLite.
 * @author wdfk-prog ()
 * @version 1.4
 * @date 2025-09-29
 * 
 * @copyright Copyright (c) 2025  
 * 
 * @note Supports both FLASH swap and RAM buffer strategies via Kconfig.
 *       This implementation is designed for in-place updates where the old firmware
 *       partition is directly overwritten to become the new firmware partition.
 * 
 * @par Modification log:
 * Date       Version Author      Description
 * 2025-09-25 1.0     wdfk-prog     First version with flash swap.
 * 2025-09-26 1.1     wdfk-prog     Refactored to support both RAM and FLASH buffer strategies.
 * 2025-09-27 1.2     wdfk-prog     Refined with Kconfig integration, ULOG, Doxygen, and bug fixes.
 * 2025-09-29 1.4     wdfk-prog     Completed RAM buffer strategy based on user's unified logic.
 */
#include <qboot.h>

// Only compile this file if HPatchLite is enabled
#ifdef QBOOT_USING_HPATCHLITE

#include "hpatch_impl.h"

// Define ULOG tag and level
#define DBG_TAG "qboot.hpatch"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifndef QBOOT_HPATCH_ERASE_ALIGN_DEFAULT
#define QBOOT_HPATCH_ERASE_ALIGN_DEFAULT 4096
#endif

/**
 * @brief Instance structure to hold state during the patching process.
 *        Conditionally includes members based on the selected buffer strategy.
 */
typedef struct hpatchi_instance_t
{
    hpatchi_listener_t parent;              /**< HPatchLite listener callbacks */
    int patch_file_offset;                  /**< Starting offset of the patch data within the patch partition */
    int patch_file_len;                     /**< Total length of the patch data */
    int patch_read_pos;                     /**< Current read position within the patch data stream */
    int newer_file_len;                     /**< Expected final length of the new firmware */
    int newer_write_pos;                    /**< Logical current write position in the new firmware */
    int progress_percent;                   /**< Last reported progress percentage to avoid duplicate logs */

    void *patch_part;                       /**< Patch data handle */
    void *old_part;                         /**< Old firmware handle (the one being updated) */
    const char *patch_name;                 /**< Patch data name (for logs) */
    const char *old_name;                   /**< Old firmware name (for logs) */

#if defined(QBOOT_HPATCH_USE_FLASH_SWAP)
    // --- Members for FLASH swap strategy ---
    void *swap_part;                        /**< Swap buffer handle */
    int swap_offset;                        /**< Starting offset of the usable area within the swap partition */
    int swap_write_pos;                     /**< Current write position within the swap buffer area */
    int swap_size;                          /**< Total usable size of the swap buffer area */
#elif defined(QBOOT_HPATCH_USE_RAM_BUFFER)
    // --- Members for RAM buffer strategy ---
    uint8_t *swap_buffer;                   /**< Pointer to the RAM buffer */
    int swap_buffer_size;                   /**< Size of the RAM buffer */
    int swap_write_pos;                     /**< Current write position within the RAM buffer */
#endif
    int committed_len;                      /**< Total length of data already committed from RAM/Flash to the old partition */

} hpatchi_instance_t;

static rt_uint32_t qbt_hpatch_get_erase_align(void *handle)
{
    rt_uint32_t align = QBOOT_HPATCH_ERASE_ALIGN_DEFAULT;

    if (_header_io_ops && _header_io_ops->ioctl)
    {
        if (_header_io_ops->ioctl(handle, QBOOT_IO_CMD_GET_ERASE_ALIGN, &align) != RT_EOK)
        {
            align = QBOOT_HPATCH_ERASE_ALIGN_DEFAULT;
        }
    }
    if (align == 0)
    {
        align = QBOOT_HPATCH_ERASE_ALIGN_DEFAULT;
    }
    return align;
}

/**
 * @brief Placeholder decompressor for HPatchLite.
 */
static rt_err_t qbt_algo_hpatchlite_decompress(const qbt_stream_buf_t *buf, qbt_stream_status_t *out, const qbt_stream_ctx_t *ctx)
{
    RT_UNUSED(ctx);
    if (out != RT_NULL)
    {
        out->consumed = 0;
        out->produced = 0;
        out->remaining_in = (buf != RT_NULL) ? buf->in_len : 0;
    }
    LOG_E("HPatchLite placeholder decompress called; use patch flow.");
    return -RT_ERROR;
}

static const qboot_cmprs_ops_t qbt_algo_hpatchlite_cmprs_ops = {
    .cmprs_name = "HPATCHLITE",
    .cmprs_id = QBOOT_ALGO_CMPRS_HPATCHLITE,
    .init = RT_NULL,
    .decompress = qbt_algo_hpatchlite_decompress,
    .deinit = RT_NULL,
};

rt_err_t qbt_algo_hpatchlite_register(void)
{
    return qboot_cmprs_register(&qbt_algo_hpatchlite_cmprs_ops);
}


// -----------------------------------------------------------------------------
// Common Listener Callbacks
// -----------------------------------------------------------------------------
/**
 * @brief HPatchLite listener: Reads patch data as a stream.
 * @param input_stream  Handle to the instance, used to track stream position.
 * @param data          Pointer to the buffer to fill with patch data.
 * @param size          Pointer to the requested data size (input) and actual read size (output).
 * @return hpi_TRUE on success, hpi_FALSE on failure.
 */
static hpi_BOOL _do_read_patch(hpi_TInputStreamHandle input_stream, hpi_byte *data, hpi_size_t *size)
{
    hpatchi_instance_t *instance = (hpatchi_instance_t *)input_stream;

    //If it has already been read or out of bounds, return 0 bytes directly.
    if (instance->patch_read_pos >= instance->patch_file_len)
    {
        *size = 0;
        return hpi_TRUE;
    }

    // Calculate the remaining length and adjust the length of this read
    hpi_size_t remaining_len = instance->patch_file_len - instance->patch_read_pos;
    if (*size > remaining_len)
    {
        *size = remaining_len;
    }

    //If *size is adjusted to 0, it indicates that reading is complete.
    if (*size == 0)
    {
        return hpi_TRUE;
    }

    if (_header_io_ops->read(instance->patch_part, instance->patch_file_offset + instance->patch_read_pos, data, *size) != RT_EOK)
    {
        LOG_E("Failed to read patch data from '%s'.", instance->patch_name);
        *size = 0;
        return hpi_FALSE;
    }
    instance->patch_read_pos += *size;
    return hpi_TRUE;
}

/**
 * @brief HPatchLite listener: Reads old data directly from the old partition.
 *        This function's behavior is IDENTICAL for both buffer strategies.
 * @param listener  Pointer to the listener instance.
 * @param addr      Absolute address within the old firmware to read from.
 * @param data      Pointer to the buffer to fill with old data.
 * @param size      Number of bytes to read.
 * @return hpi_TRUE on success, hpi_FALSE on failure.
 */
static hpi_BOOL _do_read_old(struct hpatchi_listener_t *listener, hpi_pos_t addr, hpi_byte *data, hpi_size_t size)
{
    hpatchi_instance_t *instance = (hpatchi_instance_t *)listener;
    if (_header_io_ops->read(instance->old_part, addr, data, size) != RT_EOK)
    {
        LOG_E("Failed to read old data from '%s'.", instance->old_name);
        return hpi_FALSE;
    }
    return hpi_TRUE;
}


// -----------------------------------------------------------------------------
// FLASH Swap Strategy Implementation
// -----------------------------------------------------------------------------
#if defined(QBOOT_HPATCH_USE_FLASH_SWAP)

/**
 * @brief Copies data from one FAL partition to another using a temporary RAM buffer.
 * @param src_part      Source partition handle.
 * @param src_offset    Starting offset in the source partition.
 * @param dst_part      Destination partition handle.
 * @param dst_offset    Starting offset in the destination partition.
 * @param size          Total number of bytes to copy.
 * @return 0 on success, non-zero on failure.
 */
static int flash_to_flash_copy(void *src_part, int src_offset, const char *src_name, void *dst_part, int dst_offset, const char *dst_name, int size)
{
    uint8_t *buffer = RT_NULL;
    int result = 0;
    int remaining_len = size;
    int current_src_offset = src_offset;
    int current_dst_offset = dst_offset;

    buffer = rt_malloc(QBOOT_HPATCH_COPY_BUFFER_SIZE);
    if (buffer == RT_NULL)
    {
        LOG_E("Failed to malloc %d bytes for flash copy buffer!", QBOOT_HPATCH_COPY_BUFFER_SIZE);
        return -1;
    }

    LOG_D("Starting flash-to-flash copy of %d bytes: '%s' -> '%s'...", size, src_name, dst_name);

    while (remaining_len > 0)
    {
        int chunk_size = (remaining_len > QBOOT_HPATCH_COPY_BUFFER_SIZE) ? QBOOT_HPATCH_COPY_BUFFER_SIZE : remaining_len;

        if (_header_io_ops->read(src_part, current_src_offset, buffer, chunk_size) != RT_EOK)
        {
            LOG_E("Flash copy failed at read step from '%s'.", src_name);
            result = -1;
            break;
        }

        if (_header_io_ops->write(dst_part, current_dst_offset, buffer, chunk_size) != RT_EOK)
        {
            LOG_E("Flash copy failed at write step to '%s'.", dst_name);
            result = -2;
            break;
        }

        current_src_offset += chunk_size;
        current_dst_offset += chunk_size;
        remaining_len -= chunk_size;
    }

    rt_free(buffer);

    if (result == 0)
    {
        LOG_D("Flash copy successful: '%s' -> '%s'.", src_name, dst_name);
    }
    return result;
}

/**
 * @brief Commits the buffered data from the swap partition to the old partition.
 *        This involves erasing the target area on the old partition, copying data,
 *        and then erasing the swap partition for the next round.
 * @param instance Pointer to the patch instance.
 * @return hpi_TRUE on success, hpi_FALSE on failure.
 */
static hpi_BOOL _commit_swap_to_old_flash(hpatchi_instance_t *instance)
{
    if (instance->swap_write_pos == 0)
    {
        return hpi_TRUE;
    }

    // Print a newline if the progress bar was previously shown
    if (instance->progress_percent >= 0 && instance->progress_percent < 100)
    {
        rt_kprintf("\n");
    }
    LOG_I("\nCommitting %d bytes from '%s' to '%s' partition...", instance->swap_write_pos, QBOOT_HPATCH_SWAP_PART_NAME, instance->old_name);

    LOG_D("Erasing '%s' partition from offset %d...", instance->old_name, instance->committed_len);
    if (_header_io_ops->erase(instance->old_part, instance->committed_len, instance->swap_write_pos) != RT_EOK)
    {
        LOG_E("Failed to erase '%s' partition at offset %d.", instance->old_name, instance->committed_len);
        return hpi_FALSE;
    }

    if (flash_to_flash_copy(instance->swap_part, instance->swap_offset, QBOOT_HPATCH_SWAP_PART_NAME, instance->old_part, instance->committed_len, instance->old_name, instance->swap_write_pos) < 0)
    {
        LOG_E("Failed to copy from '%s' to '%s' partition.", QBOOT_HPATCH_SWAP_PART_NAME, instance->old_name);
        return hpi_FALSE;
    }

    LOG_D("Erasing swap partition '%s' for next round...", QBOOT_HPATCH_SWAP_PART_NAME);
    if (_header_io_ops->erase(instance->swap_part, instance->swap_offset, instance->swap_size) != RT_EOK)
    {
        LOG_W("Failed to erase swap partition '%s', may affect next write cycle.", QBOOT_HPATCH_SWAP_PART_NAME);
        return hpi_FALSE;
    }

    instance->committed_len += instance->swap_write_pos;
    instance->swap_write_pos = 0;
    LOG_I("\nCommit successful. Total committed: %d bytes.", instance->committed_len);
    return hpi_TRUE;
}

/**
 * @brief HPatchLite listener: Writes new data by buffering it in the swap partition.
 *        Triggers a commit when the buffer is full.
 * @param listener  Pointer to the listener instance.
 * @param data      Pointer to the new data to be written.
 * @param size      Size of the new data.
 * @return hpi_TRUE on success, hpi_FALSE on failure.
 */
static hpi_BOOL _do_write_new_flash(struct hpatchi_listener_t *listener, const hpi_byte *data, hpi_size_t size)
{
    hpatchi_instance_t *instance = (hpatchi_instance_t *)listener;
    const hpi_byte *data_cur = data;
    hpi_size_t remain_size = size;

    // The amount of data written at one time may be very large, requiring processing in loops.
    while (remain_size > 0)
    {
        // Calculate the remaining space of the swap partition
        int swap_free_space = instance->swap_size - instance->swap_write_pos;
        // First, fill up the swap partition.
        if (swap_free_space < remain_size)
        {
            if (swap_free_space > 0)
            {
                if (_header_io_ops->write(instance->swap_part, instance->swap_offset + instance->swap_write_pos, (const uint8_t *)data_cur, swap_free_space) != RT_EOK)
                {
                    return hpi_FALSE;
                }
                else
                {
                    instance->swap_write_pos += swap_free_space;
                    data_cur += swap_free_space;
                    remain_size -= swap_free_space;
                }
            }
            //Execute submit operation
            if (!_commit_swap_to_old_flash(instance))
            {
                return hpi_FALSE;
            }
        }
        else
        {
            // Write all remaining data (which could be all the data or just a truncated portion) directly to the swap partition
            if (remain_size > 0)
            {
                if (_header_io_ops->write(instance->swap_part, instance->swap_offset + instance->swap_write_pos, (const uint8_t *)data_cur, remain_size) != RT_EOK)
                {
                    return hpi_FALSE;
                }
                else
                {
                    instance->swap_write_pos += remain_size;
                }
            }
            break;
        }
    }

    instance->newer_write_pos += size;

    int percent = instance->newer_write_pos * 100 / instance->newer_file_len;
    if (percent != instance->progress_percent && (percent % 5 == 0))
    {
        rt_kprintf("\rBuffering... %3d%%", percent);
        instance->progress_percent = percent;
        // Print a newline if the progress bar was previously shown
        if (percent >= 100)
        {
            rt_kprintf("\n");
        }
    }
    return hpi_TRUE;
}

#endif // QBOOT_HPATCH_USE_FLASH_SWAP


// -----------------------------------------------------------------------------
// RAM Buffer Strategy Implementation
// -----------------------------------------------------------------------------
#if defined(QBOOT_HPATCH_USE_RAM_BUFFER)

/**
 * @brief Commits the buffered data from the RAM buffer to the old partition.
 *        This is the core of the "write-buffer, commit-full" logic for RAM.
 * @param instance Pointer to the patch instance.
 * @return hpi_TRUE on success, hpi_FALSE on failure.
 */
static hpi_BOOL _commit_ram_to_old(hpatchi_instance_t *instance)
{
    if (instance->swap_write_pos == 0)
    {
        return hpi_TRUE;
    }

    // Print a newline if the progress bar was previously shown
    if (instance->progress_percent >= 0 && instance->progress_percent < 100)
    {
        rt_kprintf("\n");
    }
    LOG_I("\nCommitting %d bytes from RAM buffer to '%s' partition...", instance->swap_write_pos, instance->old_name);

    // 1. Erase the target area on the old partition
    LOG_D("Erasing '%s' partition from offset %d...", instance->old_name, instance->committed_len);
    if (_header_io_ops->erase(instance->old_part, instance->committed_len, instance->swap_write_pos) != RT_EOK)
    {
        LOG_E("Failed to erase '%s' partition at offset %d.", instance->old_name, instance->committed_len);
        return hpi_FALSE;
    }

    // 2. Write the entire RAM buffer content to the erased area
    if (_header_io_ops->write(instance->old_part, instance->committed_len, instance->swap_buffer, instance->swap_write_pos) != RT_EOK)
    {
        LOG_E("Failed to write from RAM buffer to '%s' partition.", instance->old_name);
        return hpi_FALSE;
    }

    // 3. Update state: only reset the write position, no need to clear RAM
    instance->committed_len += instance->swap_write_pos;
    instance->swap_write_pos = 0;
    LOG_I("\nCommit successful. Total committed: %d bytes.", instance->committed_len);
    return hpi_TRUE;
}

/**
 * @brief HPatchLite listener: Writes new data by buffering it in the RAM buffer.
 *        Triggers a commit when the buffer is full.
 * @param listener  Pointer to the listener instance.
 * @param data      Pointer to the new data to be written.
 * @param size      Size of the new data.
 * @return hpi_TRUE on success, hpi_FALSE on failure.
 */
static hpi_BOOL _do_write_new_ram(struct hpatchi_listener_t *listener, const hpi_byte *data, hpi_size_t size)
{
    hpatchi_instance_t *instance = (hpatchi_instance_t *)listener;
    const hpi_byte *data_cur = data;
    hpi_size_t remain_size = size;

    while (remain_size > 0)
    {
        int swap_free_space = instance->swap_buffer_size - instance->swap_write_pos;
        if (swap_free_space < remain_size)
        {
            if (swap_free_space > 0)
            {
                rt_memcpy(instance->swap_buffer + instance->swap_write_pos, data_cur, swap_free_space);
                instance->swap_write_pos += swap_free_space;
                data_cur += swap_free_space;
                remain_size -= swap_free_space;
            }
            if (!_commit_ram_to_old(instance))
            {
                return hpi_FALSE;
            }
        }
        else
        {
            if (remain_size > 0)
            {
                rt_memcpy(instance->swap_buffer + instance->swap_write_pos, data_cur, remain_size);
                instance->swap_write_pos += remain_size;
            }
            break;
        }
    }

    instance->newer_write_pos += size;

    int percent = instance->newer_write_pos * 100 / instance->newer_file_len;
    if (percent != instance->progress_percent && (percent % 5 == 0))
    {
        rt_kprintf("\rBuffering... %3d%%", percent);
        instance->progress_percent = percent;
        // Print a newline if the progress bar was previously shown
        if (percent >= 100)
        {
            rt_kprintf("\n");
        }
    }
    return hpi_TRUE;
}

#endif // QBOOT_HPATCH_USE_RAM_BUFFER


// -----------------------------------------------------------------------------
// Main Public Function
// -----------------------------------------------------------------------------

/**
 * @brief Performs an in-place differential update from a patch package.
 * 
 * This function orchestrates the entire patch process. It initializes the appropriate
 * buffer strategy (Flash or RAM), invokes the HPatchLite library, and handles
 * the finalization steps like committing the last block and erasing tail data.
 * 
 * @param patch_part        Handle containing the patch data.
 * @param old_part          Handle containing the old firmware, which will be updated in-place.
 * @param patch_name        Patch source name (for logs).
 * @param old_name          Old firmware name (for logs).
 * @param patch_file_len    Length of the patch data within the patch partition.
 * @param newer_file_len    Expected length of the new firmware after patching.
 * @param patch_file_offset Starting offset of the patch data within the patch partition.
 * @return 0 on success, non-zero on failure.
 */
int qbt_hpatchlite_release_from_part(void *patch_part, void *old_part, const char *patch_name, const char *old_name, int patch_file_len, int newer_file_len, int patch_file_offset)
{
    hpatchi_instance_t instance = {
        .patch_part = patch_part,
        .old_part = old_part,
        .patch_name = patch_name,
        .old_name = old_name,
        .patch_file_offset = patch_file_offset,
        .patch_file_len = patch_file_len,
        .newer_file_len = newer_file_len,
        .progress_percent = -1,
    };
    hpi_patch_result_t result = HPATCHI_PATCH_ERROR;
#if defined(QBOOT_HPATCH_USE_FLASH_SWAP)
    rt_bool_t swap_opened = RT_FALSE;
#endif

#if defined(QBOOT_HPATCH_USE_FLASH_SWAP)
    LOG_I("HPatchLite: Using FLASH swap strategy.");
    rt_uint32_t swap_part_size = 0;
    if (!qbt_target_open(QBOOT_HPATCH_SWAP_PART_NAME, &instance.swap_part, &swap_part_size))
    {
        LOG_E("Swap partition '%s' open fail.", QBOOT_HPATCH_SWAP_PART_NAME);
        return -1;
    }
    swap_opened = RT_TRUE;
    instance.swap_offset = QBOOT_HPATCH_SWAP_OFFSET;

    if ((rt_uint32_t)instance.swap_offset >= swap_part_size)
    {
        LOG_E("Swap offset %d out of range (size %u).", instance.swap_offset, swap_part_size);
        result = HPATCHI_PATCH_ERROR;
        goto hpatch_cleanup;
    }
    instance.swap_size = (int)(swap_part_size - (rt_uint32_t)instance.swap_offset);

    LOG_I("Erasing swap area '%s' (size: %d) before use...", QBOOT_HPATCH_SWAP_PART_NAME, instance.swap_size);
    if (_header_io_ops->erase(instance.swap_part, instance.swap_offset, instance.swap_size) != RT_EOK)
    {
        LOG_E("Failed to erase swap partition '%s'! OTA aborted.", QBOOT_HPATCH_SWAP_PART_NAME);
        result = HPATCHI_PATCH_ERROR;
        goto hpatch_cleanup;
    }

    result = hpi_patch(&instance.parent, QBOOT_HPATCH_PATCH_CACHE_SIZE, QBOOT_HPATCH_DECOMPRESS_CACHE_SIZE, _do_read_patch, _do_read_old, _do_write_new_flash);

    if (result == HPATCHI_SUCCESS && instance.swap_write_pos > 0)
    {
        if (!_commit_swap_to_old_flash(&instance))
        {
            result = HPATCHI_PATCH_ERROR;
        }
    }
hpatch_cleanup:
    if (swap_opened)
    {
        qbt_target_close(instance.swap_part);
    }
#elif defined(QBOOT_HPATCH_USE_RAM_BUFFER)
    LOG_I("HPatchLite: Using RAM buffer strategy.");
    instance.swap_buffer_size = QBOOT_HPATCH_RAM_BUFFER_SIZE;
    instance.swap_buffer = rt_malloc(instance.swap_buffer_size);
    if (!instance.swap_buffer)
    {
        LOG_E("Failed to malloc %d bytes for RAM buffer.", instance.swap_buffer_size);
        return -1;
    }
    instance.swap_write_pos = 0;
    instance.committed_len = 0;
    LOG_D("Allocated %d bytes for RAM buffer.", instance.swap_buffer_size);

    result = hpi_patch(&instance.parent, QBOOT_HPATCH_PATCH_CACHE_SIZE, QBOOT_HPATCH_DECOMPRESS_CACHE_SIZE, _do_read_patch, _do_read_old, _do_write_new_ram);

    // After the patch loop, commit the last data block in RAM if it exists
    if (result == HPATCHI_SUCCESS && instance.swap_write_pos > 0)
    {
        if (!_commit_ram_to_old(&instance))
        {
            result = HPATCHI_PATCH_ERROR;
        }
    }
    rt_free(instance.swap_buffer);
#else
#error "No HPatchLite buffer strategy selected. Please define QBOOT_HPATCH_USE_FLASH_SWAP or QBOOT_HPATCH_USE_RAM_BUFFER."
#endif

    // --- Finalization: Tail Erase and Verification ---
    if (result == HPATCHI_SUCCESS)
    {
        rt_uint32_t part_len = 0;
        if (_header_io_ops->size(old_part, &part_len) == RT_EOK && newer_file_len < (int)part_len)
        {
            rt_uint32_t sector_size = qbt_hpatch_get_erase_align(old_part);
            rt_uint32_t new_len = (rt_uint32_t)newer_file_len;
            rt_uint32_t erase_start_addr = (new_len % sector_size == 0) ? new_len : ((new_len / sector_size) + 1U) * sector_size;

            if (erase_start_addr < part_len)
            {
                rt_uint32_t erase_size = part_len - erase_start_addr;
                LOG_I("New firmware is smaller than '%s' partition. Erasing aligned tail data...", instance.old_name);
                LOG_D("Erasing '%s' from aligned offset %d, size %d", instance.old_name, erase_start_addr, erase_size);
                if (_header_io_ops->erase(old_part, erase_start_addr, erase_size) != RT_EOK)
                {
                    LOG_W("Failed to erase tail data, but patch itself is considered successful.");
                }
            }
        }
    }

    // Final check and result reporting
    if (result == HPATCHI_SUCCESS)
    {
        int final_len = 0;
        final_len = instance.committed_len;
        if (final_len == newer_file_len)
        {
            LOG_I("Update successful! Total size: %d bytes.", final_len);
        }
        else
        {
            LOG_E("Update finished, but final length (%d) != newer_file_len (%d)!", final_len, newer_file_len);
            result = HPATCHI_PATCH_ERROR;
        }
    }
    else
    {
        LOG_E("Update failed with error code: %d", result);
    }

    return (result == HPATCHI_SUCCESS);
}

#endif // QBOOT_USING_HPATCHLITE
