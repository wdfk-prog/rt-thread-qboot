/* CI-only mixed storage profile: APP uses FAL, DOWNLOAD/FACTORY use FS. */
#define RT_USING_FAL
#define FAL_PART_HAS_TABLE_CFG
#define FAL_DEV_NAME_MAX       24
#define FAL_DEV_BLK_MAX        6
#define BSP_USING_ON_CHIP_FLASH

#define RT_USING_DFS
#define DFS_USING_POSIX
#define DFS_USING_WORKDIR
#define DFS_FD_MAX               16
#define RT_USING_DFS_V1
#define DFS_FILESYSTEMS_MAX      4
#define DFS_FILESYSTEM_TYPES_MAX 4
#define RT_USING_DFS_DEVFS
#define RT_USING_DFS_ROMFS

#define QBOOT_PKG_SOURCE_FAL
#define QBOOT_PKG_SOURCE_FS
#define QBOOT_APP_STORE_FAL
#define QBOOT_APP_FAL_PART_NAME        "app"
#define QBOOT_DOWNLOAD_STORE_FS
#define QBOOT_DOWNLOAD_FILE_PATH       "/download.rbl"
#define QBOOT_DOWNLOAD_SIGN_FILE_PATH  "/download.rbl.sign"
#define QBOOT_FACTORY_STORE_FS
#define QBOOT_FACTORY_FILE_PATH        "/factory.rbl"
