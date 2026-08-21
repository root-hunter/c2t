import assert from "node:assert/strict";
import { gunzipSync } from "node:zlib";
import test from "node:test";

import { createExecutableTar, createExecutableTarGz } from "../docs/archive.mjs";

function text(bytes) {
  return new TextDecoder().decode(bytes).replace(/\0.*$/s, "");
}

test("tar entry stores the configured binary with mode 0755", () => {
  const binary = Uint8Array.from([0x7f, 0x45, 0x4c, 0x46]);
  const tar = createExecutableTar(binary, "c2t-configured-linux-x86_64");

  assert.equal(text(tar.subarray(0, 100)), "c2t-configured-linux-x86_64");
  assert.equal(Number.parseInt(text(tar.subarray(100, 108)), 8), 0o755);
  assert.equal(Number.parseInt(text(tar.subarray(124, 136)), 8), binary.length);
  assert.deepEqual(tar.subarray(512, 512 + binary.length), binary);
});

test("gzip output contains the executable tar entry", async () => {
  const tarGz = await createExecutableTarGz(Uint8Array.from([1, 2, 3]), "c2t");
  const tar = gunzipSync(tarGz);
  assert.equal(Number.parseInt(text(tar.subarray(100, 108)), 8), 0o755);
  assert.deepEqual(Array.from(tar.subarray(512, 515)), [1, 2, 3]);
});
