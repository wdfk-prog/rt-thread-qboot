# QBoot HPatchLite Differential OTA

## 1. Goal

This document explains how to integrate HPatchLite into QBoot for **in-place differential OTA**.

In-place differential update means:

- the device already has an old APP image
- DOWNLOAD stores a patch package
- the bootloader reads the old image and the patch
- the new image is reconstructed directly in the APP target region

Compared with full-package upgrade, diff OTA saves bandwidth and DOWNLOAD storage, but it demands stricter control over erase alignment, buffering, and recovery behavior.

## 2. Patch package creation flow

### 2.1 Generate the patch
On the PC, use the HPatchLite tools to generate a patch from:

- `old.bin`
- `new.bin`

The output is typically:
- `patch.bin`

### 2.2 Optional verification
Before testing on hardware, reconstruct the target image on the PC and verify that it matches `new.bin`.

### 2.3 Package it for QBoot
Use `tools/package_tool.py` to wrap `patch.bin` into a QBoot-readable package.

`new.bin` is still needed during packaging because the package metadata must contain the final firmware size, CRC, and related fields.

## 3. Required configuration

At minimum, enable:

- `QBOOT_USING_HPATCHLITE`
- `QBOOT_HPATCH_PATCH_CACHE_SIZE`
- `QBOOT_HPATCH_DECOMPRESS_CACHE_SIZE`

Then choose one strategy:

- `QBOOT_HPATCH_USE_STORAGE_SWAP`
- `QBOOT_HPATCH_USE_RAM_BUFFER`

## 4. Choosing between the two strategies

### 4.1 Flash swap
Characteristics:
- lower RAM usage
- slower processing
- requires an extra swap storage region

Good for:
- RAM-constrained products
- products that can accept slower update time

### 4.2 RAM buffer
Characteristics:
- faster processing
- higher RAM usage
- no separate swap partition required

Good for:
- products with more available RAM
- products that want to avoid extra flash-to-flash copy overhead

## 5. Flash swap mode details

If flash swap is selected, choose one backend:

- FAL partition
- custom flash region
- filesystem file

Key options include:
- `QBOOT_HPATCH_SWAP_PART_NAME`
- `QBOOT_HPATCH_SWAP_FLASH_ADDR`
- `QBOOT_HPATCH_SWAP_FLASH_LEN`
- `QBOOT_HPATCH_SWAP_FILE_PATH`
- `QBOOT_HPATCH_SWAP_FILE_SIZE`
- `QBOOT_HPATCH_SWAP_OFFSET`
- `QBOOT_HPATCH_COPY_BUFFER_SIZE`

Design requirements:
- enough usable swap capacity
- a realistic copy buffer size
- stable read/write/erase behavior on the swap medium

## 6. RAM buffer mode details

Key option:
- `QBOOT_HPATCH_RAM_BUFFER_SIZE`

Recommendation:
- the RAM buffer should be at least as large as the erase unit of the target partition
- a larger buffer reduces commit count, but consumes more system RAM

## 7. Backend requirements

Diff OTA places stricter demands on the storage backend than full-package upgrade. At minimum, the backend should support:

- open/close/read/erase/write/size
- correct erase-alignment handling
- ideally an `ioctl` query for erase granularity

If erase alignment is unknown or wrong, in-place differential update is likely to fail or corrupt the target region.

## 8. Recommended partition layout

For flash swap mode, a common layout is:

```text
bootloader | app | factory | swap | download
```

Meaning:
- `app`: in-place update target
- `download`: patch package storage
- `swap`: temporary swap region for flash-swap mode
- `factory`: optional recovery image

## 9. Debug priorities

When enabling diff OTA for the first time, verify these first:

1. the patch was generated from the correct old firmware version
2. the package metadata contains the correct final size and CRC
3. erase granularity is real and usable
4. swap capacity or RAM buffer size is sufficient
5. APP and DOWNLOAD regions do not overlap accidentally

## 10. Recommended rollout order

1. make full-package upgrade work first
2. verify patch generation and PC-side reconstruction
3. enable device-side HPatchLite last

Do not make diff OTA your first upgrade path.
