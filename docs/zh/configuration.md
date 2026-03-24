# QBoot 配置指南

## 1. 配置思路

QBoot 的配置建议按四层来理解：

1. **存储层**：升级数据从哪里来，目标镜像写到哪里
2. **算法层**：是否启用加密解密、压缩解压、差分升级
3. **流程层**：是否启用升级接收流程框架、恢复流程、命令入口
4. **平台层**：是否覆盖 MCU 跳转、校验、看门狗、结果持久化等接口

## 2. 存储后端配置

### 2.1 FAL backend
适合已有分区表与 Flash 抽象的 RT-Thread 工程。

关键配置：
- `QBOOT_PKG_SOURCE_FAL`
- `QBOOT_APP_STORE_FAL`
- `QBOOT_DOWNLOAD_STORE_FAL`
- `QBOOT_FACTORY_STORE_FAL`
- `QBOOT_*_FAL_PART_NAME`

### 2.2 Filesystem backend
适合升级包和目标镜像通过文件形式管理的工程。

关键配置：
- `QBOOT_PKG_SOURCE_FS`
- `QBOOT_*_STORE_FS`
- `QBOOT_*_FILE_PATH`
- `QBOOT_*_SIGN_FILE_PATH`

### 2.3 Custom backend
适合以下情况：

- 不希望引入 FAL
- 已有自定义 Flash / QSPI / NAND / 外部存储驱动
- 需要混合片上与片外存储
- 需要完全自行控制读写擦策略

关键配置：
- `QBOOT_PKG_SOURCE_CUSTOM`
- `QBOOT_*_STORE_CUSTOM`
- `QBOOT_*_FLASH_ADDR`
- `QBOOT_*_FLASH_LEN`
- `QBOOT_FLASH_ERASE_ALIGN`

必要条件：
- 你必须提供可靠的读 / 写 / 擦接口
- 擦除对齐必须真实有效
- 若有特殊控制需求，需要配套扩展 `ioctl` 或 custom ops

## 3. 算法配置

### 3.1 加密解密
适合需要控制固件明文暴露风险的项目。

### 3.2 压缩解压
可在带宽与存储压力较大时启用。

### 3.3 差分升级
适合希望减少传输量和下载时间的项目，但对数据组织和存储规划要求更高。

### 3.4 组合建议
首次集成建议先关闭算法链路，确认写入、校验和跳转都正常后，再逐项打开。

## 4. 升级接收流程框架

启用条件：
- `QBOOT_USING_UPDATE_MGR`

这个模块的定位不是“唯一升级方案”，而是一个**可配置的内置升级接收流程框架**。它主要负责：

- 管理等待接收、正在接收、处理完成等状态
- 组织下载超时与等待窗口
- 协调恢复探测与结果状态
- 在 helper 模式下统一处理接收目标区的打开、擦除和写入

## 5. MCU 对接接口与扩展框架

QBoot 提供了多系列 MCU 对接接口与可扩展框架。默认行为不适合你的工程时，应在用户工程中覆盖实现。

常见接口包括：

- `qbt_jump_to_app()`：APP 跳转前的芯片收尾动作
- `qbt_fw_check()`：固件包合法性检查
- `qbt_dest_part_verify()`：目标区验证策略
- `qbt_release_sign_check()`：release 标记检查
- `qboot_src_read_pos()`：包头读取位置规则
- `qbt_wdt_feed()`：看门狗喂狗行为
- `qbt_ops_custom_init()`：自定义初始化入口
- `qboot_notify_update_result()`：升级结果记录与上报

## 6. 自定义后端设计要点

采用 custom backend 时，至少要自己明确：

- APP / DOWNLOAD / FACTORY 是否存在，以及各自位置
- 擦除粒度是否统一
- 是否跨片内与片外存储
- 是否需要文件式接口或块设备式接口
- 校验与跳转前是否要补充硬件收尾动作

## 7. 实用组合建议

### 7.1 最小可用组合
- 一个后端
- APP 目标区
- 无算法或单一轻量算法
- 基础校验
- 跳转接口

### 7.2 常规升级组合
- APP + DOWNLOAD
- 升级接收流程框架
- 必要的压缩或解密
- 产品码校验或产品信息输出

### 7.3 差分升级组合
- DOWNLOAD + APP
- HPatchLite
- RAM buffer 或 SWAP 规划
- 正确的擦除对齐与恢复策略

## 8. 配置顺序建议

推荐按以下顺序逐步开启：

1. 先定存储后端
2. 再定 APP 目标区和升级输入区
3. 再接 MCU 跳转与校验接口
4. 再加算法能力
5. 最后再加升级接收流程框架和附加功能
