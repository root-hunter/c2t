export const PLATFORMS = Object.freeze({
  "linux-x86_64": Object.freeze({
    asset: "c2t-linux-x86_64",
    label: "Linux x86_64",
    filenameSuffix: "linux-x86_64",
    archive: true,
    formatChoice: true,
    configMode: "embedded",
  }),
  "linux-aarch64": Object.freeze({
    asset: "c2t-linux-aarch64",
    label: "Linux ARM64",
    filenameSuffix: "linux-aarch64",
    archive: true,
    formatChoice: true,
    configMode: "embedded",
  }),
  "windows-x86_64": Object.freeze({
    asset: "c2t-windows-x86_64.exe",
    label: "Windows x86_64",
    filenameSuffix: "windows-x86_64.exe",
    archive: false,
    formatChoice: false,
    configMode: "embedded",
  }),
  "macos-x86_64": Object.freeze({
    asset: "c2t-macos-x86_64",
    label: "macOS Intel",
    filenameSuffix: "macos-x86_64",
    archive: true,
    formatChoice: false,
    configMode: "sidecar",
  }),
  "macos-aarch64": Object.freeze({
    asset: "c2t-macos-aarch64",
    label: "macOS Apple Silicon",
    filenameSuffix: "macos-aarch64",
    archive: true,
    formatChoice: false,
    configMode: "sidecar",
  }),
});

export function inferPlatform(binary) {
  if (binary.length >= 2 && binary[0] === 0x4d && binary[1] === 0x5a) {
    return "windows-x86_64";
  }
  if (
    binary.length >= 20 &&
    binary[0] === 0x7f && binary[1] === 0x45 &&
    binary[2] === 0x4c && binary[3] === 0x46
  ) {
    const littleEndian = binary[5] === 1;
    const machine = littleEndian
      ? binary[18] | (binary[19] << 8)
      : (binary[18] << 8) | binary[19];
    if (machine === 62) return "linux-x86_64";
    if (machine === 183) return "linux-aarch64";
  }
  if (
    binary.length >= 8 && binary[0] === 0xcf && binary[1] === 0xfa &&
    binary[2] === 0xed && binary[3] === 0xfe
  ) {
    const cpu = binary[4] | (binary[5] << 8) |
      (binary[6] << 16) | (binary[7] << 24);
    if ((cpu >>> 0) === 0x01000007) return "macos-x86_64";
    if ((cpu >>> 0) === 0x0100000c) return "macos-aarch64";
  }
  throw new Error("The local file is not a supported c2t platform binary");
}
