# QBoot Upgrade Reception Workflow

## 1. Module role

This module should be understood as a **configurable built-in upgrade reception workflow framework**.

It is not mandatory and it is not the only upgrade solution. Its value is that it unifies common states such as wait, start receive, receiving, receive complete, receive failure, and recovery probing, so the protocol layer and storage layer do not need to keep separate state machines.

## 2. What it handles

Typical responsibilities include:

- upgrade reason and runtime state management
- wait-window and receive-idle timeout handling
- recovery probing when APP is not usable
- receive progress reporting
- open / erase / write handling for the reception target in helper mode

## 3. When it is useful

Use it when:

- the bootloader must wait for upgrade input after boot
- normal boot and request-upgrade boot must be distinguished
- receive-state and error handling should be centralized
- recovery or fallback logic is needed when APP is invalid

## 4. When you can skip it

You can often skip it when:

- the protocol layer already has a complete state machine
- no wait window is needed
- the reception path is simple and external logic already controls everything

## 5. Integration prerequisites

### 5.1 Enable option
- `QBOOT_USING_UPDATE_MGR`

### 5.2 Provide callbacks
Implement and register `qbt_update_ops_t`, typically including:

- `is_app_valid`
- `get_reason`
- `set_reason`
- `enter_download`
- `leave_download`
- `on_error`
- `on_ready_to_app`
- `try_recover`

## 6. Helper mode vs non-helper mode

### 6.1 Helper mode
Enable:
- `QBOOT_UPDATE_MGR_USE_DOWNLOAD_HELPER`

Good when:
- you want the component to handle open / erase / write for the reception target
- the protocol layer should only feed the data stream

### 6.2 Non-helper mode
Good when:
- you already have a full reception-storage flow
- you only want the state-control logic

## 7. Download helper length ownership

`qbt_update_mgr_download_begin()`, `qbt_update_mgr_download_write()`, and
`qbt_update_mgr_download_finish()` are storage-session helpers. They do not
track the complete received length, enforce packet ordering, reject duplicate
or overlapping writes, or compare a transport-level total size.

The intended ownership is:

- the protocol adapter owns transport-level total length, ordering, retry,
  duplicate, overlap, and gap policy;
- the storage backend owns write-range and capacity checks;
- the firmware parser/release path owns package-level `pkg_size`, `raw_size`,
  CRC, compression/decryption output size, and destination-size validation.

Calling `qbt_update_mgr_download_finish(RT_TRUE)` means the caller has already
accepted the received object. The helper does not independently prove that the
object is complete. For RBL packages, malformed, truncated, oversized, zero-size,
CRC-mismatched, and target-size-exceeded packages are rejected later by the
firmware check/release path. Extra trailing bytes after the declared package
body are accepted by current policy and ignored by package-size based parsing.

## 8. State model

The typical state meanings are:

- **WAIT**: waiting for upgrade input
- **RECV**: receiving upgrade data
- **READY**: ready for further processing or app handoff

## 9. Integration advice

1. Define the upgrade input entry first
2. Decide whether helper mode is needed
3. Persist the upgrade reason
4. Make recovery probing repeatable and side-effect aware
5. Bring up reception first, then optimize progress logs and UI

## 10. Relationship with other modules

- works with custom reception protocols
- works with custom/FAL/FS backends
- works with APP validation and recovery logic to decide when APP can run

## 11. Debug priorities

Check these first:

- whether upgrade reason read/write is reliable
- whether begin/write/finish order is correct
- whether timeout values are reasonable
- whether `try_recover()` reflects the real state
- whether helper-mode erase/write really succeeds
