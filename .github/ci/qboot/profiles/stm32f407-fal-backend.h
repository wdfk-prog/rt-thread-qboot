/* CI-only FAL backend profile. */
#define RT_USING_FAL
#define FAL_PART_HAS_TABLE_CFG
#define FAL_DEV_NAME_MAX       24
#define FAL_DEV_BLK_MAX        6
#define BSP_USING_ON_CHIP_FLASH
#define QBOOT_PKG_SOURCE_FAL
#define QBOOT_APP_STORE_FAL
#define QBOOT_APP_FAL_PART_NAME        "app"
#define QBOOT_DOWNLOAD_STORE_FAL
#define QBOOT_DOWNLOAD_FAL_PART_NAME   "download"
#define QBOOT_FACTORY_STORE_FAL
#define QBOOT_FACTORY_FAL_PART_NAME    "factory"
