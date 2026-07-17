#!/usr/bin/env node
// Pocket Tunes library indexer.
// Scans a music folder and emits library.json + covers/ for the Pocket Tunes core.
// See ../docs/library-format.md for the schema.

import fs from "node:fs/promises";
import path from "node:path";
import { scanLibrary } from "./scan.js";
import { readMetadata } from "./metadata.js";
import { findSidecar, writeThumbnails } from "./covers.js";
import { hueForAlbum } from "./hue.js";
import { readChapters } from "./chapters.js";

// v2 (design 4a): loose tracks keep their real depth — top-level "rootTracks"
// (files directly in the music root) and per-artist "rootTracks" (files
// directly in an artist folder), instead of synthetic "(unknown)"/"(singles)"
// buckets. "chapters" is only emitted when non-empty. KEY ORDER MATTERS: the
// firmware numbers tracks in file order (albums before rootTracks, artists
// before the top-level rootTracks) and cover files are named after that order.
const SCHEMA_VERSION = 2;
const VERSION = "0.2.0";

function parseArgs(argv) {
  const args = { music: null, out: null, sdRoot: "/Music", quiet: false };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--music" || a === "-m") args.music = argv[++i];
    else if (a === "--out" || a === "-o") args.out = argv[++i];
    else if (a === "--sd-root" || a === "-r") args.sdRoot = argv[++i];
    else if (a === "--quiet" || a === "-q") args.quiet = true;
    else if (a === "--help" || a === "-h") args.help = true;
    else throw new Error(`unknown argument: ${a}`);
  }
  return args;
}

const USAGE = `Pocket Tunes indexer ${VERSION}

Usage:
  pocket-tunes-indexer --music <folder> --out <folder> [--sd-root <path>]

Options:
  -m, --music <folder>   Music folder to scan (on the SD card or a copy of it).
  -o, --out <folder>     Where to write library.json and covers/.
  -r, --sd-root <path>   Absolute path of the music folder AS SEEN ON THE POCKET
                         SD card. Recorded into every track path so the core can
                         open files with target command 0x0192. Default: /Music
  -q, --quiet            Less logging.

Example:
  pocket-tunes-indexer -m /Volumes/POCKET/Music -o /Volumes/POCKET/Assets/pockettunes/common -r /Music
`;

/** Transliterate a string to plain ASCII (é→e, œ→oe, …). The Pocket's openfile
 *  (0x0192) can only match ASCII names — it fails to match ANY non-ASCII entry,
 *  in EITHER Unicode form (NFC and NFD both tested on hardware → ERR 1). So the
 *  only reliable fix is to give the file an ASCII name on disk and store that
 *  ASCII path here; the accented original is kept as the display title. */
const LIGATURES = { "œ": "oe", "Œ": "OE", "æ": "ae", "Æ": "AE", "ß": "ss",
  "ø": "o", "Ø": "O", "đ": "d", "Đ": "D", "ł": "l", "Ł": "L", "þ": "th", "ð": "d" };
function deaccent(s) {
  return s
    .normalize("NFKD")
    .replace(/[̀-ͯ]/g, "")                 // strip combining diacritics
    .replace(/[œŒæÆßøØđĐłŁþð]/g, (c) => LIGATURES[c]) // common ligatures
    .replace(/[^\x20-\x7e]/g, "_");                  // any remaining non-ASCII → _
}
function hasNonAscii(s) {
  return /[^\x20-\x7e]/.test(s);
}

/** Join an SD absolute root with a relative path (POSIX separators) and force
 *  the whole path to ASCII. `sanitizeMusicTree` renames the real files/folders
 *  on disk to the same ASCII names so this path resolves on the Pocket. */
function sdJoin(sdRoot, rel) {
  const norm = rel.split(path.sep).join("/");
  return deaccent((sdRoot.replace(/\/+$/, "") + "/" + norm).replace(/\/{2,}/g, "/"));
}

