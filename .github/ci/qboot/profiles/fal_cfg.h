/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-07-13     Dozingfiretruck   first version
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include <rtthread.h>
#include <board.h>

#define FLASH_SIZE_GRANULARITY_16K   (4 * 16 * 1024)
#define FLASH_SIZE_GRANULARITY_64K   (64 * 1024)
#define FLASH_SIZE_GRANULARITY_128K  (7 * 128 * 1024)

#define STM32_FLASH_START_ADRESS_16K  STM32_FLASH_START_ADRESS
#define STM32_FLASH_START_ADRESS_64K  (STM32_FLASH_START_ADRESS_16K + FLASH_SIZE_GRANULARITY_16K)
#define STM32_FLASH_START_ADRESS_128K (STM32_FLASH_START_ADRESS_64K + FLASH_SIZE_GRANULARITY_64K)

/* ===================== Flash device Configuration ========================= */
extern const struct fal_flash_dev stm32_onchip_flash_16k;
extern const struct fal_flash_dev stm32_onchip_flash_64k;
extern const struct fal_flash_dev stm32_onchip_flash_128k;

extern struct fal_flash_dev nor_flash0;

#define NOR_FLASH_DEV_NAME "norflash0"

/* flash device table */
#define FAL_FLASH_DEV_TABLE              \
{                                        \
    &stm32_onchip_flash_16k,             \
    &stm32_onchip_flash_64k,             \
    &stm32_onchip_flash_128k,            \
    &nor_flash0,                         \
}

#define RT_APP_PART_ADDR STM32_FLASH_START_ADRESS_128K

/* ====================== Partition Configuration ========================== */
#ifdef FAL_PART_HAS_TABLE_CFG

#define CMB_LOG_SIZE (4 * 1024)

#ifndef CMB_FAL_FLASH_LOG_PART
#define CMB_FAL_FLASH_LOG_PART "cmb_log"
#endif

/* partition table */
#define FAL_PART_TABLE                                                            \
{                                                                                 \
    {                                                                             \
        FAL_PART_MAGIC_WROD, "app", "onchip_flash_128k", 0,                      \
        FLASH_SIZE_GRANULARITY_128K, 0                                            \
    },                                                                            \
    {                                                                             \
        FAL_PART_MAGIC_WORD, "filesystem", NOR_FLASH_DEV_NAME, 0,                \
        8 * 1024 * 1024 - CMB_LOG_SIZE, 0                                         \
    },                                                                            \
    {                                                                             \
        FAL_PART_MAGIC_WORD, CMB_FAL_FLASH_LOG_PART, NOR_FLASH_DEV_NAME,          \
        8 * 1024 * 1024 - CMB_LOG_SIZE, CMB_LOG_SIZE, 0                           \
    },                                                                            \
}

/*
 * {FAL_PART_MAGIC_WROD, "bootloader", "onchip_flash_16k", 0,
 *  FLASH_SIZE_GRANULARITY_16K, 0},
 * {FAL_PART_MAGIC_WROD, "bootloader", "onchip_flash_64k", 0,
 *  FLASH_SIZE_GRANULARITY_64K, 0},
 */
#endif /* FAL_PART_HAS_TABLE_CFG */

#endif /* _FAL_CFG_H_ */
