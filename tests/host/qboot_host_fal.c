/**
 * @file qboot_host_fal.c
 * @brief Host FAL partition mock for QBoot L1 tests.
 */
#include "qboot_host_flash.h"
#include <fal.h>

#define QBOOT_HOST_FAL_FLASH_NAME "host-flash"

static rt_uint8_t g_fal_app[QBOOT_HOST_APP_LEN];           /**< APP partition image. */
static rt_uint8_t g_fal_download[QBOOT_HOST_DOWNLOAD_LEN]; /**< DOWNLOAD partition image. */
static rt_uint8_t g_fal_factory[QBOOT_HOST_FACTORY_LEN];   /**< FACTORY partition image. */

static struct fal_flash_dev g_fal_flash = {
    .name = QBOOT_HOST_FAL_FLASH_NAME,
    .blk_size = QBOOT_FLASH_ERASE_ALIGN,
};

static struct fal_partition g_fal_parts[] = {
    { "app", QBOOT_HOST_FAL_FLASH_NAME, 0u, QBOOT_HOST_APP_LEN },
    { "download", QBOOT_HOST_FAL_FLASH_NAME, 0u, QBOOT_HOST_DOWNLOAD_LEN },
    { "factory", QBOOT_HOST_FAL_FLASH_NAME, 0u, QBOOT_HOST_FACTORY_LEN },
};

/**
 * @brief Convert a host FAL partition pointer to a QBoot target id.
 *
 * @param part FAL partition pointer.
 * @return Matching QBoot target id, or QBOOT_TARGET_COUNT when unknown.
 */
static qbt_target_id_t fal_part_target(const struct fal_partition *part)
{
    if (part == &g_fal_parts[0])
    {
        return QBOOT_TARGET_APP;
    }
    if (part == &g_fal_parts[1])
    {
        return QBOOT_TARGET_DOWNLOAD;
    }
    if (part == &g_fal_parts[2])
    {
        return QBOOT_TARGET_FACTORY;
    }
    return QBOOT_TARGET_COUNT;
}

/**
 * @brief Convert a host FAL partition name to a QBoot target id.
 *
 * @param name FAL partition name.
 * @return Matching QBoot target id, or QBOOT_TARGET_COUNT when unknown.
 */
static qbt_target_id_t fal_name_target(const char *name)
{
    for (rt_size_t i = 0; i < sizeof(g_fal_parts) / sizeof(g_fal_parts[0]); i++)
    {
        if (rt_strcmp(name, g_fal_parts[i].name) == 0)
        {
            return fal_part_target(&g_fal_parts[i]);
        }
    }
    return QBOOT_TARGET_COUNT;
}

/**
 * @brief Return the in-memory buffer for a host FAL partition.
 *
 * @param part FAL partition pointer.
 * @return Partition data buffer, or RT_NULL when unknown.
 */
static rt_uint8_t *fal_part_data(const struct fal_partition *part)
{
    if (part == &g_fal_parts[0])
    {
        return g_fal_app;
    }
    if (part == &g_fal_parts[1])
    {
        return g_fal_download;
    }
    if (part == &g_fal_parts[2])
    {
        return g_fal_factory;
    }
    return RT_NULL;
}

/**
 * @brief Reset all host FAL partitions to erased state.
 */
void qboot_host_fal_reset(void)
{
    rt_memset(g_fal_app, 0xFF, sizeof(g_fal_app));
    rt_memset(g_fal_download, 0xFF, sizeof(g_fal_download));
    rt_memset(g_fal_factory, 0xFF, sizeof(g_fal_factory));
}

/**
 * @brief Initialize the host FAL mock.
 *
 * @return Positive value to match successful FAL initialization.
 */
int fal_init(void)
{
    return 1;
}

/**
 * @brief Look up a host FAL partition by name.
 *
 * @param name Partition name.
 * @return Partition descriptor, or RT_NULL when not found or faulted.
 */
const struct fal_partition *fal_partition_find(const char *name)
{
    qbt_target_id_t id;
    if (name == RT_NULL)
    {
        return RT_NULL;
    }
    id = fal_name_target(name);
    if (id < QBOOT_TARGET_COUNT && qboot_host_fault_check_id(QBOOT_HOST_FAULT_OPEN, id))
    {
        return RT_NULL;
    }
    for (rt_size_t i = 0; i < sizeof(g_fal_parts) / sizeof(g_fal_parts[0]); i++)
    {
        if (rt_strcmp(name, g_fal_parts[i].name) == 0)
        {
            return &g_fal_parts[i];
        }
    }
    return RT_NULL;
}

/**
 * @brief Read bytes from a host FAL partition.
 *
 * @param part   Partition descriptor.
 * @param offset Byte offset inside the partition.
 * @param buf    Output buffer.
 * @param size   Bytes to read.
 * @return Number of bytes read on success, negative value otherwise.
 */
int fal_partition_read(const struct fal_partition *part, rt_uint32_t offset, rt_uint8_t *buf, rt_uint32_t size)
{
    qbt_target_id_t id = fal_part_target(part);
    rt_uint8_t *data = fal_part_data(part);
    if (id < QBOOT_TARGET_COUNT && qboot_host_fault_check_id(QBOOT_HOST_FAULT_READ, id))
    {
        return -1;
    }
    if (data == RT_NULL || buf == RT_NULL || offset > part->len || size > (part->len - offset))
    {
        return -1;
    }
    rt_memcpy(buf, data + offset, size);
    return (int)size;
}

/**
 * @brief Write bytes to a host FAL partition.
 *
 * @param part   Partition descriptor.
 * @param offset Byte offset inside the partition.
 * @param buf    Input buffer.
 * @param size   Bytes to write.
 * @return Number of bytes written on success, negative value otherwise.
 */
int fal_partition_write(const struct fal_partition *part, rt_uint32_t offset, const rt_uint8_t *buf, rt_uint32_t size)
{
    qbt_target_id_t id = fal_part_target(part);
    rt_uint8_t *data = fal_part_data(part);
    if (id < QBOOT_TARGET_COUNT && qboot_host_fault_check_id(QBOOT_HOST_FAULT_WRITE, id))
    {
        return -1;
    }
    if (data == RT_NULL || buf == RT_NULL || offset > part->len || size > (part->len - offset))
    {
        return -1;
    }
    rt_memcpy(data + offset, buf, size);
    return (int)size;
}

/**
 * @brief Erase a range inside a host FAL partition.
 *
 * @param part   Partition descriptor.
 * @param offset Byte offset inside the partition.
 * @param size   Bytes to erase.
 * @return Number of bytes erased on success, negative value otherwise.
 */
int fal_partition_erase(const struct fal_partition *part, rt_uint32_t offset, rt_uint32_t size)
{
    qbt_target_id_t id = fal_part_target(part);
    rt_uint8_t *data = fal_part_data(part);
    if (id < QBOOT_TARGET_COUNT && qboot_host_fault_check_id(QBOOT_HOST_FAULT_ERASE, id))
    {
        return -1;
    }
    if (data == RT_NULL || offset > part->len || size > (part->len - offset))
    {
        return -1;
    }
    rt_memset(data + offset, 0xFF, size);
    return (int)size;
}

/**
 * @brief Look up the host FAL flash device by name.
 *
 * @param name Flash device name.
 * @return Flash device descriptor, or RT_NULL when not found.
 */
const struct fal_flash_dev *fal_flash_device_find(const char *name)
{
    if (name != RT_NULL && rt_strcmp(name, QBOOT_HOST_FAL_FLASH_NAME) == 0)
    {
        return &g_fal_flash;
    }
    return RT_NULL;
}
