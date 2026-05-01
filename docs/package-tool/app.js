const form = document.getElementById("packager-form");
const buildButton = document.getElementById("build-button");
const statusBox = document.getElementById("status");
const langButtons = document.querySelectorAll("[data-lang]");
const fileInputs = document.querySelectorAll(".file-input");
const fileTriggerButtons = document.querySelectorAll("[data-file-trigger]");
const modeSections = document.querySelectorAll("[data-mode-section]");
const packOnlySections = document.querySelectorAll("[data-pack-only]");
const compressFieldSections = document.querySelectorAll("[data-compress-field]");

const translations = {
  "zh-CN": {
    pageTitle: "QBoot RBL 打包/解包工具",
    eyebrow: "QBoot Web Tool",
    title: "RBL 打包/解包工具",
    subtitle: "在浏览器本地执行 RBL header 生成、gzip 压缩/解压、AES-CBC 加密/解密和固件还原。文件不会上传到服务器。",
    noticeTitle: "语义说明：",
    noticeBody: "自动打包模式会按 QBoot 释放链路的反向顺序处理：先压缩，再加密。解包模式会先校验 RBL header 和包体 CRC，再按 header 中算法字段执行解密和解压。当前浏览器端真实处理 none、gzip、fastlz、quicklz、aes；差分模式固定使用 RBL cmprs=hpatchlite，并且差分包体可选择不压缩或使用 HPatchLite 自带的 _CompressPlugin_tuz 压缩/解压。",
    operation: "操作模式",
    operationPackAuto: "自动处理原始固件并打包",
    operationPackManual: "手动包体打包",
    operationUnpack: "解包并还原固件",
    operationPatchPack: "生成 HPatchLite 差分包",
    operationPatchUnpack: "应用 HPatchLite 差分包并还原固件",
    rawFirmware: "原始固件",
    rawFirmwareHint: "自动打包时会由此文件生成包体；手动包体打包时用于写入 raw_crc 和 raw_size；差分打包时作为新固件。",
    oldFirmware: "旧固件",
    oldFirmwareHint: "差分模式需要。生成差分包时作为旧固件；应用差分包时作为还原基线。",
    hpatchCompress: "HPatchLite patch 压缩",
    hpatchCompressHint: "差分包体可选择 none 或 HPatchLite 自带的 _CompressPlugin_tuz。这里不是 RBL 外层 gzip/zlib，也不是通用 raw-deflate。",
    pkgBody: "要写入 RBL 的包体",
    pkgBodyHint: "仅手动包体打包需要。该文件会直接追加到 RBL header 后面。",
    rblPackage: "RBL 包文件",
    rblPackageHint: "解包模式需要。工具会校验 header、包体 CRC，并在支持的算法范围内还原原始固件。",
    chooseFile: "选择文件",
    noFileSelected: "未选择任何文件",
    crypt: "加密算法字段 (crypt)",
    compress: "压缩算法字段 (compress)",
    algo2: "校验算法字段 (algo2)",
    aesKey: "AES key",
    aesKeyHint: "默认与 QBOOT_AES_KEY 一致；支持普通文本或 hex: 前缀，必须为 32 字节。",
    aesIv: "AES IV",
    aesIvHint: "默认与 QBOOT_AES_IV 一致；支持普通文本或 hex: 前缀，必须为 16 字节。AES 不做 padding，输入长度必须 16 字节对齐。",
    part: "分区名 (partition name)",
    version: "固件版本 (firmware version)",
    product: "产品码 (product code)",
    outputName: "输出文件名 (output file name)",
    buttonLoading: "加载工具中...",
    buttonReady: "执行并下载",
    buttonFailed: "工具加载失败",
    statusTitle: "状态",
    statusLoading: "正在加载 Pyodide...",
    statusLoadingRuntime: "正在加载 Pyodide 运行环境...",
    statusReady: "工具已就绪。请选择操作模式和输入文件。",
    statusReading: "正在读取输入文件...",
    statusBuilding: "正在浏览器本地处理...",
    statusPackSuccess: "生成完成：{name}\n原始固件: {rawSize} bytes\nRBL 包体: {pkgSize} bytes\n输出文件: {outputSize} bytes\ncrypt={crypt}, cmprs={cmprs}, algo2={algo2}",
    statusUnpackSuccess: "还原完成：{name}\nRBL 包体: {pkgSize} bytes\n还原固件: {rawSize} bytes\ncrypt={crypt}, cmprs={cmprs}, algo2={algo2}\npart={part}, version={version}, product={product}",
    statusBuildFailed: "处理失败：{message}",
    statusLoadFailed: "工具加载失败：{message}",
    errorPyodideNotReady: "Pyodide 尚未就绪",
    errorFileRequired: "{id} 是必选文件",
    errorLoadPython: "加载 package_tool_web.py 失败：{status}",
  },
  en: {
    pageTitle: "QBoot RBL Packager / Unpacker",
    eyebrow: "QBoot Web Tool",
    title: "RBL Packager / Unpacker",
    subtitle: "Run RBL header generation, gzip compression/decompression, AES-CBC encryption/decryption, and firmware restore locally in your browser. Files are not uploaded to a server.",
    noticeTitle: "Scope note:",
    noticeBody: "Automatic packaging follows the reverse order of the QBoot release path: compress first, then encrypt. Unpack mode validates the RBL header and body CRC, then decrypts and decompresses according to the header algorithm fields. The browser currently performs real none, gzip, fastlz, quicklz, and aes transforms. Differential mode fixes the RBL cmprs field to hpatchlite, and the patch body can be left uncompressed or compressed through HPatchLite's own _CompressPlugin_tuz path.",
    operation: "Operation",
    operationPackAuto: "Process raw firmware and package",
    operationPackManual: "Wrap a prepared package body",
    operationUnpack: "Unpack and restore firmware",
    operationPatchPack: "Build HPatchLite differential package",
    operationPatchUnpack: "Apply HPatchLite differential package",
    rawFirmware: "Raw firmware",
    rawFirmwareHint: "Used to build the body in automatic mode; used for raw_crc and raw_size in manual mode; used as the new firmware in differential package mode.",
    oldFirmware: "Old firmware",
    oldFirmwareHint: "Required for differential modes. It is the old image when building a patch and the restore baseline when applying a patch.",
    hpatchCompress: "HPatchLite patch compression",
    hpatchCompressHint: "The differential package body can use none or HPatchLite's own _CompressPlugin_tuz path. This is not outer RBL gzip/zlib and not generic raw-deflate.",
    pkgBody: "Package body to append",
    pkgBodyHint: "Required only in manual package-body mode. This file is appended directly after the RBL header.",
    rblPackage: "RBL package",
    rblPackageHint: "Required in unpack mode. The tool validates the header and body CRC, then restores the raw firmware when the algorithms are supported.",
    chooseFile: "Choose file",
    noFileSelected: "No file selected",
    crypt: "Crypt",
    compress: "Compress",
    algo2: "Verify algo2",
    aesKey: "AES key",
    aesKeyHint: "Defaults to QBOOT_AES_KEY. Use plain text or a hex: prefix; exactly 32 bytes are required.",
    aesIv: "AES IV",
    aesIvHint: "Defaults to QBOOT_AES_IV. Use plain text or a hex: prefix; exactly 16 bytes are required. AES uses no padding, so input must be 16-byte aligned.",
    part: "Partition name",
    version: "Firmware version",
    product: "Product code",
    outputName: "Output file name",
    buttonLoading: "Loading tool...",
    buttonReady: "Run and download",
    buttonFailed: "Tool failed to load",
    statusTitle: "Status",
    statusLoading: "Loading Pyodide...",
    statusLoadingRuntime: "Loading Pyodide runtime...",
    statusReady: "Tool is ready. Select an operation and input files.",
    statusReading: "Reading input files...",
    statusBuilding: "Processing locally in the browser...",
    statusPackSuccess: "Generated: {name}\nraw firmware: {rawSize} bytes\nRBL body: {pkgSize} bytes\noutput: {outputSize} bytes\ncrypt={crypt}, cmprs={cmprs}, algo2={algo2}",
    statusUnpackSuccess: "Restored: {name}\nRBL body: {pkgSize} bytes\nraw firmware: {rawSize} bytes\ncrypt={crypt}, cmprs={cmprs}, algo2={algo2}\npart={part}, version={version}, product={product}",
    statusBuildFailed: "Processing failed: {message}",
    statusLoadFailed: "Tool failed to load: {message}",
    errorPyodideNotReady: "Pyodide is not ready yet",
    errorFileRequired: "{id} is required",
    errorLoadPython: "failed to load package_tool_web.py: {status}",
  },
};

