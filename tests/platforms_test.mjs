import assert from "node:assert/strict";
import test from "node:test";

import { inferPlatform, PLATFORMS } from "../docs/platforms.mjs";

function elf(machine) {
  const binary = new Uint8Array(20);
  binary.set([0x7f, 0x45, 0x4c, 0x46]);
  binary[5] = 1;
  binary[18] = machine & 0xff;
  binary[19] = machine >>> 8;
  return binary;
}

test("all published site assets have distinct names", () => {
  const names = Object.values(PLATFORMS).map(({ asset }) => asset);
  assert.equal(new Set(names).size, names.length);
});

test("Linux outputs are executable archives and Windows remains a direct executable", () => {
  assert.equal(PLATFORMS["linux-x86_64"].archive, true);
  assert.equal(PLATFORMS["linux-aarch64"].archive, true);
  assert.equal(PLATFORMS["windows-x86_64"].archive, false);
});

test("ELF machine type selects x86_64 or ARM64", () => {
  assert.equal(inferPlatform(elf(62)), "linux-x86_64");
  assert.equal(inferPlatform(elf(183)), "linux-aarch64");
});

test("PE files select Windows and unknown files are rejected", () => {
  assert.equal(inferPlatform(Uint8Array.from([0x4d, 0x5a])), "windows-x86_64");
  assert.throws(() => inferPlatform(new Uint8Array(20)), /not a supported/);
});
