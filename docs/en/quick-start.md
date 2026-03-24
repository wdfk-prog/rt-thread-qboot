# QBoot Quick Start

## 1. Goal

The goal of this page is to turn a blank or nearly blank project into a **working minimal bootloader flow** with as few assumptions as possible.

It does not assume on-chip flash, FAL, a three-region layout, compression, or diff OTA. Those can all be added later if needed.

## 2. Preconditions

You should at least have:

- a bootloader project that can be built and flashed independently
- a known application image location and startup address
- one usable low-level storage access path
- one way to feed upgrade data into the bootloader

## 3. First integration advice

For the first successful integration, keep only these pieces:

- one working storage backend: `FAL`, `FS`, or `CUSTOM`
- one APP target region
- one basic firmware input path
- basic image validation and jump flow

This is the fastest way to validate the main path.

## 4. Basic project preparation

### 4.1 Keep `main()` simple
If the bootloader does not need complex application-style initialization, keep `main()` minimal and only drive the required boot flow.

### 4.2 Prepare low-level storage access
You need at least one way to:

- read the firmware package or upgrade data
- erase the target region
- write the target image
- read target headers when validation or jump requires it

These capabilities can come from FAL, a filesystem, or a fully custom backend.

### 4.3 Define image boundaries
Before the first integration, define at least:

- APP start address or target location
- upgrade input region or file location
- erase granularity
- maximum image size

## 5. Choose a backend model

### 5.1 FAL backend
Good for RT-Thread projects that already use a partition table.

### 5.2 Filesystem backend
Good when upgrade packages arrive as files.

### 5.3 Custom backend
Good when the project already has a private flash abstraction, external storage driver, or a non-standard layout.

## 6. Add the QBoot package

Add QBoot through your package manager and focus first on:

- package source backend selection
- APP target region selection
- MCU-specific jump implementation
- whether any algorithm pipeline is enabled

## 7. Minimal recommended configuration

### Required
- `PKG_USING_QBOOT`
- one package source backend
- one APP store backend

### Usually disabled on the first bring-up
- AES
- gzip
- HPatchLite
- shell
- status LED
- recovery key
- upgrade reception workflow

## 8. Package and verify

### 8.1 Prepare the upgrade package
Use the tools in `tools/` to generate a QBoot-compatible package, or feed upgrade data through your own reception path.

### 8.2 Verify first success conditions
For the first successful run, confirm at least:

- the bootloader can read the upgrade input
- QBoot completes firmware processing and writing
- the APP image passes validity checks
- the bootloader jumps to APP reliably

## 8. Illustrated steps

The following screenshots keep the key integration views from the upstream bring-up example. They are useful as visual references for the path from a basic project to a working bootloader, but they are not meant to force the same toolchain, partition layout, or directory structure.

### 8.1 Create the base project

![Create the base project](../figures/QBoot_sample_t01.jpg)

### 8.2 Open the package manager and select QBoot

![Open the package manager and select QBoot](../figures/QBoot_sample_t02.jpg)

### 8.3 Enter the QBoot configuration page

![Enter the QBoot configuration page](../figures/QBoot_sample_t03.jpg)

### 8.4 Configure the basic options

![Configure the basic options](../figures/QBoot_sample_t04.jpg)

### 8.5 Configure storage or partition-related options

![Configure storage or partition-related options](../figures/QBoot_sample_t05.jpg)

### 8.6 Configure the APP region and upgrade input region

![Configure the APP region and upgrade input region](../figures/QBoot_sample_t06.jpg)

### 8.7 Configure the algorithm or processing pipeline

![Configure the algorithm or processing pipeline](../figures/QBoot_sample_t07.jpg)

### 8.8 Generate the project and verify package integration

![Generate the project and verify package integration](../figures/QBoot_sample_t08.jpg)

### 8.9 Prepare the packaging tool or upgrade input

![Prepare the packaging tool or upgrade input](../figures/QBoot_sample_t09.jpg)

### 8.10 Flash, verify, and observe the boot path

![Flash, verify, and observe the boot path](../figures/QBoot_sample_t10.jpg)

### 8.11 Confirm the first successful bring-up

![Confirm the first successful bring-up](../figures/QBoot_sample_t11.jpg)

## 9. Common problems

### 9.1 Why the package is not detected
Common causes:

- the input region was never written correctly
- the header position does not match the read rule
- the custom backend returns wrong length, offset, or data
- target-name, product-code, or validation policy rejects the package

### 9.2 Why release succeeds but jump fails
Check these first:

- whether `qbt_jump_to_app()` matches the current MCU
- whether interrupts, clocks, caches, and vector handling are correct
- whether the APP link address matches what the bootloader expects

### 9.3 Why APP validation fails
Check these first:

- whether the package format is correct
- whether the enabled algorithm pipeline matches the package
- whether custom validation is stricter than the default flow

## 10. Relationship to the upstream article

This page is inspired by the upstream article **“基于RT-Thread 4.0快速打造bootloader”**, but is reorganized for the current engineering target:

- FAL is not treated as mandatory
- a three-region layout is not treated as fixed
- on-chip flash is not treated as the default limitation
- the historical RT-Thread version context is not the main narrative anymore

## 11. What next

- Feature selection: [Configuration Guide](configuration.md)
- Upgrade state machine: [Upgrade Reception Workflow](update-manager.md)
- Differential update: [HPatchLite Differential OTA](differential-ota-hpatchlite.md)