let pyodide = null;
let currentLanguage = detectInitialLanguage();
let currentStatusKey = "statusLoading";
let currentStatusArgs = {};
let toolLoaded = false;
let toolLoadFailed = false;

function readSavedLanguage() {
  try {
    return window.localStorage.getItem("qboot-package-tool-lang");
  } catch (_) {
    return null;
  }
}

function saveLanguage(lang) {
  try {
    window.localStorage.setItem("qboot-package-tool-lang", lang);
  } catch (_) {
    /* Ignore storage failures and keep the in-memory language switch active. */
  }
}

function normalizeLanguage(lang) {
  if (lang === "en") {
    return "en";
  }
  if (lang === "zh" || lang === "zh-CN") {
    return "zh-CN";
  }
  return null;
}

function detectInitialLanguage() {
  const params = new URLSearchParams(window.location.search);
  const lang = normalizeLanguage(params.get("lang")) || normalizeLanguage(readSavedLanguage());
  if (lang !== null) {
    return lang;
  }
  return navigator.language && navigator.language.toLowerCase().startsWith("en") ? "en" : "zh-CN";
}

function t(key, values = {}) {
  const text = (translations[currentLanguage] && translations[currentLanguage][key]) || translations["zh-CN"][key] || key;
  return text.replace(/\{([A-Za-z0-9_]+)\}/g, (_, name) => String(values[name] ?? ""));
}

