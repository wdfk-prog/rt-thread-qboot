/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-04-30     wdfk-prog         add qboot CI partitions
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include <rtthread.h>
#include <board.h>

#define FLASH_SIZE_GRANULARITY_16K   (4 * 16 * 1024)
#define FLASH_SIZE_GRANULARITY_64K   (64 * 1024)
#define FLASH_SIZE_GRANULARITY_128K  (7 * 128 * 1024)

#define STM32_FLASH_START_ADRESS_16K  STM32_FLASH_START_ADRESS
#define STM32_FLASH_START_ADRESS_64K  \
    (STM32_FLASH_START_ADRESS_16K + FLASH_SIZE_GRANULARITY_16K)
#define STM32_FLASH_START_ADRESS_128K \
    (STM32_FLASH_START_ADRESS_64K + FLASH_SIZE_GRANULARITY_64K)

extern const struct fal_flash_dev stm32_onchip_flash_16k;
extern const struct fal_flash_dev stm32_onchip_flash_64k;
extern const struct fal_flash_dev stm32_onchip_flash_128k;

#define FAL_FLASH_DEV_TABLE                   \
{                                             \
    &stm32_onchip_flash_16k,                  \
    &stm32_onchip_flash_64k,                  \
    &stm32_onchip_flash_128k,                 \
}

#ifdef FAL_PART_HAS_TABLE_CFG
#define QBOOT_FAL_PART(_name, _offset, _len)  \
    {                                         \
        FAL_PART_MAGIC_WORD,                  \
        _name,                                \
        "onchip_flash_128k",                 \
        _offset,                              \
        _len,                                 \
        0                                     \
    }

#define FAL_PART_TABLE                                  \
{                                                       \
    QBOOT_FAL_PART("app", 0x00000000, 0x00040000),      \
    QBOOT_FAL_PART("download", 0x00040000, 0x00040000), \
    QBOOT_FAL_PART("factory", 0x00080000, 0x00020000),  \
    QBOOT_FAL_PART("swap", 0x000A0000, 0x00020000),     \
}
#endif /* FAL_PART_HAS_TABLE_CFG */
#endif /* _FAL_CFG_H_ */
