// Folder-driven tree discovery: top-level folders under the music root are
// artists, their subfolders are albums, audio files inside (at any depth) are
// tracks. Loose audio files get synthetic artist/album buckets. This pass only
// discovers *files*; metadata is read later (metadata.js).

import fs from "node:fs/promises";
import path from "node:path";
import { AUDIO_EXTS } from "./metadata.js";

const isAudio = (name) => AUDIO_EXTS.has(path.extname(name).toLowerCase());
// Skip hidden/system entries: dotfiles, .DS_Store, and macOS AppleDouble "._name"
// sidecars that FAT/exFAT volumes accumulate (they share the real file's extension).
const isHidden = (name) => name.startsWith(".");
const byName = (a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: "base" });

async function readdirTyped(dir) {
  try {
    return await fs.readdir(dir, { withFileTypes: true });
  } catch {
    return [];
  }
}

/** All audio files at or below `dir` (recursive), absolute paths, sorted. */
async function collectAudioDeep(dir) {
  const out = [];
  const walk = async (d) => {
    for (const e of await readdirTyped(d)) {
      if (isHidden(e.name)) continue;
      const p = path.join(d, e.name);
      if (e.isDirectory()) await walk(p);
      else if (e.isFile() && isAudio(e.name)) out.push(p);
    }
  };
  await walk(dir);
  return out.sort(byName);
}

/** Audio files directly in `dir` only (not recursive), absolute paths, sorted. */
async function listAudioShallow(dir) {
  return (await readdirTyped(dir))
    .filter((e) => e.isFile() && !isHidden(e.name) && isAudio(e.name))
    .map((e) => path.join(dir, e.name))
    .sort(byName);
}

/**
 * Returns [{ name, albums: [{ title, dir, files: [absPath...] }] }].
 * `dir` is the folder used to look for a sidecar cover image.
 */
export async function scanLibrary(root) {
  const artists = [];
  const top = await readdirTyped(root);

  for (const entry of top
    .filter((e) => e.isDirectory() && !isHidden(e.name))
    .sort((a, b) => byName(a.name, b.name))) {
    const artistDir = path.join(root, entry.name);
    const albums = [];

    const subdirs = (await readdirTyped(artistDir))
      .filter((e) => e.isDirectory() && !isHidden(e.name))
      .sort((a, b) => byName(a.name, b.name));
    for (const sub of subdirs) {
      const albumDir = path.join(artistDir, sub.name);
      const files = await collectAudioDeep(albumDir);
      if (files.length) albums.push({ title: sub.name, dir: albumDir, files });
    }

    // Audio sitting directly in the artist folder → a "(singles)" album.
    const loose = await listAudioShallow(artistDir);
    if (loose.length) albums.push({ title: "(singles)", dir: artistDir, files: loose });

    if (albums.length) artists.push({ name: entry.name, albums });
  }

  // Audio sitting directly in the music root → "(unknown)" artist.
  const rootLoose = await listAudioShallow(root);
  if (rootLoose.length) {
    artists.push({ name: "(unknown)", albums: [{ title: "(singles)", dir: root, files: rootLoose }] });
  }

  return artists;
}
