# QBoot host 扩展覆盖说明

本文件说明 `run-host-extended-coverage.sh` 及相关 CI 模板新增的补充覆盖范围。

## 覆盖范围

- 协议适配层端到端长度、乱序、重叠、空洞、重复数据拒绝语义。
- 生产 HPatchLite 链路 smoke：新增 `custom-hpatch-production` runner，使用 `algorithm/qboot_hpatchlite.c` 和 CI HPatchLite shim 覆盖 no-compress single-cover fixture 子集，避免扩展覆盖静默跳过生产入口。
- RBL parser 固定随机种子 fuzz corpus，以及结构化 property manifest，覆盖合法边界包、非 NUL 字段、最大版本字段、坏 magic、header CRC、截断、unsupported algo 和 size overflow 等属性。
- 多故障序列与 replay 收敛 smoke：在同一进程内依次注入 erase/write/sign-read/sign-write 等故障，验证 custom/FAL backend 最终可收敛到可跳转的新 APP。
- FAL / fake-flash 分区偏移、跨区和邻区保护 current-policy。
- FS temp file、rename、retry、stale sign、close 失败等原子性 current-policy。
- AES+gzip 和 HPatchLite host release 的跨块 streaming 边界；由于 AES 与 HPatchLite 不能同时启用，二者通过互斥 runner 构建。
- runtime codec 集成：
  - `run-host-runtime-codecs.sh` 拉取 crclib、tinycrypt、zlib、QuickLZ、FastLZ、HPatchLite 外部软件包源码，并分别链接到支持 AES 的 runner 和 HPatchLite-only runner 中。
  - `docs/package-tool/package_tool_web.py` 生成 golden RBL fixture，在不同时启用 AES 和 HPatchLite 的前提下验证 none/gzip/AES/AES+gzip/QuickLZ/FastLZ/HPatchLite 经过 C runtime release 路径。
  - crclib 和 TinyCrypt 通过生成的 runtime-only ABI adapter 编译，adapter 使用符号重命名包含外部实现文件，并导出 QBoot host 需要的 CRC/AES 符号。
  - 兼容头只用于适配 include 名称，不链接 host codec stub。
  - runtime 脚本把实际选中的外部源码写入 source manifest；如果软件包没有提供必要运行时符号，会在链接前失败。
- HPatchLite 资源耗尽路径：覆盖生产 shim 首次 malloc 失败、首次分配后成功，以及 host adapter 无隐藏二次分配的 current-policy。
- 板级 smoke/HIL 模板：`run-board-smoke-template.sh` 在 CI 中以 dry-run 验证脚本自身，可在板级 runner 上通过 `QBOOT_BOARD_FLASH_CMD`、`QBOOT_BOARD_RESET_CMD` 和日志来源执行 Flash、跳转、复位 marker 检查。
- 进度回调、错误码和 host artifact 一致性检查。
- 版本字段 current-policy：QBoot host 用例明确记录当前策略不会比较版本大小，版本降级包在产品码、目标分区和完整性校验通过时仍会被接受。

## 版本策略说明

QBoot 当前只把 `fw_ver` 作为包元数据记录和展示字段，不在 core release/resume 路径中实现防回滚或版本大小比较。host 覆盖中的 `custom-version-downgrade-current-policy` 和 `sign-same-size-different-version-current-policy` 用例用于在非阻塞 current-policy 报告中记录该行为，使后续策略变化可见，但不表示 QBoot 内置了 anti-rollback。

如果产品需要防回滚，应在产品业务层、签名/验签策略、服务端发包策略或调用 QBoot release 前的产品自定义检查中实现；不要仅依赖 QBoot 的版本字段。

## 限制

runtime codec 集成属于 host 侧 C 集成测试：它证明生成的 fixture 能在链接外部软件包源码和 runtime-only CRC/AES adapter 后的 QBoot 路径中释放成功，HPatchLite fixture 则通过独立的 HPatchLite-only runner 执行；但不能替代 BSP/HIL 对真实 Flash 时序、向量跳转副作用、复位保持行为和产品级防回滚策略的验证。生产 HPatchLite host 覆盖使用 CI shim 支持的 no-compress 和 TUZ fixture 子集；紧凑的 upstream `hdiffi` match-based patch 仍需要在产品工具链或板级环境中验证。

默认情况下，缺少 `qboot_host_runner_custom-hpatch-production` 或生产 fixture 会导致扩展覆盖失败；只有依赖 bootstrap job 可以显式设置 `QBOOT_HOST_ALLOW_HPATCHLITE_SKIP=1` 记录 skip。
