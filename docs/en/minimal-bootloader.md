# QBoot Minimal Configuration Example

## 1. Document goal

This page shows how to trim QBoot into a minimal configuration that is easy to bring up, easy to validate, and easy to extend later.

“Minimal” here is not a fixed template. It is a configuration method: keep the required pieces first, then add algorithms, reception workflow, and product features step by step.

## 2. When this approach is useful

It is a good starting point when:

- you first need a version that can write, validate, and jump
- you want to validate the backend and jump path before anything else
- you plan to add compression, encryption, differential update, or recovery later

## 3. Minimalization principles

### Keep first
- QBoot core
- one working backend
- one APP target region
- basic image validation
- MCU jump interface

### Usually disable at first
- differential update
- heavy compression algorithms
- upgrade reception workflow
- shell and debug helpers
- status LED and recovery key

## 4. Suggested steps

1. make the bootloader project build independently
2. integrate one backend and verify erase/write behavior
3. keep only one APP target and the simplest firmware input path
4. bring up write, validation, and jump
5. add DOWNLOAD, FACTORY, diff update, or recovery only when needed

## 5. Backend selection advice

### 5.1 Choose FAL
Good for RT-Thread projects with a partition table.

### 5.2 Choose CUSTOM
Good when the project already has a private storage abstraction or does not want to depend on FAL.

### 5.3 Choose FS
Good when upgrade images are received and managed as files.

## 6. Algorithm advice

For the first minimal configuration:

- keep the algorithm pipeline disabled until the main path works
- if compression is required, prefer a lighter option first
- add differential update only after the main flow is stable

## 7. Validation priorities

At minimum, verify:

- the firmware input can be read
- the target region can be erased and written correctly
- image validation is trustworthy
- the jump path matches the current MCU

## 8. Recommended expansion order

1. add DOWNLOAD
2. add the upgrade reception workflow
3. add compression or encryption
4. then evaluate diff update and recovery policy

## 8. Illustrated trimming example

The following screenshots keep the visual path from the upstream “minimal bootloader” example. They show how a minimal working configuration can be trimmed step by step, but they are still examples rather than fixed mandatory settings.

### 8.1 Establish the minimal project baseline

![Establish the minimal project baseline](../figures/QBoot_mini_tu01.jpg)

### 8.2 Keep only the required package and platform settings

![Keep only the required package and platform settings](../figures/QBoot_mini_tu02.jpg)

### 8.3 Remove unnecessary components

![Remove unnecessary components](../figures/QBoot_mini_tu03.jpg)

### 8.4 Configure the smallest backend path

![Configure the smallest backend path](../figures/QBoot_mini_tu04.jpg)

### 8.5 Keep the APP target region

![Keep the APP target region](../figures/QBoot_mini_tu05.jpg)

### 8.6 Disable non-essential features

![Disable non-essential features](../figures/QBoot_mini_tu06.jpg)

### 8.7 Disable extra algorithms or advanced features

![Disable extra algorithms or advanced features](../figures/QBoot_mini_tu07.jpg)

### 8.8 Review the generated minimal configuration

![Review the generated minimal configuration](../figures/QBoot_mini_tu08.jpg)

### 8.9 Prepare build and link validation

![Prepare build and link validation](../figures/QBoot_mini_tu09.jpg)

### 8.10 Review the build result

![Review the build result](../figures/QBoot_mini_tu10.jpg)

### 8.11 Inspect the image output

![Inspect the image output](../figures/QBoot_mini_tu11.jpg)

### 8.12 Prepare the minimal upgrade input

![Prepare the minimal upgrade input](../figures/QBoot_mini_tu12.jpg)

### 8.13 Write and verify the basic upgrade path

![Write and verify the basic upgrade path](../figures/QBoot_mini_tu13.jpg)

### 8.14 Inspect the target region result

![Inspect the target region result](../figures/QBoot_mini_tu14.jpg)

### 8.15 Validate the image-check path

![Validate the image-check path](../figures/QBoot_mini_tu15.jpg)

### 8.16 Validate the state before jump

![Validate the state before jump](../figures/QBoot_mini_tu16.jpg)

### 8.17 Validate the APP jump result

![Validate the APP jump result](../figures/QBoot_mini_tu17.jpg)

### 8.18 Observe the runtime result of the minimal configuration

![Observe the runtime result of the minimal configuration](../figures/QBoot_mini_tu18.jpg)

### 8.19 Evaluate whether more features should be added

![Evaluate whether more features should be added](../figures/QBoot_mini_tu19.jpg)

### 8.20 Create the baseline for later expansion

![Create the baseline for later expansion](../figures/QBoot_mini_tu20.jpg)

## 9. Relationship to the upstream historical document

This page keeps the core intent of the upstream “minimal bootloader” document, but rewrites it as a cleaner configuration guide rather than preserving the original narrative style.
