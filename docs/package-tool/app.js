const form = document.getElementById("packager-form");
const buildButton = document.getElementById("build-button");
const statusBox = document.getElementById("status");
const langButtons = document.querySelectorAll("[data-lang]");
const fileInputs = document.querySelectorAll(".file-input");
const fileTriggerButtons = document.querySelectorAll("[data-file-trigger]");

const translations = {
  "zh-CN": {
    pageTitle: "QBoot RBL 打包工具",
    eyebrow: "QBoot Web Tool",
    title: "RBL 打包工具",
    subtitle: "上传原始固件和要写入 RBL 的包体文件，在浏览器本地生成 .rbl 文件并下载。文件不会上传到服务器。",
    noticeTitle: "语义说明：",
    noticeBody: "当前工具复现 tools/package_tool.py 的行为：生成 RBL header 并追加你选择的包体文件。无压缩/无加密时，包体文件通常与原始固件相同；选择 gzip、quicklz、aes 等算法时，请先用对应工具生成处理后的包体文件。",
    rawFirmware: "原始固件",
    rawFirmwareHint: "用于写入 raw_crc 和 raw_size 的原始固件。",
    pkgBody: "要写入 RBL 的包体",
    pkgBodyHint: "最终会直接追加到 RBL header 后面。无压缩/无加密时通常选择与原始固件相同的文件。",
    chooseFile: "选择文件",
    noFileSelected: "未选择任何文件",
    crypt: "加密算法字段 (crypt)",
    compress: "压缩算法字段 (compress)",
    algo2: "校验算法字段 (algo2)",
    part: "分区名 (partition name)",
    version: "固件版本 (firmware version)",
    product: "产品码 (product code)",
    outputName: "输出文件名 (output file name)",
    buttonLoading: "加载工具中...",
    buttonReady: "生成并下载 .rbl",
    buttonFailed: "工具加载失败",
    statusTitle: "状态",
    statusLoading: "正在加载 Pyodide...",
    statusLoadingRuntime: "正在加载 Pyodide 运行环境...",
    statusReady: "工具已就绪。请选择原始固件和要写入 RBL 的包体文件。",
    statusReading: "正在读取输入文件...",
    statusBuilding: "正在浏览器本地生成 RBL 文件...",
    statusSuccess: "生成完成：{name}\n原始固件: {rawSize} bytes\nRBL 包体: {pkgSize} bytes\n输出文件: {outputSize} bytes\ncrypt={crypt}, cmprs={cmprs}, algo2={algo2}",
    statusBuildFailed: "生成失败：{message}",
    statusLoadFailed: "工具加载失败：{message}",
    errorPyodideNotReady: "Pyodide 尚未就绪",
    errorFileRequired: "{id} 是必选文件",
    errorLoadPython: "加载 package_tool_web.py 失败：{status}",
  },
  en: {
    pageTitle: "QBoot RBL Packager",
    eyebrow: "QBoot Web Tool",
    title: "RBL Packager",
    subtitle: "Upload a raw firmware image and the package body to append to the RBL file, then generate and download an .rbl file locally in your browser. Files are not uploaded to a server.",
    noticeTitle: "Scope note:",
    noticeBody: "This page mirrors tools/package_tool.py: it creates an RBL header and appends the selected package body. For none/none packaging, the package body is usually the same file as the raw firmware. If gzip, quicklz, aes, or another algorithm is selected, generate the processed package body with the matching tool first.",
    rawFirmware: "Raw firmware",
    rawFirmwareHint: "The original firmware used for raw_crc and raw_size in the RBL header.",
    pkgBody: "Package body to append",
    pkgBodyHint: "This file is appended directly after the RBL header. For none/none packaging, it is usually the same file as the raw firmware.",
    chooseFile: "Choose file",
    noFileSelected: "No file selected",
    crypt: "Crypt",
    compress: "Compress",
    algo2: "Verify algo2",
    part: "Partition name",
    version: "Firmware version",
    product: "Product code",
    outputName: "Output file name",
    buttonLoading: "Loading tool...",
    buttonReady: "Generate and download .rbl",
    buttonFailed: "Tool failed to load",
    statusTitle: "Status",
    statusLoading: "Loading Pyodide...",
    statusLoadingRuntime: "Loading Pyodide runtime...",
    statusReady: "Tool is ready. Select the raw firmware and the package body to append.",
    statusReading: "Reading input files...",
    statusBuilding: "Generating the RBL file locally in the browser...",
    statusSuccess: "Generated: {name}\nraw firmware: {rawSize} bytes\nRBL body: {pkgSize} bytes\noutput: {outputSize} bytes\ncrypt={crypt}, cmprs={cmprs}, algo2={algo2}",
    statusBuildFailed: "Generation failed: {message}",
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

function getInput(id) {
  return document.getElementById(id);
}

function getFirstFile(id) {
  const file = getInput(id).files[0];
  if (!file) {
    throw new Error(t("errorFileRequired", { id }));
  }
  return file;
}

function sanitizeOutputName(name) {
  const trimmed = name.trim();
  if (!trimmed) {
    return "qboot-package.rbl";
  }
  return trimmed.endsWith(".rbl") ? trimmed : `${trimmed}.rbl`;
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

async function buildPackage(event) {
  event.preventDefault();

  try {
    if (pyodide === null) {
      throw new Error(t("errorPyodideNotReady"));
    }

    buildButton.disabled = true;
    setStatus("statusReading");

    const rawFile = getFirstFile("raw-file");
    const pkgFile = getFirstFile("pkg-file");
    const rawBytes = new Uint8Array(await rawFile.arrayBuffer());
    const pkgBytes = new Uint8Array(await pkgFile.arrayBuffer());
    const request = {
      crypt: getInput("crypt").value,
      cmprs: getInput("cmprs").value,
      algo2: getInput("algo2").value,
      part: getInput("part").value,
      version: getInput("version").value,
      product: getInput("product").value,
    };

    pyodide.FS.writeFile("/qboot-raw.bin", rawBytes);
    pyodide.FS.writeFile("/qboot-pkg.bin", pkgBytes);
    pyodide.globals.set("qboot_request_json", JSON.stringify(request));

    setStatus("statusBuilding");
    const outputSize = pyodide.runPython(`
import json
from pathlib import Path
_request = json.loads(qboot_request_json)
_raw = Path('/qboot-raw.bin').read_bytes()
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
Path('/qboot-output.rbl').write_bytes(_rbl)
len(_rbl)
`);
    const outputBytes = pyodide.FS.readFile("/qboot-output.rbl");
    const outputName = sanitizeOutputName(getInput("output-name").value);
    downloadBytes(outputBytes, outputName);

    setStatus("statusSuccess", {
      name: outputName,
      rawSize: rawBytes.length,
      pkgSize: pkgBytes.length,
      outputSize,
      crypt: request.crypt,
      cmprs: request.cmprs,
      algo2: request.algo2,
    });
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
form.addEventListener("submit", buildPackage);
applyLanguage(currentLanguage);

loadPackager().catch((error) => {
  toolLoadFailed = true;
  buildButton.disabled = true;
  buildButton.textContent = t("buttonFailed");
  setStatus("statusLoadFailed", { message: error.message });
});
