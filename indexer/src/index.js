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

const SCHEMA_VERSION = 1;
const VERSION = "0.1.0";

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

/** Join an SD absolute root with a relative path, forcing POSIX separators.
 *  The path is normalized to Unicode NFC: exFAT stores filenames precomposed
 *  (Microsoft/NFC convention) and the Pocket's openfile (0x0192) matches those
 *  raw entries. macOS's readdir DEcomposes them to NFD on the way out, so the
 *  as-read form does NOT byte-match what the Pocket looks up — accented files
 *  then fail with openfile ERR 1 while ASCII names work. NFC fixes it. */
function sdJoin(sdRoot, rel) {
  const norm = rel.split(path.sep).join("/");
  return (sdRoot.replace(/\/+$/, "") + "/" + norm)
    .replace(/\/{2,}/g, "/")
    .normalize("NFC");
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

  for (let ai = 0; ai < scanned.length; ai++) {
    const sArtist = scanned[ai];
    const albums = [];

    for (const sAlbum of sArtist.albums) {
      const tracks = [];
      let year = null;
      let genre = null;

      for (const file of sAlbum.files) {
        let md;
        try {
          md = await readMetadata(file);
        } catch (e) {
          log(args.quiet, `  ! skip ${path.basename(file)} (${e.message})`);
          continue;
        }
        if (year == null && md.year != null) year = md.year;
        if (genre == null && md.genre != null) genre = md.genre;

        // Per-track cover from this file's own embedded art.
        let tCover = null, tCoverSmall = null;
        if (md.picture) {
          const tstem = `t${String(coverSeq++).padStart(5, "0")}`;
          if (await writeThumbnails(md.picture, coversDir, tstem)) {
            tCover = `covers/${tstem}.rgb565`;
            tCoverSmall = `covers/${tstem}.s.rgb565`;
            coverHits++;
          }
        }

        tracks.push({
          _diskNo: md.diskNo ?? 0,
          _trackNo: md.trackNo ?? 9999,
          _file: file,
          title: md.title || path.basename(file, path.extname(file)),
          durationMs: md.durationMs,
          format: md.format,
          codec: md.codec,
          bitrateKbps: md.bitrateKbps,
          sampleRate: md.sampleRate,
          channels: md.channels,
          coverArt: tCover,
          coverArtSmall: tCoverSmall,
          path: sdJoin(args.sdRoot, path.relative(musicRoot, file)),
        });
      }

      if (!tracks.length) continue;

      // Play order: disc, then track number, then filename.
      tracks.sort(
        (a, b) =>
          a._diskNo - b._diskNo ||
          a._trackNo - b._trackNo ||
          a._file.localeCompare(b._file, undefined, { numeric: true })
      );
      let fileSizePromises = tracks.map((t) => fs.stat(t._file).then((s) => s.size).catch(() => null));
      const sizes = await Promise.all(fileSizePromises);
      const outTracks = tracks.map((t, i) => ({
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
        path: t.path,
      }));

      // Album cover: a sidecar image in the folder wins; otherwise reuse the
      // first track's cover as the representative thumbnail (no re-render).
      const id = albumId++;
      const sidecar = await findSidecar(sAlbum.dir);
      let coverArt = outTracks[0].coverArt;
      let coverArtSmall = outTracks[0].coverArtSmall;
      if (sidecar) {
        const stem = `a${String(id).padStart(4, "0")}`;
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
    if (albums.length) artists.push({ id: ai, name: sArtist.name, albums });
  }

  // Re-number artist ids to their final array position.
  artists.forEach((a, i) => (a.id = i));

  const library = {
    schemaVersion: SCHEMA_VERSION,
    generator: `pocket-tunes-indexer ${VERSION}`,
    root: args.sdRoot,
    counts: { artists: artists.length, albums: albumId, tracks: trackTotal, trackCovers: coverHits },
    artists,
  };

  const jsonPath = path.join(outDir, "library.json");
  await fs.writeFile(jsonPath, JSON.stringify(library, null, 2));
  log(args.quiet, `\nWrote ${jsonPath}`);
  log(args.quiet, `  ${artists.length} artists, ${albumId} albums, ${trackTotal} tracks, ${coverHits} track covers`);
}

main().catch((e) => {
  console.error(`error: ${e.message}`);
  process.exit(1);
});
