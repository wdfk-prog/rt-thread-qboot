# 存储后端契约

[English](../../en/reference/storage-backend-contract.md)

本文档定义 FAL、文件系统、CUSTOM 和 mux 存储后端共同遵守的 handle 与所有权契约。

## Handle 所有权

存储 handle 是后端拥有的不透明值。调用者必须通过 `qbt_target_open()` 获取 handle，并在本次操作结束后通过 `qbt_target_close()` 关闭一次。

`qboot_io_ops_t` 中的后端回调只接受由匹配后端 `open()` 回调返回、且仍处于打开状态的有效 handle。传入非 NULL 的错误 handle、close 后的 stale handle、来自其他后端的 handle，或对同一 handle 重复 close，均不属于后端契约范围。

## 责任边界

`qbt_target_open()` 负责目标查找、后端 open 和可选 size 查询。`qbt_target_close()` 为了便于清理流程，接受 `RT_NULL` 作为 no-op。其他非 NULL handle 误用属于调用者错误，FAL、文件系统和 CUSTOM 后端不负责统一规整该类行为。

后端仍需要报告有效 handle 使用过程中的操作失败，例如 open 失败、read/write 短计数、erase 失败、close 失败、文件系统容量限制，以及 host 测试中的后端故障注入。

## 测试策略

Host 测试覆盖合法生命周期和后端操作错误。测试不再断言 wrong handle、stale handle 或 double close 的行为，因为这些调用违反共同后端契约，不属于具体后端的恢复路径。

当前 FS 生命周期测试聚焦于合法多目标访问与 size 查询行为：

```text
fs-open-app-then-download-independent-fds
fs-size-lseek-current-position-restored
```

## 集成建议

生产代码应通过 `qbt_target_open()` / `qbt_target_close()` 访问目标，不要把后端 handle 保存到打开它的操作范围之外。新增路径时，优先使用统一 cleanup 分支：只关闭已经成功打开的 handle，并在关闭后把局部 handle 变量重置为 `RT_NULL`。
