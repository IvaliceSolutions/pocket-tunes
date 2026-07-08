// RGB conversions for the core's framebuffer format (RGB565, little-endian).

/** Pack 8-bit R,G,B into a 16-bit RGB565 value. */
export function rgb888to565(r, g, b) {
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

/**
 * Convert a tightly-packed RGBA buffer (width*height*4) into a tightly-packed
 * RGB565 little-endian buffer (width*height*2). Alpha is ignored (opaque UI).
 */
export function rgbaToRgb565Buffer(rgba, width, height) {
  const out = Buffer.allocUnsafe(width * height * 2);
  for (let i = 0, o = 0; i < width * height * 4; i += 4, o += 2) {
    out.writeUInt16LE(rgb888to565(rgba[i], rgba[i + 1], rgba[i + 2]), o);
  }
  return out;
}