function setStatus(key, values = {}) {
  currentStatusKey = key;
  currentStatusArgs = values;
  statusBox.textContent = t(key, values);
}

function applyLanguage(lang) {
  currentLanguage = lang;
  document.documentElement.lang = lang;
  document.title = t("pageTitle");
  saveLanguage(lang);

  document.querySelectorAll("[data-i18n]").forEach((node) => {
    node.textContent = t(node.dataset.i18n);
  });
  langButtons.forEach((button) => {
    const active = button.dataset.lang === lang;
    button.classList.toggle("active", active);
    button.setAttribute("aria-pressed", active ? "true" : "false");
  });

  if (toolLoadFailed) {
    buildButton.textContent = t("buttonFailed");
  } else if (toolLoaded) {
    buildButton.textContent = t("buttonReady");
  } else {
    buildButton.textContent = t("buttonLoading");
  }
  statusBox.textContent = t(currentStatusKey, currentStatusArgs);
  refreshFileLabels();
}

function getInput(id) {
  return document.getElementById(id);
}

function getFileNameLabel(input) {
  return document.querySelector(`[data-file-name="${input.id}"]`);
}

function refreshFileName(input) {
  const label = getFileNameLabel(input);
  if (label === null) {
    return;
  }

  const file = input.files[0];
  if (file) {
    label.textContent = file.name;
    label.removeAttribute("data-i18n");
  } else {
    label.dataset.i18n = "noFileSelected";
    label.textContent = t("noFileSelected");
  }
}

function refreshFileLabels() {
  fileInputs.forEach((input) => refreshFileName(input));
}

function requireFileInput(id, required) {
  const input = getInput(id);
  input.required = required;
  input.disabled = !required;
}

function setSectionVisible(section, visible) {
  section.hidden = !visible;
}

function updateFormState() {
  const operation = getInput("operation").value;
  const crypt = getInput("crypt").value;

  modeSections.forEach((section) => {
    const modes = section.dataset.modeSection.split(" ");
    setSectionVisible(section, modes.includes(operation));
  });
  packOnlySections.forEach((section) => {
    setSectionVisible(section, operation !== "unpack" && operation !== "patch-unpack");
  });
  compressFieldSections.forEach((section) => {
    setSectionVisible(section, operation !== "patch-pack");
  });

  if (operation === "patch-pack") {
    getInput("cmprs").value = "hpatchlite";
    getInput("algo2").value = "none";
  } else if (operation === "pack-auto" && getInput("cmprs").value === "hpatchlite") {
    getInput("cmprs").value = "none";
  }

  requireFileInput("raw-file", operation === "pack-auto" || operation === "pack-manual" || operation === "patch-pack");
  requireFileInput("old-file", operation === "patch-pack" || operation === "patch-unpack");
  requireFileInput("pkg-file", operation === "pack-manual");
  requireFileInput("rbl-file", operation === "unpack" || operation === "patch-unpack");

  document.querySelectorAll("[data-aes-only]").forEach((section) => {
    setSectionVisible(section, crypt === "aes" || operation === "unpack" || operation === "patch-unpack");
  });
}

