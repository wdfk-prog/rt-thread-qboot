# QBoot Tools and Packaging

This page explains the role of each entry in the repository `tools/` directory and recommends the most reviewable packaging path for current use.

## 1. What is in `tools/`

The current directory contains:

- `package_tool.py`
- `QBootPackager_V1.05.zip`
- `命令行打包工具.zip`

The primary recommendation in this documentation set is **`package_tool.py`** because it has:

- explicit arguments
- clear input and output behavior
- easy integration into scripts, CI, or production tooling
- directly reviewable source behavior

The two ZIP files can be treated as upstream bundled Windows packaging utilities for teams that want ready-to-run executables.

## 2. Role of each tool

### 2.1 `package_tool.py`
Packages a **package body that will be appended after the RBL header** into an RBL package that QBoot can parse.

It writes an RBL header in front of the payload. The header includes:

- algorithm combination
- timestamp
- partition name
- firmware version
- product code
- package CRC
- raw firmware CRC
- raw firmware size
- package size
- header CRC

### 2.2 `QBootPackager_V1.05.zip`
This is an upstream bundled Windows GUI packaging archive. The ZIP contains:

- `QBootPackager_V1.05.exe`
- `fastlz.dll`
- `quicklz150_32_3.dll`
- `QBootPackager_V1.05.INI`

Use this when the packaging flow is more manual or validation-oriented.

### 2.3 `命令行打包工具.zip`
This is another upstream bundled Windows command-line packaging archive. The ZIP currently includes:

- `PackagerTools.exe`
- `rtthread.bin`
- `命令示例.bat`
- required runtime DLLs

If you want, I can later unpack that ZIP and add a separate page with a more detailed option guide. For now, this page documents it as an **upstream bundled Windows CLI packager**.

## 3. Input and output model of `package_tool.py`

### Input
- `--pkg`: package body appended after the RBL header
- `--raw`: raw firmware image
- `--output`: output RBL file
- `--crypt`: encryption algorithm
- `--cmprs`: compression or differential algorithm
- `--algo2`: extra verification algorithm
- `--part`: target partition name
- `--version`: firmware version
- `--product`: product code

### Output
- one binary package with an RBL header

This output package is one of the standard input formats consumed by QBoot during update handling.

## 4. Supported argument values

According to the current `package_tool.py` source:

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

Note: when `--cmprs hpatchlite` is selected, the script forces `algo2` to `none`.

## 5. Typical usage

### 5.1 Full-image package without extra processing
If the package body is the raw firmware itself, `--pkg` and `--raw` can point to the same file:

```bash
python tools/package_tool.py   --pkg app.bin   --raw app.bin   -o app.rbl   --crypt none   --cmprs none   --algo2 crc   --part app   --version v1.00   --product 00010203040506070809
```

### 5.2 Package after compression or encryption
If compression or encryption is done on the PC side first:

- `--pkg` points to the processed package body
- `--raw` still points to the raw firmware

Example:

```bash
python tools/package_tool.py   --pkg app_fastlz.bin   --raw app.bin   -o app_fastlz.rbl   --crypt none   --cmprs fastlz   --algo2 crc   --part app   --version v1.00   --product 00010203040506070809
```

### 5.3 HPatchLite differential package
For differential update:

- `--pkg` points to `patch.bin`
- `--raw` points to `new.bin`
- `--cmprs` is `hpatchlite`

```bash
python tools/package_tool.py   --pkg patch.bin   --raw new.bin   -o patch.rbl   --crypt none   --cmprs hpatchlite   --part app   --version v1.00   --product 00010203040506070809
```


## 6. Web packager on GitHub Pages

The GitHub Pages site also publishes a browser-based RBL packager:

- Web entry: [QBoot RBL Packager](https://wdfk-prog.space/rt-thread-qboot/package-tool/index.html)
- Languages: Chinese and English, switchable from the page header
- Execution model: local browser execution through Pyodide
- File handling: uploaded files stay in the browser and are not sent to a server

The web page intentionally reuses the same packaging semantics as `tools/package_tool.py`: it builds the RBL header and appends the selected package body. For `none`/`none` packaging, the package body is usually the same file as the raw firmware. The selected `gzip`, `quicklz`, `fastlz`, `hpatchlite`, `xor`, or `aes` values are recorded in the RBL header algorithm fields. They do not perform package-body compression or encryption in the web page.

CI keeps the web page and local Python tool aligned by comparing `docs/package-tool/package_tool_web.py` against `tools/package_tool.py` across all supported `--crypt`, `--cmprs`, and `--algo2` combinations. The comparison is byte-for-byte on the generated `.rbl` output, so the local command-line tool and browser-side core are checked against the same observable package format.

## 7. How to choose a tool

### Recommended order
1. **`package_tool.py`** as the mainline choice
2. **`QBootPackager_V1.05.zip`** for Windows GUI packaging
3. **`命令行打包工具.zip`** for flows that want to stay with the upstream Windows CLI utility

### Selection guidance
- Need a reviewable, automatable, CI-friendly path: use `package_tool.py`
- Need a fast manual packaging tool on Windows: use the GUI packager
- Already have a Windows production flow based on upstream tools: keep using the ZIP-packed executable toolchain

## 8. Related documents

- For the main bring-up path, read [Quick Start](quick-start.md)
- For backend and algorithm combinations, read [Configuration Guide](configuration.md)
- For differential update, read [HPatchLite Differential OTA](differential-ota-hpatchlite.md)
