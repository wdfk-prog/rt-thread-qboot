# QBoot

English | [中文](README_zh.md)

QBoot is a configurable bootloader component framework for embedded products. It fits RT-Thread-based projects and can also be adapted to projects that use a custom storage backend.

Project site: https://wdfk-prog.space/rt-thread-qboot/

## Overview

- Supports **FAL / filesystem / CUSTOM** storage backends
- Built-in support for **encryption/decryption, compression/decompression, and differential update**
- Can be configured as a built-in **upgrade reception workflow framework**
- Provides **multi-MCU integration interfaces and an extensible framework**
- Can be trimmed to a minimal working set or expanded to a full upgrade solution

See the detailed [Overview](docs/en/overview.md).

## Documentation

### Getting started
- [Overview](docs/en/overview.md)
- [Documentation Map](docs/en/document-map.md)
- [Quick Start](docs/en/quick-start.md)
- [Configuration Guide](docs/en/configuration.md)

### Update and OTA
- [Upgrade Reception Workflow](docs/en/update-manager.md)
- [HPatchLite Differential OTA](docs/en/differential-ota-hpatchlite.md)
- [Tools and Packaging](docs/en/tools.md)

### Practical guide
- [Minimal Configuration Example](docs/en/minimal-bootloader.md)

### Reference
- [Resource Usage](docs/en/reference/resource-usage.md)
- [Command Reference](docs/en/reference/command-reference.md)
- [Workflow Reference](docs/en/reference/workflow.md)
- [Status LED](docs/en/reference/status-led.md)

## Recommended reading order

1. Read the [Overview](docs/en/overview.md)
2. Use the [Quick Start](docs/en/quick-start.md) to bring up the smallest working flow
3. Use the [Configuration Guide](docs/en/configuration.md) to choose backend, algorithm, and workflow options
4. Read the [Upgrade Reception Workflow](docs/en/update-manager.md) only when you need a managed upgrade state machine
5. Read [HPatchLite Differential OTA](docs/en/differential-ota-hpatchlite.md) only when diff update is required

## Authors and contact

### Upstream references
- Upstream repository: `qiyongzhong0/rt-thread-qboot`
- Upstream repository maintainer: **齐永忠** (`qiyongzhong0`)
- Upstream article: **"基于RT-Thread 4.0快速打造bootloader"**
- Upstream article author: **红枫**
- Email: **not publicly listed**
- Contact:
  - Gitee: `qiyongzhong0`
  - RT-Thread community article page: upstream author page

### Current maintenance
- Current repository and branch: `wdfk-prog/rt-thread-qboot` / `feature`
- Current maintainer: **wdfk-prog**
- Email: `qq1425075683@gmail.com`
- Contact:
  - GitHub: `wdfk-prog/rt-thread-qboot`
  - Handle: `@qq1425075683`

## Documentation notes

- Read the full document list in [Documentation Map](docs/en/document-map.md)
- Read packaging details in [Tools and Packaging](docs/en/tools.md)

## Repository layout

- `algorithm/`: algorithm handlers
- `inc/`: public headers
- `src/`: core implementation
- `platform/`: platform and MCU integration layer
- `tools/`: packaging and helper tools
