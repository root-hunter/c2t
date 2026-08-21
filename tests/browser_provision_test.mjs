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
  assert.throws(() => encodePayload({ C2T_QUEUE_MAX_ITEMS: "0" }), /positivo/);
  assert.throws(() => encodePayload({ TELEGRAM_CHAT_ID: "bad\nvalue" }), /consentito/);
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
