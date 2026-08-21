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
import { gunzipSync } from "node:zlib";
import test from "node:test";

import {
  createExecutableTar,
  createExecutableTarGz,
  createMacOSBundleTarGz,
} from "../docs/archive.mjs";

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

test("macOS archive preserves the signed binary and adds a private sidecar", async () => {
  const tar = gunzipSync(await createMacOSBundleTarGz(
    Uint8Array.from([0xcf, 0xfa, 0xed, 0xfe]),
    new TextEncoder().encode("TELEGRAM_ENABLED=1\n"),
    "c2t-macos-aarch64",
  ));
  assert.equal(text(tar.subarray(0, 100)), "c2t-macos-aarch64/");
  assert.equal(tar[156], "5".charCodeAt(0));
  const binaryHeader = 512;
  assert.equal(text(tar.subarray(binaryHeader, binaryHeader + 100)), "c2t-macos-aarch64/c2t");
  assert.equal(Number.parseInt(text(tar.subarray(binaryHeader + 100, binaryHeader + 108)), 8), 0o755);
  const sidecarHeader = 1536;
  assert.equal(text(tar.subarray(sidecarHeader, sidecarHeader + 100)), "c2t-macos-aarch64/.c2t.env");
  assert.equal(Number.parseInt(text(tar.subarray(sidecarHeader + 100, sidecarHeader + 108)), 8), 0o600);
});
