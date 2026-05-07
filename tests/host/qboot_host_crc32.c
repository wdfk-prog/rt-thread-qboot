/**
 * @file qboot_host_crc32.c
 * @brief Host-side CRC32 implementation used by the qboot simulator.
 */
#include <crc32.h>

/** @brief IEEE CRC32 polynomial used by package_tool.py/zlib.crc32. */
#define QBOOT_HOST_CRC32_POLY 0xEDB88320u

/**
 * @brief Continue CRC32 calculation from an existing accumulator.
 *
 * @param crc Current CRC accumulator.
 * @param buf Input buffer.
 * @param len Input buffer length in bytes.
 *
 * @return Updated CRC accumulator without final XOR.
 */
rt_uint32_t crc32_cyc_cal(rt_uint32_t crc, rt_uint8_t *buf, rt_uint32_t len)
{
    for (rt_uint32_t i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (rt_uint32_t bit = 0; bit < 8; bit++)
        {
            if ((crc & 1u) != 0u)
            {
                crc = (crc >> 1) ^ QBOOT_HOST_CRC32_POLY;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief Calculate a complete CRC32 value.
 *
 * @param buf Input buffer.
 * @param len Input buffer length in bytes.
 *
 * @return Final CRC32 value.
 */
rt_uint32_t crc32_cal(rt_uint8_t *buf, rt_uint32_t len)
{
    return crc32_cyc_cal(0xFFFFFFFFu, buf, len) ^ 0xFFFFFFFFu;
}
