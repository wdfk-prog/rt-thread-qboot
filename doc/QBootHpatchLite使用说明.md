# QBoot使用指导 —— 集成HPatchLite实现差分升级

## 1. 功能简介

本文档是在 **《QBoot使用指导》** 的基础上，专门针对如何集成和使用 `HPatchLite` 组件实现**差分升级（FOTA）**功能的详细说明。

`HPatchLite` 是一个高性能、资源占用极低的二进制差分库，特别适合在资源受限的嵌入式设备上执行固件的增量更新。通过将 `HPatchLite` 与 `QBoot` 结合，您可以显著减小OTA升级包的体积，节省流量和Flash空间。

本功能依赖 `QBoot`、`FAL`、`HPatchLite` 组件协同工作。

## 2. 差分升级包制作流程

差分升级的核心在于制作一个包含固件差异信息的“补丁包”。整个流程分为两步：

### 2.1 生成原始Patch文件

这一步需要在自行下载工具完成，用于对比新旧固件，并生成最原始的差分数据。

1.  **下载工具**:
    从HPatchLite的官方GitHub仓库下载其预编译的二进制工具包：
    [https://github.com/sisong/HPatchLite/releases](https://github.com/sisong/HPatchLite/releases)
    请下载最新版本的 `HPatchLite_vX.X.X_bin_xxxx.zip` 并解压。

2.  **准备文件**:
    *   `old.bin`: 您设备上当前正在运行的固件版本。
    *   `new.bin`: 您希望升级到的新版本固件。

3.  **执行打包命令**:
    打开命令行工具（如CMD或PowerShell），进入 `hdiffi.exe` 所在的目录，执行以下命令：

    ```bash
    .\hdiffi.exe old.bin new.bin patch.bin
    ```    *   `old.bin`: 旧固件文件路径。
    *   `new.bin`: 新固件文件路径。
    *   `patch.bin`: 命令成功后生成的原始差分补丁文件。

4.  **（可选）验证差分包**:
    您可以使用 `hpatchi.exe` 工具在PC上模拟差分合并过程，以验证 `patch.bin` 的正确性。

    ```bash
    .\hpatchi.exe old.bin patch.bin new_flash.bin
    ```
    执行后，使用文件比对工具确认生成的 `new_flash.bin` 与 `new.bin` 完全一致。

### 2.2 将Patch文件打包为QBoot可识别的RBL格式

QBoot需要一个带有标准头部信息（`.rbl`格式）的升级包。我们需要使用配套的Python脚本将上一步生成的 `patch.bin` 进行封装。

1.  **获取脚本**:
    该脚本通常位于 `qboot/tools/package_tool.py`。
    脚本参考了 [RT_ota_packager_python](https://github.com/sunkr1995/RT_ota_packager_python) 的设计。

2.  **执行打包命令**:
    在PC上执行以下Python脚本命令：
    ```bash
    python .\package_tool.py patch.bin new.bin
    ```
    *   **第一个参数 (`patch.bin`)**: 上一步生成的原始差分补丁文件。它将作为RBL包的数据主体。
    *   **第二个参数 (`new.bin`)**: 新版本固件文件。**注意**：这个文件**仅用于**计算RBL头部所需的元数据（如新固件的总大小`raw_size`和CRC校验`raw_crc`），它**不会**被包含在最终的升级包里。

    命令成功后，会在同目录下生成一个名为 **`patch.rbl`** 的文件。这个文件就是最终可以被QBoot下载并用于差分升级的固件包。

## 3. QBoot组件配置

要启用 `HPatchLite` 差分升级功能，您需要在RT-Thread Studio或menuconfig中进行相关配置。

#### 3.1 启用HPatchLite
首先，在QBoot的配置菜单中，必须勾选 `using hpatch-lite decompress` 选项。
```Kconfig
config QBOOT_USING_HPatchLITE
    bool "using hpatch-lite decompress"
    select PKG_USING_HPATCHLITE
    default n
```

#### 3.2 配置升级策略 (核心)
勾选后，会出现一个关键的二选一配置项 `HPatchLite In-Place Update Strategy`，用于决定差分升级时使用的缓冲区类型。**这是为了解决在同一块Flash上进行读（旧固件）写（新固件）冲突的核心问题。**

##### 3.2.1 策略一：使用Flash作为缓冲区 (Flash Swap)
这是在RAM资源极其宝贵时的推荐选项。它会使用一块独立的Flash分区作为临时周转空间。

*   **配置项**:
    ```Kconfig
    config QBOOT_HPATCH_USE_FLASH_SWAP
        bool "Use a FLASH partition as swap buffer"
    ```
*   **优点**: 极大地节省了宝贵的RAM资源。
*   **缺点**: 速度相对较慢，因为涉及多次Flash读写操作；对Flash的磨损也更大。
*   **关联配置**:
    *   **`Swap partition name for HPatchLite`**:
        必须指定一个在FAL中定义好的、用作交换空间的分区名称。
        ```Kconfig
        config QBOOT_HPATCH_SWAP_PART_NAME
            string "Swap partition name for HPatchLite"
            default "swap"
        ```
    *   **`Offset within the swap partition to use as a buffer`**:
        指定在`swap`分区内的起始偏移量。这允许您只使用`swap`分区的一部分作为缓冲区。
        ```Kconfig
        config QBOOT_HPATCH_SWAP_OFFSET
            int "Offset within the swap partition to use as a buffer"
            default 0
        ```
    *   **`RAM buffer size for flash-to-flash copy operations`**:
        配置在Flash分区之间拷贝数据时使用的**临时RAM缓冲区**大小。这个值越大，拷贝速度越快，但消耗的RAM也越多。
        ```Kconfig
        config QBOOT_HPATCH_COPY_BUFFER_SIZE
            int "RAM buffer size for flash-to-flash copy operations"
            default 4096
        ```

##### 3.2.2 策略二：使用RAM作为缓冲区 (RAM Buffer)
这是在RAM资源相对充足时的推荐选项。它直接在内存中完成数据周转，速度更快。

*   **配置项**:
    ```Kconfig
    config QBOOT_HPATCH_USE_RAM_BUFFER
        bool "Use a RAM buffer (Faster, consumes RAM)"
    ```
*   **优点**: 速度快，对Flash的磨损小。
*   **缺点**: 消耗的RAM较多，需要仔细评估。
*   **关联配置**:
    *   **`RAM buffer size for HPatchLite`**:
        配置用于差分升级的RAM缓冲区大小。**注意**：为了保证原地升级逻辑的正确性，这个值**必须大于等于**您`app`分区所在Flash的**最小擦除单元（Sector Size）**。
        ```Kconfig
        config QBOOT_HPATCH_RAM_BUFFER_SIZE
            int "RAM buffer size for HPatchLite (must be >= sector size)"
            default 4096
        ```

#### 3.3 存储后端与擦除对齐（新增）

HPatchLite 差分流程通过 `qboot_io_ops_t` 访问存储后端，不直接依赖 FAL。  
后端需要实现 `open/close/read/erase/write/size`，并建议实现可选 `ioctl` 扩展点：

- **`QBOOT_IO_CMD_GET_ERASE_ALIGN`**：返回擦除对齐/块大小，用于尾部对齐擦除与 swap 逻辑。
- 若未实现 `ioctl`，将回退到 **`QBOOT_HPATCH_ERASE_ALIGN_DEFAULT`**（默认 4096），可在工程配置或编译参数中覆盖。
- 自定义后端时，在 `qboot_register_storage_ops()` 中注册 `qboot_register_header_io_ops()` 与 `qboot_register_header_parser_ops()`。

## 4. FAL分区与配置示例

正确的FAL分区规划是差分升级成功的前提。以下是一个典型的配置示例。

### 4.1 `fal_cfg.h` 分区表示例

假设您的STM32芯片Flash的擦除粒度为128KB，您规划了`app`, `factory`, `swap`, `download` 四个分区。

```c
/* light_boot/UserSrc/fal_cfg.h */

/* 
 * Flash 物理地址规划 (示例):
 * bl       0x08000000 (128KB, 由IDE管理, 不在FAL中)
 * app      0x08020000 (384KB)
 * factory  0x08080000 (256KB)
 * swap     0x080A0000 (128KB)
 * download 0x080C0000 (128KB)
*/
#define FAL_PART_TABLE                                                                                  \
{                                                                                                       \
    {FAL_PART_MAGIC_WORD,   "app",          "onchip_flash_128k",                 0, 3 * 128 * 1024, 0}, \
    {FAL_PART_MAGIC_WORD,   "factory",      "onchip_flash_128k",    3 * 128 * 1024, 2 * 128 * 1024, 0}, \
    {FAL_PART_MAGIC_WORD,   "swap",         "onchip_flash_128k",    5 * 128 * 1024, 1 * 128 * 1024, 0}, \
    {FAL_PART_MAGIC_WORD,   "download",     "onchip_flash_128k",    6 * 128 * 1024, 1 * 128 * 1024, 0}, \
}
```
*   **`app`**: 存放应用程序固件的分区，也是差分升级的目标分区。
*   **`swap`**: **必须存在**（如果使用Flash Swap策略）。它的大小至少要等于 `app` 分区Flash的擦除粒度（本例中为128KB），用于在升级时做数据周转。
*   **`download`**: 用于存放下载的 `patch.rbl` 升级包。

### 4.2 `rtconfig.h` Kconfig宏定义示例

以下是与上述FAL分区和选择Flash Swap策略对应的 `rtconfig.h` 宏定义。

```c
#define PKG_USING_QBOOT
#define QBOOT_USING_PRODUCT_CODE
#define QBOOT_PRODUCT_CODE "00010203040506070809"
#define QBOOT_APP_PART_NAME "app"
#define QBOOT_DOWNLOAD_PART_NAME "download"
#define QBOOT_FACTORY_PART_NAME "factory"
#define QBOOT_USING_QUICKLZ

/* --- HPatchLite 关键配置 --- */
#define QBOOT_USING_HPATCHLITE
#define QBOOT_HPATCH_USE_FLASH_SWAP
#define QBOOT_HPATCH_SWAP_PART_NAME "swap"
#define QBOOT_HPATCH_SWAP_OFFSET 0
#define QBOOT_HPATCH_COPY_BUFFER_SIZE 4096
```

完成以上配置后，您的QBoot就具备了稳定、高效的差分升级能力。