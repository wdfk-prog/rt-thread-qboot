# QBoot 资源占用说明

## 1. 使用说明

- 表中 RAM 占用**不包含线程栈**
- 括号中的数值表示附加缓冲需求
- 数据更适合做相对比较，不适合直接当作量产绝对预算
- 若要做最终资源评估，应在目标工程中重新测量

## 2. 核心结论

- 最小功能集大约在 **5.3K flash / 4.1K RAM** 级别
- 全功能集大约在 **37.4K flash / 17.7K RAM** 级别
- QuickLZ 往往是体积与资源之间较均衡的选择
- AES、gzip、Shell、下载相关功能会显著拉高资源占用

## 3. 功能模块资源表

| 功能模块 | 选项宏 | Flash | RAM | 说明 | 使用建议 |
|---|---|---:|---:|---|---|
| QBoot core | `PKG_USING_QBOOT` | 5392 + 1156 | 4192 | 核心模块 | 必备 |
| Syswatch | `QBOOT_USING_SYSWATCH` | 2812 | 100 | 系统看守 | 量产可选 |
| Factory key | `QBOOT_USING_FACTORY_KEY` | 80 | 0 | 恢复按键 | 有硬件入口时可选 |
| Status LED | `QBOOT_USING_STATUS_LED` | 980 | 20 | 状态指示 | 调试与现场维护可选 |
| Product code | `QBOOT_USING_PRODUCT_CODE` | 120 | 0 | 产品码鉴别 | 有包防错需求时可选 |
| AES | `QBOOT_USING_AES` | 11568 | 296 (+4096) | 解密 | 视安全需求启用 |
| gzip | `QBOOT_USING_GZIP` | 9972 | 8268 (+4096) | 解压 | 资源充裕时考虑 |
| QuickLZ | `QBOOT_USING_QUICKLZ` | 768 | 4 (+1024) (+4096) | 轻量解压 | 常见首选 |
| FastLZ | `QBOOT_USING_FASTLZ` | 704 | 0 (+1024) (+4096) | 轻量解压 | 备选 |
| OTA downloader | `QBOOT_USING_OTA_DOWNLOADER` | 2456 | 24 | 下载能力 | 有在线接收需求时启用 |
| Shell | `QBOOT_USING_SHELL` | 3268 | 8 | 命令行能力 | 调试期价值高 |
| Product info | `QBOOT_USING_PRODUCT_INFO` | 164 | 0 | 启动信息输出 | 常规可选 |

## 4. 如何利用这张表裁剪

### 最小可用版本
优先关闭：

- AES
- gzip
- Shell
- OTA downloader
- 状态灯
- 恢复按键

### 常规维护版本
建议视需求保留：

- Shell
- 状态灯
- Product info
- 必要时再加 Syswatch

### 带算法处理版本
再评估：

- AES
- QuickLZ / gzip
- 差分升级相关缓存与流程开销

## 5. 如何重新测量

建议使用目标工程的最终配置重新测量，至少保证：

- 使用最终链接脚本
- 使用最终优化等级
- 使用真实 MCU BSP
- 记录是否包含线程栈、协议缓存和额外缓冲区
