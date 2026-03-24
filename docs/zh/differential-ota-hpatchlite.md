# QBoot HPatchLite 差分升级

## 1. 目标

本文档说明如何在 QBoot 中接入 HPatchLite，实现**原地差分升级**。

所谓原地差分升级，是指：

- 设备上已有旧 APP
- 下载区存放 patch 包
- bootloader 读取旧固件与补丁
- 在 APP 目标区原地合成新固件

相比整包升级，差分升级更节省带宽和下载区空间，但对擦除粒度、缓冲区设计和异常恢复要求更高。

## 2. 升级包制作流程

### 2.1 生成 patch
在 PC 上使用 HPatchLite 工具，根据旧固件和新固件生成 patch 文件。

输入：
- `old.bin`
- `new.bin`

输出：
- `patch.bin`

### 2.2 可选验证
在 PC 上先把 `old.bin + patch.bin` 合成出 `new_flash.bin`，确认它与 `new.bin` 一致。

### 2.3 打包为 QBoot 可识别格式
使用仓库中的 `tools/package_tool.py` 把 `patch.bin` 进一步打包为 QBoot 可处理的升级包。

打包时需要同时提供 `new.bin`，因为包头仍需要新固件长度、CRC 等元数据。

## 3. 启用配置

至少需要：

- `QBOOT_USING_HPATCHLITE`
- `QBOOT_HPATCH_PATCH_CACHE_SIZE`
- `QBOOT_HPATCH_DECOMPRESS_CACHE_SIZE`

然后在两种策略中二选一：

- `QBOOT_HPATCH_USE_STORAGE_SWAP`
- `QBOOT_HPATCH_USE_RAM_BUFFER`

## 4. 两种策略如何选

### 4.1 Flash swap
特点：
- RAM 占用较小
- 速度较慢
- 需要额外 swap 存储区域

适合：
- RAM 很紧张
- 允许更慢的升级过程

### 4.2 RAM buffer
特点：
- 速度更快
- RAM 占用更大
- 不需要独立 swap 分区

适合：
- RAM 较充足
- 希望减少 flash-to-flash 搬运

## 5. Flash swap 模式要点

如果选择 flash swap，需要进一步选择后端：

- FAL 分区
- 自定义 flash 区域
- 文件系统文件

关键配置包括：
- `QBOOT_HPATCH_SWAP_PART_NAME`
- `QBOOT_HPATCH_SWAP_FLASH_ADDR`
- `QBOOT_HPATCH_SWAP_FLASH_LEN`
- `QBOOT_HPATCH_SWAP_FILE_PATH`
- `QBOOT_HPATCH_SWAP_FILE_SIZE`
- `QBOOT_HPATCH_SWAP_OFFSET`
- `QBOOT_HPATCH_COPY_BUFFER_SIZE`

设计要求：
- swap 可用空间必须合理
- copy buffer 不能小到影响实际可用性
- swap 所在介质必须支持稳定读写擦

## 6. RAM buffer 模式要点

关键配置：
- `QBOOT_HPATCH_RAM_BUFFER_SIZE`

建议：
- RAM buffer 至少不小于目标分区擦除粒度
- 缓冲越大，commit 次数越少，但会挤占系统 RAM

## 7. 后端能力要求

差分升级对底层存储后端的要求比整包升级更高。至少应保证：

- 支持 open/close/read/erase/write/size
- 能正确处理擦除对齐
- 最好支持通过 `ioctl` 查询擦除粒度

如果后端不能正确提供擦除对齐信息，差分升级极易失败或破坏目标区数据。

## 8. 推荐分区规划

如果使用 flash swap，常见布局如下：

```text
bootloader | app | factory | swap | download
```

说明：
- `app`：原地升级目标区
- `download`：存放 patch 包
- `swap`：flash swap 周转区
- `factory`：可选恢复镜像

## 9. 调试重点

差分升级首次接入时，优先确认以下问题：

1. patch 包是否由正确版本的旧固件生成
2. 包头中的新固件长度和 CRC 是否正确
3. 擦除粒度是否真实可用
4. swap 容量或 RAM buffer 大小是否足够
5. APP 区和 DOWNLOAD 区是否被错误重叠

## 10. 推荐落地顺序

1. 先跑通整包升级
2. 再验证 patch 生成和 PC 侧还原
3. 最后再启用设备侧 HPatchLite

不要直接把差分升级作为第一条升级路径。
