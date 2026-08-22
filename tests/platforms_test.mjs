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
import test from "node:test";

import { detectUserPlatform, inferPlatform, PLATFORMS } from "../docs/platforms.mjs";

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

test("Unix outputs are executable archives and Windows remains a direct executable", () => {
  assert.equal(PLATFORMS["linux-x86_64"].archive, true);
  assert.equal(PLATFORMS["linux-aarch64"].archive, true);
  assert.equal(PLATFORMS["windows-x86_64"].archive, false);
  assert.equal(PLATFORMS["macos-x86_64"].archive, true);
  assert.equal(PLATFORMS["macos-aarch64"].archive, true);
  assert.equal(PLATFORMS["macos-aarch64"].configMode, "sidecar");
});

test("ELF machine type selects x86_64 or ARM64", () => {
  assert.equal(inferPlatform(elf(62)), "linux-x86_64");
  assert.equal(inferPlatform(elf(183)), "linux-aarch64");
});

test("PE files select Windows and unknown files are rejected", () => {
  assert.equal(inferPlatform(Uint8Array.from([0x4d, 0x5a])), "windows-x86_64");
  assert.throws(() => inferPlatform(new Uint8Array(20)), /not a supported/);
});

test("thin Mach-O files select Intel or Apple Silicon", () => {
  const macho = (cpu) => Uint8Array.from([
    0xcf, 0xfa, 0xed, 0xfe,
    cpu & 0xff, (cpu >>> 8) & 0xff, (cpu >>> 16) & 0xff, cpu >>> 24,
  ]);
  assert.equal(inferPlatform(macho(0x01000007)), "macos-x86_64");
  assert.equal(inferPlatform(macho(0x0100000c)), "macos-aarch64");
});

test("detectUserPlatform identifies the visitor platform correctly", () => {
  assert.equal(detectUserPlatform(null), "linux-x86_64");

  // Windows
  assert.equal(
    detectUserPlatform({
      userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
      platform: "Win32",
    }),
    "windows-x86_64",
  );
  assert.equal(
    detectUserPlatform({
      userAgent: "Mozilla/5.0",
      userAgentData: { platform: "Windows" },
    }),
    "windows-x86_64",
  );

  // macOS Intel
  assert.equal(
    detectUserPlatform({
      userAgent: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15",
      platform: "MacIntel",
    }),
    "macos-x86_64",
  );

  // macOS Apple Silicon (ARM64)
  assert.equal(
    detectUserPlatform({
      userAgent: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7; arm64)",
      platform: "MacIntel",
    }),
    "macos-aarch64",
  );
  assert.equal(
    detectUserPlatform({
      userAgent: "Mozilla/5.0 (Macintosh)",
      userAgentData: { platform: "macOS", architecture: "arm" },
    }),
    "macos-aarch64",
  );

  // Linux x86_64
  assert.equal(
    detectUserPlatform({
      userAgent: "Mozilla/5.0 (X11; Linux x86_64; rv:109.0) Gecko/20100101 Firefox/119.0",
      platform: "Linux x86_64",
    }),
    "linux-x86_64",
  );

  // Linux ARM64
  assert.equal(
    detectUserPlatform({
      userAgent: "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36",
      platform: "Linux aarch64",
    }),
    "linux-aarch64",
  );
});
