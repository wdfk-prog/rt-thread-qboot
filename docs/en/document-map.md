# QBoot Documentation Map

This page explains what each document in the current tree is for, so you can navigate by task instead of guessing from file names.

## 1. What to read first

### First integration
1. [Overview](overview.md)
2. [Quick Start](quick-start.md)
3. [Configuration Guide](configuration.md)

### Upgrade workflow work
1. [Upgrade Reception Workflow](update-manager.md)
2. [Tools and Packaging](tools.md)

### Differential update work
1. [HPatchLite Differential OTA](differential-ota-hpatchlite.md)
2. [Tools and Packaging](tools.md)

### Minimal trimmed setup
1. [Minimal Configuration Example](minimal-bootloader.md)

## 2. Main documents

### [Overview](overview.md)
Explains QBoot positioning, capability boundaries, logical roles, and the typical processing flow. Start here when you need the big picture.

### [Quick Start](quick-start.md)
Shows how to go from a blank or nearly blank project to a working minimal bootloader path.

### [Configuration Guide](configuration.md)
Explains how to combine backends, algorithms, extension interfaces, and product-facing features.

### [Upgrade Reception Workflow](update-manager.md)
Explains how to enable and integrate reception windows, state handling, timeout behavior, and recovery probing.

### [HPatchLite Differential OTA](differential-ota-hpatchlite.md)
Explains patch generation, device-side processing strategies, swap or RAM buffer planning, and first-integration debugging points.

### [Tools and Packaging](tools.md)
Explains the role of each entry in `tools/`, with a focus on the input, output, arguments, and typical usage of `package_tool.py`.

### [Minimal Configuration Example](minimal-bootloader.md)
Shows how to trim the configuration to a minimal goal while keeping a working reception, release, and jump path.

## 3. Reference documents

### [Resource Usage](reference/resource-usage.md)
Reference data for resource usage under different feature combinations.

### [Command Reference](reference/command-reference.md)
Shell commands and their purpose, mainly for debugging and production-side checks.

### [Workflow Reference](reference/workflow.md)
Diagram-oriented workflow notes for understanding internal processing and state transitions.

### [Status LED](reference/status-led.md)
Status LED behavior and indication meanings. Read this only when the project enables that feature.