/** Rename every file/folder under `root` whose name has non-ASCII characters to
 *  its ASCII transliteration, so the paths written to library.json resolve on
 *  the Pocket. Deepest-first so a directory is renamed only after its children.
 *  Display names (artist/album/track) were already captured with accents from
 *  the earlier scan, so this only touches on-disk names, not what's shown. */
async function sanitizeMusicTree(root, quiet) {
  const entries = [];
  async function walk(dir) {
    let items;
    try {
      items = await fs.readdir(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const it of items) {
      if (it.name.startsWith(".")) continue; // skip dotfiles / macOS ._ cruft
      const full = path.join(dir, it.name);
      if (it.isDirectory()) await walk(full);
      if (hasNonAscii(it.name)) entries.push({ dir, name: it.name, depth: full.split(path.sep).length });
    }
  }
  await walk(root);
  entries.sort((a, b) => b.depth - a.depth); // deepest first

  let renamed = 0;
  for (const e of entries) {
    const ascii = deaccent(e.name);
    if (ascii === e.name) continue;
    const from = path.join(e.dir, e.name);
    const to = path.join(e.dir, ascii);
    try {
      await fs.access(to);
      log(quiet, `  ! keep accented (ASCII name taken): ${e.name}`);
      continue; // collision — leave it; would clobber a different entry
    } catch {
      /* target free → rename */
    }
    try {
      await fs.rename(from, to);
      renamed++;
    } catch (err) {
      log(quiet, `  ! rename failed: ${e.name} (${err.message})`);
    }
  }
  if (renamed) log(quiet, `Renamed ${renamed} non-ASCII name(s) → ASCII (Pocket openfile needs it)`);
}

function log(quiet, ...m) {
  if (!quiet) console.log(...m);
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (args.help || !args.music || !args.out) {
    process.stdout.write(USAGE);
    process.exit(args.help ? 0 : 1);
  }

  const musicRoot = path.resolve(args.music);
  const outDir = path.resolve(args.out);
  const coversDir = path.join(outDir, "covers");
  // Wipe stale thumbnails so removed/renamed albums don't leave orphans behind.
  await fs.rm(coversDir, { recursive: true, force: true });
  await fs.mkdir(coversDir, { recursive: true });

  log(args.quiet, `Scanning ${musicRoot} ...`);
  const scanned = await scanLibrary(musicRoot);

  const artists = [];
  let albumId = 0;
  let trackTotal = 0;
  let coverHits = 0;
  let coverSeq = 0; // global id for per-track cover thumbnails

  /** Read one audio file → raw track record (null if unreadable). */
  async function readTrack(file) {
    let md;
    try {
      md = await readMetadata(file);
    } catch (e) {
      log(args.quiet, `  ! skip ${path.basename(file)} (${e.message})`);
      return null;
    }
    // ID3 chapters (audiobooks) — start seconds + titles, for L/R nav
    const chapters = (await readChapters(file)).map((c) => ({
      s: Math.round(c.startMs / 1000),
      t: c.title,
    }));

    // Per-track cover from this file's own embedded art.
    let tCover = null, tCoverSmall = null;
    if (md.picture) {
      const tstem = `x${String(coverSeq++).padStart(5, "0")}`;  // temp; renamed to t{gidx} in the final pass
      if (await writeThumbnails(md.picture, coversDir, tstem)) {
        tCover = `covers/${tstem}.rgb565`;
        tCoverSmall = `covers/${tstem}.s.rgb565`;
        coverHits++;
      }
    }

    return {
      _diskNo: md.diskNo ?? 0,
      _trackNo: md.trackNo ?? 9999,
      _file: file,
      _year: md.year,
      _genre: md.genre,
      title: md.title || path.basename(file, path.extname(file)),
      durationMs: md.durationMs,
      format: md.format,
      codec: md.codec,
      bitrateKbps: md.bitrateKbps,
      sampleRate: md.sampleRate,
      channels: md.channels,
      coverArt: tCover,
      coverArtSmall: tCoverSmall,
      chapters,
      path: sdJoin(args.sdRoot, path.relative(musicRoot, file)),
    };
  }

  /** Sort into play order, stat sizes, strip the working fields. */
  async function finishTracks(tracks) {
    // Play order: disc, then track number, then filename.
    tracks.sort(
      (a, b) =>
        a._diskNo - b._diskNo ||
        a._trackNo - b._trackNo ||
        a._file.localeCompare(b._file, undefined, { numeric: true })
    );
    const sizes = await Promise.all(
      tracks.map((t) => fs.stat(t._file).then((s) => s.size).catch(() => null))
    );
    return tracks.map((t, i) => ({
      index: i,
      title: t.title,
      durationMs: t.durationMs,
      format: t.format,
      codec: t.codec,
      bitrateKbps: t.bitrateKbps,
      sampleRate: t.sampleRate,
      channels: t.channels,
      fileSize: sizes[i],
      coverArt: t.coverArt,
      coverArtSmall: t.coverArtSmall,
      ...(t.chapters.length ? { chapters: t.chapters } : {}),
      path: t.path,
    }));
  }

  /** readTrack over a file list, dropping unreadable ones. */
  async function readTracks(files) {
    const out = [];
    for (const f of files) {
      const t = await readTrack(f);
      if (t) out.push(t);
    }
    return out;
  }

  let rootTrackTotal = 0;

  for (let ai = 0; ai < scanned.artists.length; ai++) {
    const sArtist = scanned.artists[ai];
    const albums = [];

    for (const sAlbum of sArtist.albums) {
      const tracks = await readTracks(sAlbum.files);
      if (!tracks.length) continue;
      const year = tracks.find((t) => t._year != null)?._year ?? null;
      const genre = tracks.find((t) => t._genre != null)?._genre ?? null;
      const outTracks = await finishTracks(tracks);

      // Album cover: a sidecar image in the folder wins; otherwise reuse the
      // first track's cover as the representative thumbnail (no re-render).
      const id = albumId++;
      const sidecar = await findSidecar(sAlbum.dir);
      let coverArt = outTracks[0].coverArt;
      let coverArtSmall = outTracks[0].coverArtSmall;
      if (sidecar) {
        const stem = `y${String(id).padStart(4, "0")}`;  // temp; renamed to a{aidx} in the final pass
        if (await writeThumbnails(sidecar, coversDir, stem)) {
          coverArt = `covers/${stem}.rgb565`;
          coverArtSmall = `covers/${stem}.s.rgb565`;
        }
      }

      albums.push({
        id,
        title: sAlbum.title,
        year,
        genre,
        hue: hueForAlbum(sArtist.name, sAlbum.title),
        coverArt,
        coverArtSmall,
        tracks: outTracks,
      });
      trackTotal += outTracks.length;
      log(args.quiet, `  ${sArtist.name} / ${sAlbum.title} — ${outTracks.length} tracks${coverArt ? " +art" : ""}`);
    }

    // Albums: by year then title.
    albums.sort((a, b) => (a.year ?? 99999) - (b.year ?? 99999) || a.title.localeCompare(b.title));

    // Loose tracks directly in the artist folder (design 4a: appended after
    // the albums on the artist's screen).
    const artistRootTracks = await finishTracks(await readTracks(sArtist.rootTracks));
    rootTrackTotal += artistRootTracks.length;
    trackTotal += artistRootTracks.length;
    if (artistRootTracks.length)
      log(args.quiet, `  ${sArtist.name} — ${artistRootTracks.length} loose track(s)`);

    if (albums.length || artistRootTracks.length)
      artists.push({
        id: ai,
        name: sArtist.name,
        albums,
        ...(artistRootTracks.length ? { rootTracks: artistRootTracks } : {}),
      });
  }

  // Re-number artist ids to their final array position.
  artists.forEach((a, i) => (a.id = i));

  // Loose tracks directly in the music root (rows mixed into the top-level
  // Bibliothèque list). Emitted AFTER "artists" — the firmware numbers tracks
  // in file order and the cover rename pass below relies on it.
  const rootTracks = await finishTracks(await readTracks(scanned.rootTracks));
  rootTrackTotal += rootTracks.length;
  trackTotal += rootTracks.length;
  if (rootTracks.length) log(args.quiet, `  <root> — ${rootTracks.length} loose track(s)`);

  // Rename cover files to their FINAL enumeration order: t{gidx:05} for tracks
  // and a{aidx:04} for album sidecars, where gidx/aidx count tracks/albums in
  // library.json order — the exact order the firmware parses. The core then
  // derives every cover path from the index alone and stores nothing per track.
  // (Scan-time names use the x/y namespace so renames can't collide.)
  {
    let gidx = 0, aidx = 0, renames = 0;
    const ren = async (from, to) => {
      try {
        await fs.rename(path.join(outDir, from), path.join(outDir, to));
        renames++;
        return true;
      } catch {
        return false;
      }
    };
    const renTrack = async (t) => {
      if (t.coverArt) {
        const stem = `covers/t${String(gidx).padStart(5, "0")}`;
        await ren(t.coverArt, `${stem}.rgb565`);
        await ren(t.coverArtSmall, `${stem}.s.rgb565`);
        t.coverArt = `${stem}.rgb565`;
        t.coverArtSmall = `${stem}.s.rgb565`;
      }
      gidx++;
    };
    // Same order the firmware numbers tracks in: each artist's albums then
    // its rootTracks, then the library-root rootTracks last.
    for (const a of artists) {
      for (const al of a.albums) {
        for (const t of al.tracks) await renTrack(t);
        if (al.coverArt && al.coverArt.startsWith("covers/y")) {
          const stem = `covers/a${String(aidx).padStart(4, "0")}`;
          await ren(al.coverArt, `${stem}.rgb565`);
          await ren(al.coverArtSmall, `${stem}.s.rgb565`);
          al.coverArt = `${stem}.rgb565`;
          al.coverArtSmall = `${stem}.s.rgb565`;
        } else if (al.tracks.length) {
          // album art borrowed from the first track: follow its renamed file
          al.coverArt = al.tracks[0].coverArt;
          al.coverArtSmall = al.tracks[0].coverArtSmall;
        }
        aidx++;
      }
      for (const t of a.rootTracks ?? []) await renTrack(t);
    }
    for (const t of rootTracks) await renTrack(t);
    log(args.quiet, `Covers renamed to enumeration order (${renames} file renames)`);
  }

  // All disk reads are done; now rename any accented files/folders to ASCII so
  // the ASCII paths recorded above resolve on the Pocket (openfile can't match
  // non-ASCII). Display names keep their accents (captured before this).
  await sanitizeMusicTree(musicRoot, args.quiet);

  const library = {
    schemaVersion: SCHEMA_VERSION,
    generator: `pocket-tunes-indexer ${VERSION}`,
    root: args.sdRoot,
    counts: {
      artists: artists.length,
      albums: albumId,
      tracks: trackTotal,
      rootTracks: rootTrackTotal,
      trackCovers: coverHits,
    },
    artists,
    // Keep last: the firmware numbers tracks in file order (covers depend on it).
    ...(rootTracks.length ? { rootTracks } : {}),
  };

  const jsonPath = path.join(outDir, "library.json");
  await fs.writeFile(jsonPath, JSON.stringify(library, null, 2));
  log(args.quiet, `\nWrote ${jsonPath}`);
  log(args.quiet,
    `  ${artists.length} artists, ${albumId} albums, ${trackTotal} tracks` +
    ` (${rootTrackTotal} loose), ${coverHits} track covers`);
}

main().catch((e) => {
  console.error(`error: ${e.message}`);
  process.exit(1);
});
