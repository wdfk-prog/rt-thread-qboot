# QBoot Command Reference

## 1. Scope

When shell support is enabled, QBoot exposes a `qboot` command with a set of maintenance and verification operations. These commands are mainly useful during bring-up, service, and recovery.

## 2. Command list

| Command | Description |
|---|---|
| `qboot probe` | detect and print firmware package information in DOWNLOAD, FACTORY, and related storage |
| `qboot resume src_part` | restore firmware from the specified source partition into APP when APP is the package target |
| `qboot clone src_part dst_part` | clone a package from one partition into another |
| `qboot release part` | release the package in the specified partition to its target region |
| `qboot verify part` | verify the integrity of code in the specified target partition |
| `qboot jump` | jump to the application |

## 3. Recommendations

- keep `probe` and `verify` during bring-up
- `resume` and `clone` are useful for field recovery
- decide whether shell belongs in production based on security and footprint constraints

## 4. Risk reminder

Shell support also means:

- extra resource usage
- more room for operator mistakes
- a larger exposed surface in release builds

Disable it in production if the product does not need a maintenance CLI.
