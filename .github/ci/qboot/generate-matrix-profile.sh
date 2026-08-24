#!/usr/bin/env sh
set -eu

if [ "$#" -ne 4 ]; then
    printf 'Usage: %s <backend> <algorithm> <update> <output-file>\n' "$0" >&2
    exit 2
fi

backend=$1
algorithm=$2
update=$3
output_file=$4

case "$backend" in
    fal|fs|custom)
        ;;
    *)
        printf 'Unsupported QBoot backend: %s\n' "$backend" >&2
        exit 2
        ;;
esac

case "$algorithm" in
    none|aes|gzip|quicklz|fastlz|hpatch-ram|hpatch-storage)
        ;;
    *)
        printf 'Unsupported QBoot algorithm: %s\n' "$algorithm" >&2
        exit 2
        ;;
esac

case "$update" in
    off|helper|no-helper)
        ;;
    *)
        printf 'Unsupported QBoot update manager mode: %s\n' "$update" >&2
        exit 2
        ;;
esac

if [ "$algorithm" = hpatch-storage ] && [ "$backend" != fal ]; then
    printf 'HPatch storage swap requires the FAL backend in QBoot matrix compile profiles.\n' >&2
    exit 2
fi

mkdir -p "$(dirname "$output_file")"
cat > "$output_file" <<'EOF_COMMON'
/* Auto-generated CI-only QBoot matrix compile profile. */
#define PKG_USING_CRCLIB
#define CRCLIB_USING_CRC8
#define CRC8_USING_CONST_TABLE
#define CRC8_POLY_8C
#define CRC8_POLY 140
#define CRCLIB_USING_CRC16
#define CRC16_USING_CONST_TABLE
#define CRC16_POLY_A001
#define CRC16_POLY 40961
#define CRCLIB_USING_CRC32
#define CRC32_USING_CONST_TABLE
#define CRC32_POLY_EDB88320
#define CRC32_POLY 3988292384
#define PKG_USING_CRCLIB_V102

#define PKG_USING_QBOOT
#define QBOOT_USING_SHELL
#define QBOOT_SHELL_KEY_CHK_TMO 1
#define QBOOT_THREAD_STACK_SIZE 4096
#define QBOOT_THREAD_PRIO 5
#define PKG_USING_QBOOT_LATEST_VERSION
EOF_COMMON

case "$backend" in
    fal)
        cat >> "$output_file" <<'EOF_BACKEND'

/* FAL-only storage profile. */
#define RT_USING_FAL
#define FAL_PART_HAS_TABLE_CFG
#define FAL_USING_SFUD_PORT
#define FAL_USING_NOR_FLASH_DEV_NAME "norflash0"
#define FAL_DEV_NAME_MAX 24
#define FAL_DEV_BLK_MAX 6
#define RT_USING_SPI
#define RT_USING_MTD_NOR
#define RT_USING_SFUD
#define RT_SFUD_USING_SFDP
#define RT_SFUD_USING_FLASH_INFO_TABLE
#define RT_SFUD_SPI_MAX_HZ 50000000
#define BSP_USING_SPI_FLASH
#define BSP_USING_SPI
#define BSP_USING_SPI1
#define BSP_USING_ON_CHIP_FLASH

#define QBOOT_PKG_SOURCE_FAL
#define QBOOT_APP_STORE_FAL
#define QBOOT_APP_FAL_PART_NAME "app"
#define QBOOT_DOWNLOAD_STORE_FAL
#define QBOOT_DOWNLOAD_FAL_PART_NAME "download"
#define QBOOT_FACTORY_STORE_FAL
#define QBOOT_FACTORY_FAL_PART_NAME "factory"
EOF_BACKEND
        ;;
    fs)
        cat >> "$output_file" <<'EOF_BACKEND'

/* Filesystem-only storage profile. */
#define RT_USING_DFS
#define DFS_USING_POSIX
#define DFS_USING_WORKDIR
#define DFS_FD_MAX 16
#define RT_USING_DFS_V1
#define DFS_FILESYSTEMS_MAX 4
#define DFS_FILESYSTEM_TYPES_MAX 4
#define RT_USING_DFS_DEVFS
#define RT_USING_DFS_ROMFS
#define RT_USING_DFS_ROMFS_USER_ROOT

#define QBOOT_PKG_SOURCE_FS
#define QBOOT_APP_STORE_FS
#define QBOOT_APP_FILE_PATH "/app.rbl"
#define QBOOT_APP_SIGN_FILE_PATH "/app.rbl.sign"
#define QBOOT_DOWNLOAD_STORE_FS
#define QBOOT_DOWNLOAD_FILE_PATH "/download.rbl"
#define QBOOT_DOWNLOAD_SIGN_FILE_PATH "/download.rbl.sign"
#define QBOOT_FACTORY_STORE_FS
#define QBOOT_FACTORY_FILE_PATH "/factory.rbl"
EOF_BACKEND
        ;;
    custom)
        cat >> "$output_file" <<'EOF_BACKEND'

