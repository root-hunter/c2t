import { patchBinary } from "./provision.mjs";

const REPOSITORY = "root-hunter/c2t";
const API_URL = `https://api.github.com/repos/${REPOSITORY}/releases/latest`;
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
    setStatus(`La release ${latestRelease.tag_name} non contiene ancora l'asset diretto per ${platform}. Usa un file locale o pubblica una nuova release.`, "error");
  } else if (latestRelease && !localBinary) {
    setStatus(`${latestRelease.tag_name} pronta · configurazione eseguita solo in memoria`, "success");
  }
}

async function loadRelease() {
  try {
    const response = await fetch(API_URL, {
      headers: { Accept: "application/vnd.github+json" },
      cache: "no-store",
    });
    if (!response.ok) throw new Error(`GitHub API: ${response.status}`);
    latestRelease = await response.json();
    releaseLabel.textContent = latestRelease.tag_name;
    releaseLink.href = latestRelease.html_url;
    updatePlatform();
  } catch (error) {
    releaseLabel.textContent = "non disponibile";
    downloadButton.disabled = !localBinary;
    setStatus(`Non riesco a leggere l'ultima release: ${error.message}. Puoi usare un file locale.`, "error");
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
  if (telegramEnabled && !token) throw new Error("Inserisci il bot token Telegram");
  if (telegramEnabled && !chatId) throw new Error("Inserisci il chat ID Telegram");
  if (token) config.TELEGRAM_BOT_TOKEN = token;
  if (chatId) config.TELEGRAM_CHAT_ID = chatId;
  return config;
}

async function downloadAsset(asset) {
  const response = await fetch(asset.browser_download_url, { cache: "no-store" });
  if (!response.ok) throw new Error(`Download GitHub: ${response.status}`);
  if (!response.body) return new Uint8Array(await response.arrayBuffer());

  const total = Number(response.headers.get("content-length")) || 0;
  const reader = response.body.getReader();
  const chunks = [];
  let received = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
    received += value.length;
    const progress = total ? ` ${Math.round((received / total) * 100)}%` : "";
    setStatus(`Download del binario ufficiale…${progress}`);
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
  event.currentTarget.textContent = revealing ? "Nascondi" : "Mostra";
  event.currentTarget.setAttribute("aria-label", revealing ? "Nascondi token" : "Mostra token");
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
    setStatus(`File locale “${file.name}” pronto · nessun upload effettuato`, "success");
  } catch (error) {
    localBinary = null;
    setStatus(`Lettura del file fallita: ${error.message}`, "error");
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
      if (!asset) throw new Error("La release non contiene un binario diretto compatibile");
      source = await downloadAsset(asset);
    }
    setStatus("Validazione e scrittura della configurazione…");
    const patched = patchBinary(source, config);
    const version = latestRelease?.tag_name ?? "custom";
    const filename = platform === "windows"
      ? `c2t-${version}-configured.exe`
      : `c2t-${version}-configured-linux-x86_64`;
    saveBinary(patched, filename);
    setStatus(`Completato · ${Object.keys(config).length} valori embedded · il file è in download`, "success");
  } catch (error) {
    setStatus(error.message, "error");
  } finally {
    const platform = selectedPlatform();
    const assetAvailable = latestRelease?.assets.some(({ name }) => name === ASSET_NAMES[platform]);
    downloadButton.disabled = !localBinary && !assetAvailable;
  }
});

loadRelease();
