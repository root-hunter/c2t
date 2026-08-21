import { patchBinary } from "./provision.mjs";

const RELEASE_METADATA_URL = "./release.json";
const ASSET_NAMES = {
  linux: "c2t-linux-x86_64",
  windows: "c2t-windows-x86_64.exe",
};

const form = document.querySelector("#config-form");
const downloadButton = document.querySelector("#download-button");
const buttonPlatform = document.querySelector("#button-platform");
const releaseLabel = document.querySelector("#release-label");
const releaseLink = document.querySelector("#release-link");
const status = document.querySelector("#status");
const tokenInput = document.querySelector("#bot-token");
const localBinaryInput = document.querySelector("#local-binary");
let latestRelease = null;
let localBinary = null;

function selectedPlatform() {
  return new FormData(form).get("platform");
}

function setStatus(message, kind = "") {
  status.textContent = message;
  status.className = `status ${kind}`.trim();
}

function updatePlatform() {
  const platform = selectedPlatform();
  buttonPlatform.textContent = platform === "windows" ? "Windows x86_64" : "Linux x86_64";
  const assetAvailable = latestRelease?.assets.some(({ name }) => name === ASSET_NAMES[platform]);
  downloadButton.disabled = !localBinary && !assetAvailable;
  if (!localBinary && latestRelease && !assetAvailable) {
    setStatus(`Release ${latestRelease.tag_name} has no direct ${platform} asset yet. Use a local file or publish a new release.`, "error");
  } else if (latestRelease && !localBinary) {
    setStatus(`${latestRelease.tag_name} ready · configuration is applied in memory only`, "success");
  }
}

async function loadRelease() {
  try {
    const response = await fetch(RELEASE_METADATA_URL, {
      cache: "no-store",
    });
    if (!response.ok) throw new Error(`Release metadata: ${response.status}`);
    latestRelease = await response.json();
    releaseLabel.textContent = latestRelease.tag_name;
    releaseLink.href = latestRelease.html_url;
    updatePlatform();
  } catch (error) {
    releaseLabel.textContent = "Unavailable";
    downloadButton.disabled = !localBinary;
    setStatus(`Could not load the latest release: ${error.message}. You can use a local file instead.`, "error");
  }
}

function collectConfig() {
  const config = {};
  for (const toggle of form.querySelectorAll('input[type="checkbox"][data-key]')) {
    config[toggle.dataset.key] = toggle.checked ? "1" : "0";
  }
  for (const input of form.querySelectorAll('input[type="number"][data-key]')) {
    if (input.value !== "") config[input.dataset.key] = input.value;
  }

  const telegramEnabled = config.TELEGRAM_ENABLED === "1";
  const token = tokenInput.value.trim();
  const chatId = document.querySelector("#chat-id").value.trim();
  if (telegramEnabled && !token) throw new Error("Enter the Telegram bot token");
  if (telegramEnabled && !chatId) throw new Error("Enter the Telegram chat ID");
  if (token) config.TELEGRAM_BOT_TOKEN = token;
  if (chatId) config.TELEGRAM_CHAT_ID = chatId;
  return config;
}

async function downloadAsset(asset) {
  const response = await fetch(asset.browser_download_url, { cache: "no-store" });
  if (!response.ok) throw new Error(`Download GitHub: ${response.status}`);
  if (!response.body) return new Uint8Array(await response.arrayBuffer());

  const assetSize = Number(asset.size);
  const responseSize = Number(response.headers.get("content-length"));
  const total = assetSize > 0 ? assetSize : (responseSize > 0 ? responseSize : 0);
  const reader = response.body.getReader();
  const chunks = [];
  let received = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
    received += value.length;
    const percentage = total ? Math.min(100, Math.round((received / total) * 100)) : 0;
    const progress = total ? ` ${percentage}%` : "";
    setStatus(`Downloading the official release binary…${progress}`);
  }
  const result = new Uint8Array(received);
  let offset = 0;
  for (const chunk of chunks) {
    result.set(chunk, offset);
    offset += chunk.length;
  }
  return result;
}

function saveBinary(binary, filename) {
  const blob = new Blob([binary], { type: "application/octet-stream" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  document.body.append(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

form.addEventListener("change", (event) => {
  if (event.target.name === "platform") {
    localBinary = null;
    localBinaryInput.value = "";
    updatePlatform();
  }
});

document.querySelector("#reveal-token").addEventListener("click", (event) => {
  const revealing = tokenInput.type === "password";
  tokenInput.type = revealing ? "text" : "password";
  event.currentTarget.textContent = revealing ? "Hide" : "Show";
  event.currentTarget.setAttribute("aria-label", revealing ? "Hide token" : "Show token");
});

localBinaryInput.addEventListener("change", async () => {
  const file = localBinaryInput.files[0];
  if (!file) return;
  try {
    localBinary = new Uint8Array(await file.arrayBuffer());
    const inferredPlatform = localBinary[0] === 0x4d && localBinary[1] === 0x5a
      ? "windows"
      : "linux";
    form.querySelector(`input[name="platform"][value="${inferredPlatform}"]`).checked = true;
    buttonPlatform.textContent = inferredPlatform === "windows" ? "Windows x86_64" : "Linux x86_64";
    downloadButton.disabled = false;
    setStatus(`Local file “${file.name}” ready · nothing was uploaded`, "success");
  } catch (error) {
    localBinary = null;
    setStatus(`Could not read the file: ${error.message}`, "error");
  }
});

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  downloadButton.disabled = true;
  try {
    const platform = selectedPlatform();
    const config = collectConfig();
    let source = localBinary;
    if (!source) {
      const asset = latestRelease?.assets.find(({ name }) => name === ASSET_NAMES[platform]);
      if (!asset) throw new Error("The release does not contain a compatible direct binary");
      source = await downloadAsset(asset);
    }
    setStatus("Validating and writing the configuration…");
    const patched = patchBinary(source, config);
    const version = latestRelease?.tag_name ?? "custom";
    const filename = platform === "windows"
      ? `c2t-${version}-configured.exe`
      : `c2t-${version}-configured-linux-x86_64`;
    saveBinary(patched, filename);
    setStatus(`Done · ${Object.keys(config).length} embedded values · download started`, "success");
  } catch (error) {
    setStatus(error.message, "error");
  } finally {
    const platform = selectedPlatform();
    const assetAvailable = latestRelease?.assets.some(({ name }) => name === ASSET_NAMES[platform]);
    downloadButton.disabled = !localBinary && !assetAvailable;
  }
});

loadRelease();