/* Custom-flash-only storage profile. */
#define QBOOT_PKG_SOURCE_CUSTOM
#define QBOOT_APP_STORE_CUSTOM
#define QBOOT_APP_FLASH_ADDR 0x08020000
#define QBOOT_APP_FLASH_LEN 0x00040000
#define QBOOT_DOWNLOAD_STORE_CUSTOM
#define QBOOT_DOWNLOAD_FLASH_ADDR 0x08060000
#define QBOOT_DOWNLOAD_FLASH_LEN 0x00020000
#define QBOOT_FACTORY_STORE_CUSTOM
#define QBOOT_FACTORY_FLASH_ADDR 0x08080000
#define QBOOT_FACTORY_FLASH_LEN 0x00020000
#define QBOOT_FLASH_ERASE_ALIGN 4096
EOF_BACKEND
        ;;
esac

case "$algorithm" in
    none)
        ;;
    aes)
        cat >> "$output_file" <<'EOF_ALGORITHM'

/* AES decrypt compile profile. */
#define QBOOT_USING_AES
#define QBOOT_AES_IV "0123456789ABCDEF"
#define QBOOT_AES_KEY "0123456789ABCDEF0123456789ABCDEF"
#define PKG_USING_TINYCRYPT
#define TINY_CRYPT_AES
EOF_ALGORITHM
        ;;
    gzip)
        cat >> "$output_file" <<'EOF_ALGORITHM'

/* GZIP decompress compile profile. */
#define QBOOT_USING_GZIP
#define PKG_USING_ZLIB
EOF_ALGORITHM
        ;;
    quicklz)
        cat >> "$output_file" <<'EOF_ALGORITHM'

/* QuickLZ decompress compile profile. */
#define QBOOT_USING_QUICKLZ
#define PKG_USING_QUICKLZ
EOF_ALGORITHM
        ;;
    fastlz)
        cat >> "$output_file" <<'EOF_ALGORITHM'

/* FastLZ decompress compile profile. */
#define QBOOT_USING_FASTLZ
#define PKG_USING_FASTLZ
EOF_ALGORITHM
        ;;
    hpatch-ram)
        cat >> "$output_file" <<'EOF_ALGORITHM'

/* HPatchLite RAM-buffer compile profile. */
#define QBOOT_USING_HPATCHLITE
#define QBOOT_HPATCH_PATCH_CACHE_SIZE 4096
#define QBOOT_HPATCH_DECOMPRESS_CACHE_SIZE 4096
#define QBOOT_HPATCH_USE_RAM_BUFFER
#define QBOOT_HPATCH_RAM_BUFFER_SIZE 4096
#define PKG_USING_HPATCHLITE
EOF_ALGORITHM
        ;;
    hpatch-storage)
        cat >> "$output_file" <<'EOF_ALGORITHM'

/* HPatchLite storage-swap compile profile. */
#define QBOOT_USING_HPATCHLITE
#define QBOOT_HPATCH_PATCH_CACHE_SIZE 4096
#define QBOOT_HPATCH_DECOMPRESS_CACHE_SIZE 4096
#define QBOOT_HPATCH_USE_STORAGE_SWAP
#define QBOOT_HPATCH_SWAP_STORE_FAL
#define QBOOT_HPATCH_SWAP_PART_NAME "swap"
#define QBOOT_HPATCH_SWAP_OFFSET 0
#define QBOOT_HPATCH_COPY_BUFFER_SIZE 4096
#define PKG_USING_HPATCHLITE
EOF_ALGORITHM
        ;;
esac

case "$update" in
    off)
        ;;
    helper)
        cat >> "$output_file" <<'EOF_UPDATE'

/* Update manager with built-in download helper. */
#define QBOOT_USING_UPDATE_MGR
#define QBOOT_SHELL_CMD_REASON
#define QBOOT_POLL_DELAY_MS 20
#define QBT_UPDATE_MGR_PROGRESS_ENABLE
#define QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER
EOF_UPDATE
        ;;
    no-helper)
        cat >> "$output_file" <<'EOF_UPDATE'

/* Update manager with application-provided download flow. */
#define QBOOT_USING_UPDATE_MGR
#define QBOOT_SHELL_CMD_REASON
#define QBOOT_POLL_DELAY_MS 20
#define QBT_UPDATE_MGR_PROGRESS_ENABLE
EOF_UPDATE
        ;;
esac

printf 'Generated QBoot matrix profile: backend=%s algorithm=%s update=%s\n' \
    "$backend" "$algorithm" "$update"
