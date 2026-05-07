#ifndef CRC32_H
#define CRC32_H

#include <rtthread.h>

rt_uint32_t crc32_cal(rt_uint8_t *buf, rt_uint32_t len);
rt_uint32_t crc32_cyc_cal(rt_uint32_t crc, rt_uint8_t *buf, rt_uint32_t len);

#endif /* CRC32_H */
