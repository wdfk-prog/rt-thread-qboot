# QBoot Configuration Guide

## 1. Configuration model

It is useful to understand QBoot configuration in four layers:

1. **Storage layer**: where upgrade data comes from and where the target image is written
2. **Algorithm layer**: whether encryption/decryption, compression/decompression, or differential update is enabled
3. **Workflow layer**: whether the upgrade reception workflow, recovery logic, or command entry is enabled
4. **Platform layer**: whether MCU jump, validation, watchdog, and result persistence are overridden

## 2. Storage backend configuration

### 2.1 FAL backend
Good for RT-Thread projects that already use a partition table and flash abstraction.

Key options:
- `QBOOT_PKG_SOURCE_FAL`
- `QBOOT_APP_STORE_FAL`
- `QBOOT_DOWNLOAD_STORE_FAL`
- `QBOOT_FACTORY_STORE_FAL`
- `QBOOT_*_FAL_PART_NAME`

### 2.2 Filesystem backend
Good when upgrade packages and images are managed as files.

Key options:
- `QBOOT_PKG_SOURCE_FS`
- `QBOOT_*_STORE_FS`
- `QBOOT_*_FILE_PATH`
- `QBOOT_*_SIGN_FILE_PATH`

### 2.3 Custom backend
Good when:

- you do not want to depend on FAL
- you already have a custom flash, QSPI, NAND, or external storage driver
- you need mixed on-chip and off-chip storage
- you want full control over read/write/erase behavior

Key options:
- `QBOOT_PKG_SOURCE_CUSTOM`
- `QBOOT_*_STORE_CUSTOM`
- `QBOOT_*_FLASH_ADDR`
- `QBOOT_*_FLASH_LEN`
- `QBOOT_FLASH_ERASE_ALIGN`

Required conditions:
- you must provide reliable read / write / erase interfaces
- erase alignment must be true and usable
- special control requirements may need custom `ioctl` or custom ops support

## 3. Algorithm configuration

### 3.1 Encryption and decryption
Useful when firmware exposure needs to be controlled.

### 3.2 Compression and decompression
Useful when bandwidth or storage pressure matters.

### 3.3 Differential update
Useful when transfer size and download time need to be reduced, but it requires tighter data and storage planning.

### 3.4 Combination advice
For the first integration, keep the algorithm pipeline disabled until write, validation, and jump are already stable.

## 4. Upgrade reception workflow framework

Enable with:
- `QBOOT_USING_UPDATE_MGR`

This module is not the only upgrade solution. It is a **configurable built-in upgrade reception workflow framework**. It mainly handles:

- wait, receive, and ready states
- download timeout and wait-window control
- recovery probing and result state coordination
- open / erase / write handling for the reception target in helper mode

## 5. MCU integration interfaces and extension framework

QBoot provides multi-MCU integration interfaces and an extensible framework. If the default behavior does not match your product, override it in the product project.

Common interfaces include:

- `qbt_jump_to_app()`
- `qbt_fw_check()`
- `qbt_dest_part_verify()`
- `qbt_release_sign_check()`
- `qboot_src_read_pos()`
- `qbt_wdt_feed()`
- `qbt_ops_custom_init()`
- `qboot_notify_update_result()`

## 6. Custom backend design checklist

When using a custom backend, define at least:

- whether APP / DOWNLOAD / FACTORY exist and where they are
- whether erase granularity is uniform
- whether storage spans on-chip and off-chip devices
- whether a file-style or block-style interface is needed
- whether pre-jump hardware cleanup must be extended

## 7. Practical configuration sets

### 7.1 Minimal working set
- one backend
- one APP target region
- no algorithm or one lightweight algorithm
- basic validation
- jump interface

### 7.2 Regular upgrade set
- APP + DOWNLOAD
- upgrade reception workflow
- required compression or decryption
- product-code validation or product information output

### 7.3 Differential update set
- DOWNLOAD + APP
- HPatchLite
- RAM buffer or SWAP planning
- correct erase-alignment and recovery policy

## 8. Recommended enable order

Enable features in this order:

1. storage backend
2. APP target and upgrade input region
3. MCU jump and validation interfaces
4. algorithm pipeline
5. upgrade reception workflow and optional product features