function getFirstFile(id) {
  const file = getInput(id).files[0];
  if (!file) {
    throw new Error(t("errorFileRequired", { id }));
  }
  return file;
}

function sanitizeOutputName(name, fallback, ext) {
  const trimmed = name.trim() || fallback;
  return trimmed.endsWith(ext) ? trimmed : `${trimmed}${ext}`;
}

async function loadPackager() {
  setStatus("statusLoadingRuntime");
  pyodide = await loadPyodide();

  const response = await fetch("package_tool_web.py", { cache: "no-cache" });
  if (!response.ok) {
    throw new Error(t("errorLoadPython", { status: response.status }));
  }

  pyodide.runPython(await response.text());
  toolLoaded = true;
  buildButton.disabled = false;
  buildButton.textContent = t("buttonReady");
  setStatus("statusReady");
}

function downloadBytes(bytes, name) {
  const blob = new Blob([bytes], { type: "application/octet-stream" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");

  link.href = url;
  link.download = name;
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
}

function getRequest() {
  const operation = getInput("operation").value;
  const isPatchPack = operation === "patch-pack";

  return {
    operation,
    crypt: getInput("crypt").value,
    cmprs: isPatchPack ? "hpatchlite" : getInput("cmprs").value,
    algo2: isPatchPack ? "none" : getInput("algo2").value,
    part: getInput("part").value,
    version: getInput("version").value,
    product: getInput("product").value,
    aesKey: getInput("aes-key").value,
    aesIv: getInput("aes-iv").value,
    patchCompress: getInput("hpatch-compress").value,
  };
}

async function processPackage(request) {
  const rawFile = getFirstFile("raw-file");
  const rawBytes = new Uint8Array(await rawFile.arrayBuffer());
  pyodide.FS.writeFile("/qboot-raw.bin", rawBytes);

  if (request.operation === "pack-manual") {
    const pkgFile = getFirstFile("pkg-file");
    const pkgBytes = new Uint8Array(await pkgFile.arrayBuffer());
    pyodide.FS.writeFile("/qboot-pkg.bin", pkgBytes);
  }

  if (request.operation === "patch-pack") {
    const oldFile = getFirstFile("old-file");
    const oldBytes = new Uint8Array(await oldFile.arrayBuffer());
    pyodide.FS.writeFile("/qboot-old.bin", oldBytes);
  }

  pyodide.globals.set("qboot_request_json", JSON.stringify(request));
  const resultJson = pyodide.runPython(`
import json
from pathlib import Path
_request = json.loads(qboot_request_json)
_raw = Path('/qboot-raw.bin').read_bytes()
if _request['operation'] == 'pack-manual':
    _pkg = Path('/qboot-pkg.bin').read_bytes()
    _rbl = package_rbl_bytes(
        raw_fw=_raw,
        pkg_obj=_pkg,
        crypt=_request['crypt'],
        cmprs=_request['cmprs'],
        algo2=_request['algo2'],
        part=_request['part'],
        version=_request['version'],
        product=_request['product'],
    )
elif _request['operation'] == 'patch-pack':
    _old = Path('/qboot-old.bin').read_bytes()
    _rbl = package_hpatchlite_rbl_bytes(
        old_fw=_old,
        new_fw=_raw,
        crypt=_request['crypt'],
        algo2=_request['algo2'],
        part=_request['part'],
        version=_request['version'],
        product=_request['product'],
        aes_key=_request['aesKey'],
        aes_iv=_request['aesIv'],
        patch_compress=_request['patchCompress'],
    )
else:
    _rbl = package_firmware_bytes(
        raw_fw=_raw,
        crypt=_request['crypt'],
        cmprs=_request['cmprs'],
        algo2=_request['algo2'],
        part=_request['part'],
        version=_request['version'],
        product=_request['product'],
        aes_key=_request['aesKey'],
        aes_iv=_request['aesIv'],
        patch_compress=_request['patchCompress'],
    )
_header = parse_rbl_header(_rbl)
Path('/qboot-output.rbl').write_bytes(_rbl)
json.dumps({
    'raw_size': len(_raw),
    'pkg_size': _header['pkg_size'],
    'output_size': len(_rbl),
})
`);
  const outputBytes = pyodide.FS.readFile("/qboot-output.rbl");
  const outputName = sanitizeOutputName(getInput("output-name").value, "qboot-package.rbl", ".rbl");
  downloadBytes(outputBytes, outputName);
  return { outputName, ...JSON.parse(resultJson) };
}

async function processUnpack(request) {
  const rblFile = getFirstFile("rbl-file");
  const rblBytes = new Uint8Array(await rblFile.arrayBuffer());
  pyodide.FS.writeFile("/qboot-input.rbl", rblBytes);

  if (request.operation === "patch-unpack") {
    const oldFile = getFirstFile("old-file");
    const oldBytes = new Uint8Array(await oldFile.arrayBuffer());
    pyodide.FS.writeFile("/qboot-old.bin", oldBytes);
  }

  pyodide.globals.set("qboot_request_json", JSON.stringify(request));

  const resultJson = pyodide.runPython(`
import json
from pathlib import Path
_request = json.loads(qboot_request_json)
_rbl = Path('/qboot-input.rbl').read_bytes()
if _request['operation'] == 'patch-unpack':
    _old = Path('/qboot-old.bin').read_bytes()
    _raw, _header = unpack_hpatchlite_rbl_bytes(
        _old,
        _rbl,
        aes_key=_request['aesKey'],
        aes_iv=_request['aesIv'],
        patch_compress=_request['patchCompress'],
    )
else:
    _raw, _header = unpack_rbl_bytes(
        _rbl,
        aes_key=_request['aesKey'],
        aes_iv=_request['aesIv'],
        patch_compress=_request['patchCompress'],
    )
Path('/qboot-restored.bin').write_bytes(_raw)
json.dumps({
    'raw_size': len(_raw),
    'pkg_size': _header['pkg_size'],
    'crypt': _header['crypt'],
    'cmprs': _header['cmprs'],
    'algo2': _header['algo2_name'],
    'part': _header['part'],
    'version': _header['version'],
    'product': _header['product'],
})
`);
  const outputBytes = pyodide.FS.readFile("/qboot-restored.bin");
  const outputName = sanitizeOutputName(getInput("output-name").value, "qboot-restored.bin", ".bin");
  downloadBytes(outputBytes, outputName);
  return { outputName, ...JSON.parse(resultJson) };
}

async function buildPackage(event) {
  event.preventDefault();

  try {
    if (pyodide === null) {
      throw new Error(t("errorPyodideNotReady"));
    }

    buildButton.disabled = true;
    setStatus("statusReading");
    const request = getRequest();

    setStatus("statusBuilding");
    if (request.operation === "unpack" || request.operation === "patch-unpack") {
      const result = await processUnpack(request);
      setStatus("statusUnpackSuccess", {
        name: result.outputName,
        rawSize: result.raw_size,
        pkgSize: result.pkg_size,
        crypt: result.crypt,
        cmprs: result.cmprs,
        algo2: result.algo2,
        part: result.part,
        version: result.version,
        product: result.product,
      });
    } else {
      const result = await processPackage(request);
      setStatus("statusPackSuccess", {
        name: result.outputName,
        rawSize: result.raw_size,
        pkgSize: result.pkg_size,
        outputSize: result.output_size,
        crypt: request.crypt,
        cmprs: request.cmprs,
        algo2: request.algo2,
      });
    }
  } catch (error) {
    setStatus("statusBuildFailed", { message: error.message });
  } finally {
    buildButton.disabled = false;
  }
}

langButtons.forEach((button) => {
  button.addEventListener("click", () => applyLanguage(button.dataset.lang));
});
fileTriggerButtons.forEach((button) => {
  button.addEventListener("click", () => getInput(button.dataset.fileTrigger).click());
});
fileInputs.forEach((input) => {
  input.addEventListener("change", () => refreshFileName(input));
});
["operation", "crypt"].forEach((id) => {
  getInput(id).addEventListener("change", updateFormState);
});
form.addEventListener("submit", buildPackage);
applyLanguage(currentLanguage);
updateFormState();

loadPackager().catch((error) => {
  toolLoadFailed = true;
  buildButton.disabled = true;
  buildButton.textContent = t("buttonFailed");
  setStatus("statusLoadFailed", { message: error.message });
});
