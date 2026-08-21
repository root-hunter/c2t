import { encodePayload, patchBinary } from "./provision.mjs";
import { inferPlatform, PLATFORMS } from "./platforms.mjs";
import { createExecutableTarGz, createMacOSBundleTarGz } from "./archive.mjs";

const RELEASE_METADATA_URL = "./release.json";

const form = document.querySelector("#config-form");
const downloadButton = document.querySelector("#download-button");
const buttonPlatform = document.querySelector("#button-platform");
const releaseLabel = document.querySelector("#release-label");
const releaseLink = document.querySelector("#release-link");
const status = document.querySelector("#status");
const tokenInput = document.querySelector("#bot-token");
const localBinaryInput = document.querySelector("#local-binary");
const linuxFormat = document.querySelector("#linux-format");
const linuxFormatNote = document.querySelector("#linux-format-note");
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
  const details = PLATFORMS[platform];
  buttonPlatform.textContent = details.label;
  linuxFormat.hidden = !details.formatChoice;
  linuxFormatNote.hidden = !details.formatChoice;
  const assetAvailable = latestRelease?.assets.some(({ name }) => name === details.asset);
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

function saveDownload(binary, filename, type = "application/octet-stream") {
  const blob = new Blob([binary], { type });
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

const pairButton = document.querySelector("#pair-telegram");
const chatIdInput = document.querySelector("#chat-id");

if (pairButton) {
  pairButton.addEventListener("click", async () => {
    const token = tokenInput.value.trim();
    if (!token) {
      setStatus("Please enter your Telegram bot token first.", "error");
      tokenInput.focus();
      return;
    }

    try {
      pairButton.disabled = true;
      setStatus("Connecting to Telegram Bot API…");
      
      const meResp = await fetch(`https://api.telegram.org/bot${token}/getMe`);
      if (!meResp.ok) throw new Error("Invalid bot token or network error");
      const meData = await meResp.json();
      if (!meData.ok || !meData.result?.username) {
        throw new Error("Could not fetch bot username");
      }

      const botUsername = meData.result.username;
      const code = `c2t_${Math.random().toString(36).substring(2, 10)}`;
      const pairingUrl = `https://t.me/${botUsername}?start=${code}`;

      setStatus(`Opening Telegram (@${botUsername})… Please click Start in Telegram.`, "success");
      window.open(pairingUrl, "_blank");

      let offset = 0;
      let paired = false;
      const maxAttempts = 30;

      for (let attempt = 0; attempt < maxAttempts; attempt++) {
        setStatus(`Waiting for Telegram pairing message (${maxAttempts - attempt}s)…`);
        try {
          const updatesResp = await fetch(`https://api.telegram.org/bot${token}/getUpdates?offset=${offset}&timeout=2`);
          if (updatesResp.ok) {
            const updatesData = await updatesResp.json();
            if (updatesData.ok && updatesData.result?.length > 0) {
              for (const update of updatesData.result) {
                offset = update.update_id + 1;
                const msg = update.message;
                if (msg?.chat?.id) {
                  const chatId = String(msg.chat.id);
                  const username = msg.chat.username ? `@${msg.chat.username}` : (msg.chat.first_name || "user");
                  chatIdInput.value = chatId;
                  paired = true;

                  fetch(`https://api.telegram.org/bot${token}/sendMessage`, {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({
                      chat_id: chatId,
                      text: `✅ c2t Web Configurator paired successfully!\nDevice connected to ${username} (Chat ID: ${chatId}).`
                    })
                  }).catch(() => {});

                  setStatus(`Successfully paired with ${username} (Chat ID: ${chatId})!`, "success");
                  break;
                }
              }
            }
          }
        } catch (_) {}

        if (paired) break;
        await new Promise((r) => setTimeout(r, 2000));
      }

      if (!paired) {
        setStatus("Pairing timed out. Please try again.", "error");
      }
    } catch (err) {
      setStatus(`Pairing failed: ${err.message}`, "error");
    } finally {
      pairButton.disabled = false;
    }
  });
}

localBinaryInput.addEventListener("change", async () => {
  const file = localBinaryInput.files[0];
  if (!file) return;
  try {
    localBinary = new Uint8Array(await file.arrayBuffer());
    const inferredPlatform = inferPlatform(localBinary);
    form.querySelector(`input[name="platform"][value="${inferredPlatform}"]`).checked = true;
    buttonPlatform.textContent = PLATFORMS[inferredPlatform].label;
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
      const asset = latestRelease?.assets.find(({ name }) => name === PLATFORMS[platform].asset);
      if (!asset) throw new Error("The release does not contain a compatible direct binary");
      source = await downloadAsset(asset);
    }
    setStatus("Validating and writing the configuration…");
    const version = latestRelease?.tag_name ?? "custom";
    const details = PLATFORMS[platform];
    const executableName = `c2t-${version}-configured-${details.filenameSuffix}`;
    if (details.configMode === "sidecar") {
      setStatus("Packaging the signed executable and its private configuration…");
      const archive = await createMacOSBundleTarGz(
        source,
        encodePayload(config),
        executableName,
      );
      saveDownload(archive, `${executableName}.tar.gz`, "application/gzip");
      setStatus(`Done · ${Object.keys(config).length} configuration values · keep the executable and .c2t.env together`, "success");
      return;
    }
    const patched = patchBinary(source, config);
    const archiveLinux = details.formatChoice && new FormData(form).get("linux-format") === "archive";
    if (archiveLinux) {
      setStatus("Packaging the configured executable…");
      const archive = await createExecutableTarGz(patched, executableName);
      saveDownload(archive, `${executableName}.tar.gz`, "application/gzip");
      setStatus(`Done · ${Object.keys(config).length} embedded values · extract the archive and run directly`, "success");
    } else {
      saveDownload(patched, executableName);
      const permissionNote = details.archive ? " · run chmod +x if required" : "";
      setStatus(`Done · ${Object.keys(config).length} embedded values · download started${permissionNote}`, "success");
    }
  } catch (error) {
    setStatus(error.message, "error");
  } finally {
    const platform = selectedPlatform();
    const assetAvailable = latestRelease?.assets.some(({ name }) => name === PLATFORMS[platform].asset);
    downloadButton.disabled = !localBinary && !assetAvailable;
  }
});

loadRelease();
