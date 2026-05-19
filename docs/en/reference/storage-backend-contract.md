# Storage Backend Contract

[中文](../../zh/reference/storage-backend-contract.md)

This reference defines the handle and ownership contract shared by the FAL, filesystem, CUSTOM, and mux storage backends.

## Handle ownership

Storage handles are opaque backend-owned values. A caller must obtain a handle through `qbt_target_open()` and must close it with `qbt_target_close()` exactly once when the operation is complete.

The backend callbacks in `qboot_io_ops_t` expect a valid, live handle returned by the matching backend `open()` callback. Passing a non-NULL invalid handle, a stale handle after close, a handle from a different backend, or a double-closed handle is outside the backend contract.

## Responsibility split

`qbt_target_open()` is responsible for target lookup, backend open, and the optional size query. `qbt_target_close()` accepts `RT_NULL` as a no-op for cleanup convenience. Other non-NULL handle misuse is a caller error and is not normalized by FAL, filesystem, or CUSTOM backend implementations.

Backend implementations still report operational failures that happen while using a valid handle, such as open failure, read/write short count, erase failure, close failure, filesystem capacity limits, and backend-specific fault injection in host tests.

## Test policy

Host tests cover valid lifecycle behavior and backend operational errors. They intentionally do not assert behavior for wrong handles, stale handles, or double close, because those calls violate the common backend contract rather than a backend-specific recovery path.

Current FS lifecycle tests therefore focus on valid multi-target and size-query behavior:

```text
fs-open-app-then-download-independent-fds
fs-size-lseek-current-position-restored
```

## Integration guidance

Keep target access behind `qbt_target_open()` / `qbt_target_close()` in production code. Do not store backend handles beyond the operation that opened them. When adding new code paths, prefer a single cleanup block that closes only handles that were successfully opened and then sets local handle variables back to `RT_NULL`.
