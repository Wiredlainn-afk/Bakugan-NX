// Finds the Xenos shader containers inside the game's media/shaders/*.par files.
// A JavaScript port of tools/par_extract_shaders.py (no WebAssembly needed: the files are a few MB).
//
// A container starts with 0x102A1100 (pixel) or 0x102A1101 (vertex), then [1] the size of its virtual part and [2] the
// size of its physical part (the microcode); its total size is [1] + [2]. Found by scanning 4-aligned words and
// validating the sizes. Names are {vs,ps}_<FNV-1a 64 of the container>.bin, like the other tools of this family.

const MASK64 = 0xFFFFFFFFFFFFFFFFn;
const FNV_OFFSET = 0xCBF29CE484222325n;
const FNV_PRIME = 0x100000001B3n;

export function fnv1a64Hex(bytes) {
  let h = FNV_OFFSET;
  for (let i = 0; i < bytes.length; i++) {
    h ^= BigInt(bytes[i]);
    h = (h * FNV_PRIME) & MASK64;
  }
  return h.toString(16).padStart(16, '0');
}

/** Returns [{ name, data }] for every distinct container found in `bytes` (a Uint8Array). */
export function scanContainers(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const found = new Map();
  let o = 0;
  while (o + 12 <= bytes.length) {
    const w0 = view.getUint32(o, false);
    if ((w0 & 0xFFFFFFFE) === 0x102A1100) {
      const vs = view.getUint32(o + 4, false);
      const ps = view.getUint32(o + 8, false);
      if (vs >= 28 && vs <= 0x40000 && ps > 0 && ps <= 0x40000 && o + vs + ps <= bytes.length) {
        const data = bytes.slice(o, o + vs + ps);
        const name = `${w0 & 1 ? 'vs' : 'ps'}_${fnv1a64Hex(data)}.bin`;
        if (!found.has(name)) found.set(name, data);
        o += (vs + ps + 3) & ~3;
        continue;
      }
    }
    o += 4;
  }
  return [...found].map(([name, data]) => ({ name, data }));
}
