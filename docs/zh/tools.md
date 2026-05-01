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


## 6. GitHub Pages 网页打包/解包工具

GitHub Pages 站点中同时发布了一个浏览器版 RBL 打包/解包工具：

- 网页入口：[QBoot RBL Packager](https://wdfk-prog.space/rt-thread-qboot/package-tool/index.html)
- 语言：页面顶部可在中文和 English 之间切换
- 执行模型：通过 Pyodide 在浏览器本地执行
- 文件处理：上传的文件只在浏览器内读取，不会发送到服务器

网页端提供五种模式：

1. **自动处理原始固件并打包**：对原始固件执行浏览器端处理，再生成 RBL。处理顺序与 QBoot 释放链路相反，即先压缩、再加密。
2. **手动包体打包**：保持 `tools/package_tool.py` 的旧语义，直接把已经准备好的包体追加到 RBL header 后面。
3. **解包并还原固件**：读取 RBL 包，校验 header CRC 和包体 CRC，然后按 header 中的算法字段先解密、再解压，输出还原后的固件。
4. **生成 HPatchLite 差分包**：读取旧固件和新固件，生成 HPatchLite full-diff patch body，差分包体可选择不压缩或使用 HPatchLite 自带的 `_CompressPlugin_tuz` 压缩，然后封装为 RBL 包。
5. **应用 HPatchLite 差分包**：读取旧固件和差分 RBL 包，在浏览器中还原新固件，用于验证或下载。

当前浏览器端真实执行的算法：

- `none`：直通打包和还原
- `gzip`：使用 zlib/gzip 语义压缩和解压，兼容 QBoot 侧 `inflateInit2(..., 47)` 的自动识别行为
- `fastlz`：使用 literal-only level-1 records 生成并解析 QBoot one-shot FastLZ block
- `quicklz`：使用 level-1 stored packets 生成并解析 QBoot one-shot QuickLZ block
- `hpatchlite`：生成并应用 HPatchLite full-diff 原生格式 patch；patch stream 可选择不压缩或使用 `_CompressPlugin_tuz` 压缩/解压，格式兼容，但不做相似块匹配压缩
- `aes`：使用 QBoot 兼容的裸 AES-CBC，不做 padding，输入长度必须 16 字节对齐；默认 key/iv 与 `QBOOT_AES_KEY`、`QBOOT_AES_IV` 一致，也可以在页面手动输入

手动包体模式仍可用于封装外部工具生成的包体，例如原生 `hdiffi` 生成的紧凑 HPatchLite patch，或浏览器路径不主动合成的 compressed QuickLZ payload。

CI 会执行两类校验：

- 对全部 `--crypt`、`--cmprs`、`--algo2` 组合，对比手动包体模式与 `tools/package_tool.py` 的 `.rbl` 完整字节流。
- 对浏览器端真实处理路径执行回归测试，包括 RBL header 解析、CRC 校验、gzip/FastLZ/QuickLZ 打包与还原、AES 测试向量、AES 打包/还原、HPatchLite 差分往返，以及无效组合的明确失败路径。

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

### 差分模式说明

网页差分模式会生成 HPatchLite full-diff 原生格式包体，输入为旧固件和新固件；应用差分包时需要旧固件和 `.rbl` 包。RBL 外层 `cmprs` 字段固定为 `hpatchlite`，不能选择 `none`/`gzip`/`quicklz`/`fastlz`。差分包体内部可选择不压缩或使用 HPatchLite 自带的 `_CompressPlugin_tuz` 压缩/解压，不会走 RBL 外层 `gzip`/`zlib`，也不会使用通用 raw-deflate。该实现会把完整新固件作为 diff data 写入 HPatchLite patch，因此格式兼容 QBoot HPatchLite patch flow，但不会获得原生 `hdiffi` 相似块匹配带来的体积压缩。生产环境如需紧凑差分包，仍建议使用原生 HPatchLite 工具生成包体后走“手动包体打包”。
