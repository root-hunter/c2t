const BLOCK_SIZE = 512;

function writeText(target, offset, length, value) {
  const encoded = new TextEncoder().encode(value);
  if (encoded.length > length) throw new Error("Archive filename is too long");
  target.set(encoded, offset);
}

function writeOctal(target, offset, length, value) {
  const encoded = value.toString(8).padStart(length - 1, "0");
  if (encoded.length >= length) throw new Error("Archive value is too large");
  writeText(target, offset, length - 1, encoded);
}

export function createExecutableTar(binary, filename) {
  return createTar([{ content: binary, filename, mode: 0o755 }]);
}

export function createTar(entries) {
  const normalized = entries.map(({ content, filename, mode, type = "0" }) => ({
    content: content instanceof Uint8Array ? content : new Uint8Array(content),
    filename,
    mode,
    type,
  }));
  const blocks = normalized.reduce(
    (total, entry) => total + 1 + Math.ceil(entry.content.length / BLOCK_SIZE),
    2,
  );
  const archive = new Uint8Array(blocks * BLOCK_SIZE);
  let archiveOffset = 0;

  for (const entry of normalized) {
    const header = archive.subarray(archiveOffset, archiveOffset + BLOCK_SIZE);

    writeText(header, 0, 100, entry.filename);
    writeOctal(header, 100, 8, entry.mode);
    writeOctal(header, 108, 8, 0);
    writeOctal(header, 116, 8, 0);
    writeOctal(header, 124, 12, entry.content.length);
    writeOctal(header, 136, 12, 0);
    header.fill(0x20, 148, 156);
    header[156] = entry.type.charCodeAt(0);
    writeText(header, 257, 6, "ustar\0");
    writeText(header, 263, 2, "00");
    writeText(header, 265, 32, "c2t");
    writeText(header, 297, 32, "c2t");

    const checksum = header.reduce((sum, byte) => sum + byte, 0);
    writeText(header, 148, 8, `${checksum.toString(8).padStart(6, "0")}\0 `);
    archiveOffset += BLOCK_SIZE;
    archive.set(entry.content, archiveOffset);
    archiveOffset += Math.ceil(entry.content.length / BLOCK_SIZE) * BLOCK_SIZE;
  }
  return archive;
}

async function gzip(tar) {
  if (typeof CompressionStream !== "function") {
    throw new Error("This browser cannot create the executable archive. Please use a current browser.");
  }
  const compressed = new Blob([tar]).stream().pipeThrough(new CompressionStream("gzip"));
  return new Uint8Array(await new Response(compressed).arrayBuffer());
}

export async function createExecutableTarGz(binary, filename) {
  return gzip(createExecutableTar(binary, filename));
}

export async function createMacOSBundleTarGz(binary, config, filename) {
  return gzip(createTar([
    { content: new Uint8Array(), filename: `${filename}/`, mode: 0o755, type: "5" },
    { content: binary, filename: `${filename}/c2t`, mode: 0o755 },
    { content: config, filename: `${filename}/.c2t.env`, mode: 0o600 },
  ]));
}
