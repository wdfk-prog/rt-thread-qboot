# QBoot Documentation Portal {#mainpage}

Welcome to the QBoot documentation portal.

This site combines:
- API reference generated from the source tree
- handwritten project documentation already maintained in the repository

## Project Documents

### English
- [Documentation Map](en/document-map.md)
- [Overview](en/overview.md)
- [Quick Start](en/quick-start.md)
- [Configuration](en/configuration.md)
- [Update Manager](en/update-manager.md)
- [HPatchLite Differential OTA](en/differential-ota-hpatchlite.md)
- [Tools](en/tools.md)
- [Minimal Bootloader](en/minimal-bootloader.md)
- [Workflow Reference](en/reference/workflow.md)
- [Command Reference](en/reference/command-reference.md)

### 中文
- [文档地图](zh/document-map.md)
- [总览](zh/overview.md)
- [快速开始](zh/quick-start.md)
- [配置说明](zh/configuration.md)
- [升级流程框架](zh/update-manager.md)
- [HPatchLite 差分升级](zh/differential-ota-hpatchlite.md)
- [工具说明](zh/tools.md)
- [最小化 Bootloader](zh/minimal-bootloader.md)
- [工作流程说明](zh/reference/workflow.md)
- [命令参考](zh/reference/command-reference.md)

## API Entry Points

- [Data Structures](annotated.html)
- [File List](files.html)
- [Globals](globals.html)
- [Global Macros](globals_defs.html)

## Repository Layout

- Core public headers live under `inc/`
- Runtime implementation lives under `src/`
- MCU integration lives under `platform/`
- Algorithm modules live under `algorithm/`
- Packager helpers live under `tools/`

## Notes

- The Doxygen front page is `docs/doxygen-mainpage.md`.
- Handwritten project documentation is discovered from `docs/en/` and `docs/zh/`.
- Images used by the markdown docs are loaded from `docs/figures/`.
