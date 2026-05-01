# QBoot 工具与打包说明

本文档说明仓库 `tools/` 目录中各工具的定位，并给出当前最容易审查和自动化的打包方式。

## 1. `tools/` 目录里有什么

当前目录包含：

- `package_tool.py`
- `QBootPackager_V1.05.zip`
- `命令行打包工具.zip`

其中，最适合作为当前文档主推荐入口的是 **`package_tool.py`**，原因是：

- 参数明确
- 输入输出清晰
- 便于集成到脚本、CI 或产线流程
- 行为可以直接从源码审查

另外两个 ZIP 工具可以视为上游附带的 Windows 打包工具归档，适合已有 Windows 环境、希望直接使用现成可执行程序时采用。

## 2. 各工具定位

### 2.1 `package_tool.py`
用于把**已经准备好的包体**再封装成 QBoot 可识别的 RBL 包。

它会在输出文件前写入 RBL 头，头部中包含：

- 算法组合信息
- 时间戳
- 分区名
- 固件版本
- 产品码
- 包体 CRC
- 原始固件 CRC
- 原始固件大小
- 包体大小
- 头部 CRC

### 2.2 `QBootPackager_V1.05.zip`
这是上游附带的 Windows 图形化打包工具归档，压缩包内包含：

- `QBootPackager_V1.05.exe`
- `fastlz.dll`
- `quicklz150_32_3.dll`
- 配置文件 `QBootPackager_V1.05.INI`

如果你的打包流程更偏人工操作或验证，可以使用这类 GUI 工具。

### 2.3 `命令行打包工具.zip`
这是上游附带的另一份 Windows 命令行工具归档，压缩包内可以看到：

- `PackagerTools.exe`
- `rtthread.bin`
- `命令示例.bat`
- 运行依赖 DLL

如果后续你希望，我可以再单独把这个 ZIP 解包后补一页更细的工具参数说明。当前版本先把它定位为**上游附带的 Windows 命令行打包工具**。

## 3. `package_tool.py` 的输入输出模型

### 输入
- `--pkg`：准备好的包体
- `--raw`：原始固件
- `--output`：输出的 RBL 包路径
- `--crypt`：加解密算法
- `--cmprs`：压缩或差分算法
- `--algo2`：额外校验算法
- `--part`：目标分区名
- `--version`：固件版本
- `--product`：产品码

### 输出
- 一个带 RBL 头的二进制包

这个输出包就是 QBoot 在升级侧读取和解析的标准输入之一。

## 4. 支持的参数取值

根据当前 `package_tool.py` 源码：

### `--crypt`
- `none`
- `xor`
- `aes`

### `--cmprs`
- `none`
- `gzip`
- `quicklz`
- `fastlz`
- `hpatchlite`

### `--algo2`
- `none`
- `crc`

注意：当 `--cmprs hpatchlite` 时，脚本会强制把 `algo2` 置为 `none`。

## 5. 典型用法

### 5.1 整包升级，不启用额外处理
如果你的包体就是原始固件本身，那么 `--pkg` 和 `--raw` 可以指向同一个文件：

```bash
python tools/package_tool.py   --pkg app.bin   --raw app.bin   -o app.rbl   --crypt none   --cmprs none   --algo2 crc   --part app   --version v1.00   --product 00010203040506070809
```

### 5.2 压缩或加密后再打包
如果你已经在 PC 侧先做了压缩或加密，则：

- `--pkg` 指向处理后的包体
- `--raw` 仍指向原始固件

例如：

```bash
python tools/package_tool.py   --pkg app_fastlz.bin   --raw app.bin   -o app_fastlz.rbl   --crypt none   --cmprs fastlz   --algo2 crc   --part app   --version v1.00   --product 00010203040506070809
```

### 5.3 HPatchLite 差分包打包
差分升级时：

- `--pkg` 指向 `patch.bin`
- `--raw` 指向 `new.bin`
- `--cmprs` 选择 `hpatchlite`

```bash
python tools/package_tool.py   --pkg patch.bin   --raw new.bin   -o patch.rbl   --crypt none   --cmprs hpatchlite   --part app   --version v1.00   --product 00010203040506070809
```


## 6. GitHub Pages 网页打包工具

GitHub Pages 站点中同时发布了一个浏览器版 RBL 打包工具：

- 网页入口：[QBoot RBL Packager](https://wdfk-prog.space/rt-thread-qboot/package-tool/index.html)
- 语言：页面顶部可在中文和 English 之间切换
- 执行模型：通过 Pyodide 在浏览器本地执行
- 文件处理：上传的文件只在浏览器内读取，不会发送到服务器

网页端刻意保持和 `tools/package_tool.py` 相同的打包语义：生成 RBL header，然后追加你选择的包体文件。无压缩/无加密打包时，包体文件通常与原始固件相同。页面里选择的 `gzip`、`quicklz`、`fastlz`、`hpatchlite`、`xor` 或 `aes` 会写入 RBL header 的算法字段，但网页端不会实际对包体执行压缩或加密。

CI 会用全部 `--crypt`、`--cmprs`、`--algo2` 组合对比 `docs/package-tool/package_tool_web.py` 与 `tools/package_tool.py` 的输出。对比对象是生成后的 `.rbl` 完整字节流，因此本地命令行工具与浏览器端核心会按照同一个可观察包格式做一致性校验。

## 7. 如何选择工具

### 推荐优先级
1. **`package_tool.py`**：推荐作为主线方案
2. **`QBootPackager_V1.05.zip`**：适合 Windows 下图形化打包
3. **`命令行打包工具.zip`**：适合沿用上游现成 Windows CLI 工具的场景

### 选择建议
- 需要可审查、可自动化、可纳入 CI：优先 `package_tool.py`
- 需要快速人工打包验证：可考虑 GUI 打包器
- 已有 Windows 产线脚本依赖上游工具：可沿用 ZIP 中的可执行程序

## 8. 和文档其他页面的关系

- 想先跑通主链路：看 [快速开始](quick-start.md)
- 想理解算法与后端组合：看 [配置指南](configuration.md)
- 想做差分升级：看 [HPatchLite 差分升级](differential-ota-hpatchlite.md)
