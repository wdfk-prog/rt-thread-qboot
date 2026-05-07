# QBoot host 扩展覆盖说明

本文件说明 `run-host-extended-coverage.sh` 及相关 CI 模板新增的补充覆盖范围。

## 覆盖范围

- 协议适配层端到端长度、乱序、重叠、空洞、重复数据拒绝语义。
- 生产 HPatchLite 链路 smoke：新增 `custom-hpatch-production` runner，使用 `algorithm/qboot_hpatchlite.c` 和 CI HPatchLite shim 覆盖 no-compress single-cover fixture 子集，避免扩展覆盖静默跳过生产入口。
- RBL parser 固定随机种子 fuzz corpus，以及结构化 property manifest，覆盖合法边界包、非 NUL 字段、最大版本字段、坏 magic、header CRC、截断、unsupported algo 和 size overflow 等属性。
- 多故障序列与 replay 收敛 smoke：在同一进程内依次注入 erase/write/sign-read/sign-write 等故障，验证 custom/FAL backend 最终可收敛到可跳转的新 APP。
- FAL / fake-flash 分区偏移、跨区和邻区保护 current-policy。
- FS temp file、rename、retry、stale sign、close 失败等原子性 current-policy。
- AES+gzip 和 HPatchLite host release 的跨块 streaming 边界。
- HPatchLite 资源耗尽路径：覆盖生产 shim 首次 malloc 失败、首次分配后成功，以及 host adapter 无隐藏二次分配的 current-policy。
- 板级 smoke/HIL 模板：`run-board-smoke-template.sh` 在 CI 中以 dry-run 验证脚本自身，可在板级 runner 上通过 `QBOOT_BOARD_FLASH_CMD`、`QBOOT_BOARD_RESET_CMD` 和日志来源执行 Flash、跳转、复位 marker 检查。
- 进度回调、错误码和 host artifact 一致性检查。

## 限制

生产 HPatchLite host 覆盖使用 CI shim 支持的 no-compress single-cover fixture 子集，目的是强制经过生产 `algorithm/qboot_hpatchlite.c` 集成入口；它不是完整 upstream HPatchLite 算法证明。完整压缩组合、真实 Flash 时序、向量跳转副作用和复位保持行为仍需要板级/HIL 任务执行。

默认情况下，缺少 `qboot_host_runner_custom-hpatch-production` 或生产 fixture 会导致扩展覆盖失败；只有依赖 bootstrap job 可以显式设置 `QBOOT_HOST_ALLOW_HPATCHLITE_SKIP=1` 记录 skip。
