import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import { crc32, encodePayload, locateRegion, patchBinary } from "../docs/provision.mjs";

const [, , executable] = process.argv;

test("CRC32 matches the standard vector", () => {
  assert.equal(crc32(new TextEncoder().encode("123456789")), 0xcbf43926);
});

test("payload keys are validated and sorted", () => {
  assert.equal(
    new TextDecoder().decode(encodePayload({ TELEGRAM_ENABLED: "1", C2T_VERBOSE: "0" })),
    "C2T_VERBOSE=0\nTELEGRAM_ENABLED=1\n",
  );
  assert.throws(() => encodePayload({ C2T_QUEUE_MAX_ITEMS: "0" }), /positive/);
  assert.throws(() => encodePayload({ TELEGRAM_CHAT_ID: "bad\nvalue" }), /forbidden/);
});

test("browser patch writes a valid configuration into the release binary", () => {
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
  };
  const source = readFileSync(executable);
  const output = patchBinary(source, config);
  const offset = locateRegion(output);
  const payload = encodePayload(config);
  const view = new DataView(output.buffer, output.byteOffset, output.byteLength);

  assert.equal(output.length, source.length);
  assert.equal(view.getUint32(offset + 16, true), 1);
  assert.equal(view.getUint32(offset + 20, true), payload.length);
  assert.equal(view.getUint32(offset + 24, true), crc32(payload));
  assert.deepEqual(output.subarray(offset + 32, offset + 32 + payload.length), payload);
  assert.ok(output.subarray(offset + 32 + payload.length, offset + 32 + 4096).every((byte) => byte === 0));
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
