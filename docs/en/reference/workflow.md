# QBoot Workflow

## 1. Overall flow

QBoot's core behavior can be summarized like this:

1. initialize platform and storage access
2. initialize algorithm modules
3. evaluate update reason, download state, and recovery condition
4. read firmware metadata
5. run decrypt / decompress / diff reconstruction as required
6. write the result into the target region
7. validate the target firmware
8. jump to APP when conditions are satisfied

## 2. Overall flow diagram

![QBoot overall flow diagram](../../figures/QBoot.jpg)

## 2. Boot-time decision points

The most important boot-time questions are:

- should the system enter an update path now
- is it safe to jump directly to APP now

Common decision inputs:

- whether APP is valid
- whether DOWNLOAD holds a usable package
- whether FACTORY can be used for recovery
- whether a persistent update request flag is present

## 3. Firmware processing paths

### 3.1 Full-package path
Used for normal encrypted or compressed packages:

- read package metadata
- create the algorithm context
- process the input stream
- write the target region
- validate the target image

### 3.2 Differential path
Used for HPatchLite:

- read the patch
- read the old APP
- reconstruct the new image through swap or RAM buffer
- commit to APP with erase-aligned writes
- perform final length and image consistency checks

## 4. Recovery path

When APP is invalid, product policy may choose to:

- recover from DOWNLOAD
- recover from FACTORY
- continue waiting for a new package
- enter an error-handling path

## 5. Recommended debug order

When debugging workflow issues, check these in order:

1. storage backend behavior
2. package metadata parsing
3. algorithm switches vs real package format
4. target erase/write behavior
5. APP validation logic
6. jump-to-APP behavior

## 6. Common failure points

- partition definitions do not match the real flash map
- header location rules do not match the package format
- algorithm configuration does not match the packaging method
- custom backend erase alignment is wrong
- platform cleanup before jump is incomplete
