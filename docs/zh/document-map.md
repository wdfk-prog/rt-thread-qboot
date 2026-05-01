# QBoot 文档地图

本文档用于说明当前文档树中每一页的职责，方便按问题类型而不是按文件名找资料。

## 1. 建议先看哪些

### 第一次接入
1. [总览](overview.md)
2. [快速开始](quick-start.md)
3. [配置指南](configuration.md)

### 需要升级流程
1. [升级接收流程框架](update-manager.md)
2. [工具与打包说明](tools.md)
3. [QBoot RBL Packager](https://wdfk-prog.space/rt-thread-qboot/package-tool/index.html)

### 需要差分升级
1. [HPatchLite 差分升级](differential-ota-hpatchlite.md)
2. [工具与打包说明](tools.md)
3. [QBoot RBL Packager](https://wdfk-prog.space/rt-thread-qboot/package-tool/index.html)

### 需要最小化裁剪示例
1. [最小化配置示例](minimal-bootloader.md)

## 2. 主文档说明

### [总览](overview.md)
说明 QBoot 的定位、能力边界、逻辑角色和典型处理流程。适合第一次了解整体方案时阅读。

### [快速开始](quick-start.md)
说明如何从空白或近空白工程出发，先跑通一条可工作的 bootloader 最小链路。

### [配置指南](configuration.md)
说明后端、算法、扩展接口、产品化能力的组合方式，适合开始做功能选择和工程裁剪时阅读。

### [升级接收流程框架](update-manager.md)
说明升级等待窗口、接收流程、状态处理与恢复探测等流程侧能力如何启用与集成。

### [HPatchLite 差分升级](differential-ota-hpatchlite.md)
说明差分包生成、设备侧处理策略、swap 或 RAM buffer 规划，以及首次接入时的调试重点。

### [工具与打包说明](tools.md)
说明 `tools/` 目录中各工具的定位，重点解释 `package_tool.py` 的输入输出、参数、典型用法以及 GitHub Pages 网页打包工具。

### [QBoot RBL Packager](https://wdfk-prog.space/rt-thread-qboot/package-tool/index.html)
浏览器版双语打包页面，用于从原始固件和要追加到 RBL header 后的包体文件在本地生成 `.rbl` 文件。

### [最小化配置示例](minimal-bootloader.md)
说明如何按最小目标裁剪配置，并保留一套可工作的接收、释放与跳转链路。

## 3. 参考文档说明

### [资源占用说明](reference/resource-usage.md)
用于查看不同配置和功能组合下的资源占用参考数据。

### [命令参考](reference/command-reference.md)
用于查看 Shell 命令及其作用，适合调试或产线阶段查阅。

### [工作流程参考](reference/workflow.md)
用于查看图示化流程与状态切换关系，适合对照运行过程理解内部处理链路。

### [状态指示灯](reference/status-led.md)
用于查看状态灯功能与指示含义，仅在项目启用该能力时需要阅读。
