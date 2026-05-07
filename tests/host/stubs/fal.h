#ifndef FAL_H
#define FAL_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Host FAL flash device descriptor. */
typedef struct fal_flash_dev
{
    const char *name;       /**< Flash device name. */
    rt_uint32_t blk_size;   /**< Erase block size in bytes. */
} fal_flash_dev_t;

/** @brief Host FAL partition descriptor. */
typedef struct fal_partition
{
    const char *name;       /**< Partition name. */
    const char *flash_name; /**< Parent flash name. */
    rt_uint32_t offset;     /**< Partition offset in host flash. */
    rt_uint32_t len;        /**< Partition length in bytes. */
} *fal_partition_t;

int fal_init(void);
const struct fal_partition *fal_partition_find(const char *name);
int fal_partition_read(const struct fal_partition *part, rt_uint32_t offset, rt_uint8_t *buf, rt_uint32_t size);
int fal_partition_write(const struct fal_partition *part, rt_uint32_t offset, const rt_uint8_t *buf, rt_uint32_t size);
int fal_partition_erase(const struct fal_partition *part, rt_uint32_t offset, rt_uint32_t size);
const struct fal_flash_dev *fal_flash_device_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* FAL_H */
