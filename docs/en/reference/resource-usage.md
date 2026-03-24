# QBoot Resource Usage

## 1. How to read this page

- RAM values **do not include thread stacks**
- Numbers in parentheses indicate additional buffer requirements
- These values are better for relative comparison than for final production budgeting
- Final budgeting should always be re-measured in the target project

## 2. Main takeaways

- the minimal feature set is roughly **5.3K flash / 4.1K RAM**
- the full feature set is roughly **37.4K flash / 17.7K RAM**
- QuickLZ is usually a good balance between footprint and gain
- AES, gzip, shell, and download-related features noticeably increase usage

## 3. Feature resource table

| Feature | Option | Flash | RAM | Notes | Recommendation |
|---|---|---:|---:|---|---|
| QBoot core | `PKG_USING_QBOOT` | 5392 + 1156 | 4192 | Core module | Required |
| Syswatch | `QBOOT_USING_SYSWATCH` | 2812 | 100 | System watchdog | Optional for production |
| Factory key | `QBOOT_USING_FACTORY_KEY` | 80 | 0 | Recovery key | Optional when hardware exists |
| Status LED | `QBOOT_USING_STATUS_LED` | 980 | 20 | Status indication | Useful for debug and service |
| Product code | `QBOOT_USING_PRODUCT_CODE` | 120 | 0 | Product-code check | Optional when package gating is needed |
| AES | `QBOOT_USING_AES` | 11568 | 296 (+4096) | Decryption | Enable when security requires it |
| gzip | `QBOOT_USING_GZIP` | 9972 | 8268 (+4096) | Decompression | Consider only when resources are available |
| QuickLZ | `QBOOT_USING_QUICKLZ` | 768 | 4 (+1024) (+4096) | Lightweight decompression | Common first choice |
| FastLZ | `QBOOT_USING_FASTLZ` | 704 | 0 (+1024) (+4096) | Lightweight decompression | Alternative |
| OTA downloader | `QBOOT_USING_OTA_DOWNLOADER` | 2456 | 24 | Download capability | Enable when online reception is needed |
| Shell | `QBOOT_USING_SHELL` | 3268 | 8 | Command line capability | High value during debug |
| Product info | `QBOOT_USING_PRODUCT_INFO` | 164 | 0 | Boot-time product info | Common optional feature |

## 4. How to use this table

### Minimal working version
Disable first:

- AES
- gzip
- shell
- OTA downloader
- status LED
- recovery key

### Maintenance-oriented version
Keep as needed:

- shell
- status LED
- product info
- syswatch when needed

### Algorithm-heavy version
Evaluate additionally:

- AES
- QuickLZ / gzip
- differential-update buffer and workflow overhead

## 5. How to re-measure

Re-measure in the final target project with at least:

- the final linker script
- the final optimization level
- the real MCU BSP
- a record of whether stacks, protocol buffers, and extra buffers are included
