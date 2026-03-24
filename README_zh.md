# QBoot

[English](README.md) | 中文

QBoot 是一个面向嵌入式产品工程的可裁剪 bootloader 组件框架，适合在 RT-Thread 工程中集成使用，也支持按需改造成基于自定义存储后端的工程实现。

项目文档站点：https://wdfk-prog.space/rt-thread-qboot/

## 概览

- 支持 **FAL / 文件系统 / CUSTOM** 三类存储后端
- 内置 **加密解密、压缩解压、差分升级** 处理能力
- 可配置实现 **内置升级接收流程框架**
- 提供 **多系列 MCU 对接接口与可扩展框架**
- 支持按项目需要裁剪为最小可用版本或扩展为完整升级方案

详细说明见：[总览](docs/zh/overview.md)

## 文档索引

### 入门文档
- [总览](docs/zh/overview.md)
- [文档地图](docs/zh/document-map.md)
- [快速开始](docs/zh/quick-start.md)
- [配置指南](docs/zh/configuration.md)

### 升级与 OTA
- [升级接收流程框架](docs/zh/update-manager.md)
- [HPatchLite 差分升级](docs/zh/differential-ota-hpatchlite.md)
- [工具与打包说明](docs/zh/tools.md)

### 实战文档
- [最小化配置示例](docs/zh/minimal-bootloader.md)

### 参考文档
- [资源占用说明](docs/zh/reference/resource-usage.md)
- [命令参考](docs/zh/reference/command-reference.md)
- [工作流程参考](docs/zh/reference/workflow.md)
- [状态指示灯](docs/zh/reference/status-led.md)

## 建议阅读顺序

1. 先看 [总览](docs/zh/overview.md)
2. 再按 [快速开始](docs/zh/quick-start.md) 跑通最小链路
3. 然后按 [配置指南](docs/zh/configuration.md) 选择后端、算法和流程能力
4. 需要升级接收状态机时再看 [升级接收流程框架](docs/zh/update-manager.md)
5. 需要差分升级时再看 [HPatchLite 差分升级](docs/zh/differential-ota-hpatchlite.md)

## 作者与联系方式

### 上游资料
- 上游仓库：`qiyongzhong0/rt-thread-qboot`
- 上游仓库维护者：**齐永忠**（`qiyongzhong0`）
- 上游文章：**《基于RT-Thread 4.0快速打造bootloader》**
- 上游文章作者：**红枫**
- 邮箱：**未公开**
- 联系方式：
  - Gitee：`qiyongzhong0`
  - RT-Thread 社区文章主页：上游文章作者页

### 当前维护信息
- 当前仓库与分支：`wdfk-prog/rt-thread-qboot` / `feature`
- 当前维护者：**wdfk-prog**
- 邮箱：`qq1425075683@gmail.com`
- 联系方式：
  - GitHub：`wdfk-prog/rt-thread-qboot`
  - Handle：`@qq1425075683`

## 文档补充说明

- 完整文档列表见：[文档地图](docs/zh/document-map.md)
- 打包工具和使用方式见：[工具与打包说明](docs/zh/tools.md)

## 文档组织说明

- `algorithm/`：算法处理层
- `inc/`：公开头文件
- `src/`：核心实现
- `platform/`：平台与 MCU 对接层
- `tools/`：打包与辅助工具
