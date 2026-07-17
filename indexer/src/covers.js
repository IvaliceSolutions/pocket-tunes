// Turns a cover source (embedded picture buffer or a sidecar image file) into
// the two raw RGB565 thumbnails the core blits directly: 96x96 (Lecture
// screen, drawn 1:1 at 96 px) and 32x32 (list, reserved for a future
// list-cover pass). No decoder runs on the soft-CPU — all image work happens here.

import { Jimp } from "jimp";
import fs from "node:fs/promises";
import path from "node:path";
import { rgbaToRgb565Buffer } from "./rgb565.js";

export const LARGE = 96;
export const SMALL = 32;

// Sidecar cover filenames to look for in an album folder, in priority order.
const SIDECAR_NAMES = ["cover", "folder", "front", "album", "albumart", "thumb"];
const SIDECAR_EXTS = [".jpg", ".jpeg", ".png", ".webp", ".bmp", ".gif"];

/** Find a same-folder cover image, or null. */
export async function findSidecar(dir) {
  let entries;
  try {
    entries = await fs.readdir(dir);
  } catch {
    return null;
  }
  const lower = new Map(entries.map((e) => [e.toLowerCase(), e]));
  for (const name of SIDECAR_NAMES) {
    for (const ext of SIDECAR_EXTS) {
      const hit = lower.get(name + ext);
      if (hit) return path.join(dir, hit);
    }
  }
  return null;
}

async function toThumb(image, size) {
  // center-crop to square, then resize to size x size
  const clone = image.clone().cover({ w: size, h: size });
  return rgbaToRgb565Buffer(clone.bitmap.data, size, size);
}

/**
 * Load `source` (a Buffer of encoded image bytes, or a filesystem path), rasterize
 * both thumbnails, and write them next to each other. Returns true on success.
 * Filenames: `${stem}.rgb565` (48) and `${stem}.s.rgb565` (32).
 */
export async function writeThumbnails(source, outDir, stem) {
  let image;
  try {
    image = await Jimp.read(source);
  } catch {
    return false;
  }
  const large = await toThumb(image, LARGE);
  const small = await toThumb(image, SMALL);
  await fs.mkdir(outDir, { recursive: true });
  await fs.writeFile(path.join(outDir, `${stem}.rgb565`), large);
  await fs.writeFile(path.join(outDir, `${stem}.s.rgb565`), small);
  return true;
}
