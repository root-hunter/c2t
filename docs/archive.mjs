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
  const content = binary instanceof Uint8Array ? binary : new Uint8Array(binary);
  const contentBlocks = Math.ceil(content.length / BLOCK_SIZE);
  const archive = new Uint8Array((1 + contentBlocks + 2) * BLOCK_SIZE);
  const header = archive.subarray(0, BLOCK_SIZE);

  writeText(header, 0, 100, filename);
  writeOctal(header, 100, 8, 0o755);
  writeOctal(header, 108, 8, 0);
  writeOctal(header, 116, 8, 0);
  writeOctal(header, 124, 12, content.length);
  writeOctal(header, 136, 12, 0);
  header.fill(0x20, 148, 156);
  header[156] = 0x30;
  writeText(header, 257, 6, "ustar\0");
  writeText(header, 263, 2, "00");
  writeText(header, 265, 32, "c2t");
  writeText(header, 297, 32, "c2t");

  const checksum = header.reduce((sum, byte) => sum + byte, 0);
  writeText(header, 148, 8, `${checksum.toString(8).padStart(6, "0")}\0 `);
  archive.set(content, BLOCK_SIZE);
  return archive;
}

export async function createExecutableTarGz(binary, filename) {
  if (typeof CompressionStream !== "function") {
    throw new Error("This browser cannot create the executable Linux archive. Please use a current browser.");
  }
  const tar = createExecutableTar(binary, filename);
  const compressed = new Blob([tar]).stream().pipeThrough(new CompressionStream("gzip"));
  return new Uint8Array(await new Response(compressed).arrayBuffer());
}
