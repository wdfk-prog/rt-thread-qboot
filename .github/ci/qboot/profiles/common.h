/* CI-only common qboot compile profile for stm32f407-atk-explorer. */
#define PKG_USING_CRCLIB
#define CRCLIB_USING_CRC32
#define CRC32_USING_CONST_TABLE
#define CRC32_POLY_EDB88320
#define CRC32_POLY             3988292384U
#define PKG_USING_QBOOT
#define QBOOT_THREAD_STACK_SIZE 4096
#define QBOOT_THREAD_PRIO       5
