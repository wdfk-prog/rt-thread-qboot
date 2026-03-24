# QBoot 命令参考

## 1. 说明

QBoot 在启用 Shell 能力后，可以通过 `qboot` 命令执行若干维护操作。以下命令主要适用于调试、现场恢复和升级验证。

## 2. 命令列表

| 命令 | 说明 |
|---|---|
| `qboot probe` | 探测并显示 DOWNLOAD、FACTORY 等分区中的固件包信息 |
| `qboot resume src_part` | 当固件目标是 APP 时，从指定分区恢复到 APP |
| `qboot clone src_part dst_part` | 把一个分区中的固件包克隆到另一个分区 |
| `qboot release part` | 释放指定分区中的固件包到其目标区 |
| `qboot verify part` | 校验指定目标分区中的代码完整性 |
| `qboot jump` | 跳转到应用程序 |

## 3. 使用建议

- 调试期建议保留 `probe` 和 `verify` 能力
- 现场维护时，`resume` 和 `clone` 很有价值
- 正式量产版本是否保留 Shell，应根据安全和资源预算决定

## 4. 风险提醒

Shell 命令会带来：

- 额外资源占用
- 更大的误操作空间
- 更高的发布版本暴露面

如果产品没有维护接口需求，可以在量产版关闭 Shell。
