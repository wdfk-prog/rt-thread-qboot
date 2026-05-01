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


## 6. Web packager/unpacker on GitHub Pages

The GitHub Pages site also publishes a browser-based RBL packager/unpacker:

- Web entry: [QBoot RBL Packager](https://wdfk-prog.space/rt-thread-qboot/package-tool/index.html)
- Languages: Chinese and English, switchable from the page header
- Execution model: local browser execution through Pyodide
- File handling: uploaded files stay in the browser and are not sent to a server

The web page provides five modes:

1. **Process raw firmware and package**: transforms the raw firmware in the browser and then generates an RBL package. The transform order is the reverse of the QBoot release path: compress first, then encrypt.
2. **Wrap a prepared package body**: keeps the old `tools/package_tool.py` semantics and appends an already-prepared package body after the RBL header.
3. **Unpack and restore firmware**: reads an RBL package, validates the header CRC and package-body CRC, then decrypts and decompresses according to the header algorithm fields.
4. **Build HPatchLite differential package**: reads old and new firmware, creates an HPatchLite full-diff patch body, optionally leaves that body uncompressed or compresses it through HPatchLite's own `_CompressPlugin_tuz` path, and wraps it as an RBL package.
5. **Apply HPatchLite differential package**: reads old firmware and a differential RBL package, then restores the new firmware in the browser for validation or download.

The browser currently performs these real transforms:

- `none`: pass-through package and restore
- `gzip`: zlib/gzip compression and decompression compatible with QBoot-side `inflateInit2(..., 47)` auto detection
- `fastlz`: QBoot one-shot FastLZ block encoding and decoding using literal-only level-1 records
- `quicklz`: QBoot one-shot QuickLZ block encoding and decoding using level-1 stored packets
- `hpatchlite`: native HPatchLite full-diff patch generation and application; the patch stream can be uncompressed or use `_CompressPlugin_tuz` compression/decompression, so it is format-compatible but does not perform match-based size reduction
- `aes`: QBoot-compatible raw AES-CBC without padding; input must be 16-byte aligned. The default key/iv match `QBOOT_AES_KEY` and `QBOOT_AES_IV`, and the page also accepts manual values.

Prepared-body mode remains available for wrapping package bodies generated by external tools, including compact native `hdiffi` HPatchLite patches or compressed QuickLZ payloads that the browser path intentionally does not synthesize.

CI now checks two layers:

- Across every `--crypt`, `--cmprs`, and `--algo2` combination, prepared-body mode is compared byte-for-byte with `tools/package_tool.py`.
- Browser-side real transforms are covered by regression tests for RBL header parsing, CRC validation, gzip/FastLZ/QuickLZ package and restore, AES known-answer vectors, AES package/restore, HPatchLite differential roundtrip, and explicit failure paths for invalid transform combinations.

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

### Differential mode note

The browser differential mode emits a native HPatchLite full-diff patch body. Building a differential package requires old and new firmware inputs; applying it requires the old firmware plus the `.rbl` package. The outer RBL `cmprs` field is fixed to `hpatchlite` in differential mode and cannot be set to `none`/`gzip`/`quicklz`/`fastlz`. Differential patch-body compression can be disabled or handled through HPatchLite's own `_CompressPlugin_tuz` path; it does not use outer RBL `gzip`/`zlib` and does not use generic raw-deflate. This browser implementation stores the complete new firmware as diff data, so it is compatible with the HPatchLite patch format but does not provide the size reduction of native `hdiffi` match-based output. For compact production differential packages, generate the package body with the native HPatchLite tool and wrap it through manual package-body mode.
