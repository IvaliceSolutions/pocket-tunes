// Chapter readers: ID3v2 CHAP (MP3) and Vorbis-comment CHAPTERxxx (Opus/Ogg).
//
// MP3: music-metadata surfaces CHAP start/end times but leaves the embedded
// sub-frames empty, so chapter *titles* are lost. Chapters are the whole point
// of navigating a 20 h audiobook, so parse the raw tag ourselves — it's a
// small, well-specified format (id3.org/id3v2-chapters-1.0).
//
// CHAP frame body: elementID\0, startMs(4), endMs(4), startOff(4), endOff(4),
// then optional embedded frames — TIT2 carries the chapter title.
//
// Opus/Ogg: audiobook chapters live in the OpusTags/VorbisComment block as
// CHAPTER000=HH:MM:SS.mmm (+ optional CHAPTER000NAME=title) — the convention
// ffmpeg/opusenc/matroska tools write. music-metadata exposes those verbatim
// in the native vorbis tag list.

import fs from "node:fs/promises";
import path from "node:path";
import { parseFile } from "music-metadata";

const SYNCHSAFE = (b, o) =>
  ((b[o] & 0x7f) << 21) | ((b[o + 1] & 0x7f) << 14) | ((b[o + 2] & 0x7f) << 7) | (b[o + 3] & 0x7f);
const U32 = (b, o) => b.readUInt32BE(o);

/** Decode an ID3 text frame body (encoding byte + bytes) to a JS string. */
function decodeText(body) {
  if (!body.length) return "";
  const enc = body[0];
  const raw = body.subarray(1);
  const strip = (s) => s.replace(/\0+$/, "");
  if (enc === 0) return strip(raw.toString("latin1"));
  if (enc === 1) return strip(raw.toString("utf16le").replace(/^﻿/, "")); // BOM handled below
  if (enc === 2) return strip(raw.swap16().toString("utf16le"));
  return strip(raw.toString("utf8"));
}

/** Walk frames in `buf`, calling fn(id, body). v2.3 and v2.4 sizes differ. */
function eachFrame(buf, major, fn) {
  let i = 0;
  while (i + 10 <= buf.length) {
    const id = buf.toString("latin1", i, i + 4);
    if (!/^[A-Z0-9]{4}$/.test(id)) break; // padding
    const size = major >= 4 ? SYNCHSAFE(buf, i + 4) : U32(buf, i + 4);
    if (size <= 0 || i + 10 + size > buf.length) break;
    fn(id, buf.subarray(i + 10, i + 10 + size));
    i += 10 + size;
  }
}

/** "HH:MM:SS.mmm" (or MM:SS.mmm) → milliseconds, NaN when malformed. */
function parseChapterTime(s) {
  const m = /^(?:(\d+):)?(\d+):(\d+(?:\.\d+)?)$/.exec(s.trim());
  if (!m) return NaN;
  const h = m[1] ? parseInt(m[1], 10) : 0;
  return ((h * 60 + parseInt(m[2], 10)) * 60 + parseFloat(m[3])) * 1000;
}

/** Vorbis-comment chapters (Opus/Ogg): CHAPTER000[NAME]= pairs. */
async function readVorbisChapters(file) {
  try {
    const md = await parseFile(file, { skipCovers: true });
    const tags = [];
    for (const fmt of ["vorbis"]) {
      for (const t of md.native?.[fmt] ?? []) tags.push(t);
    }
    const starts = new Map(); // "000" → ms
    const names = new Map();  // "000" → title
    for (const { id, value } of tags) {
      const m = /^CHAPTER(\d{1,3})(NAME)?$/i.exec(id);
      if (!m || typeof value !== "string") continue;
      if (m[2]) names.set(m[1], value);
      else {
        const ms = parseChapterTime(value);
        if (!Number.isNaN(ms)) starts.set(m[1], ms);
      }
    }
    const chapters = [...starts.entries()]
      .map(([k, startMs]) => ({ startMs: Math.round(startMs), title: names.get(k) ?? "" }))
      .sort((a, b) => a.startMs - b.startMs);
    chapters.forEach((c, i) => {
      if (!c.title) c.title = `Chapitre ${i + 1}`;
    });
    return chapters;
  } catch {
    return [];
  }
}

/**
 * Read chapters from `file` (ID3v2 CHAP for MP3, vorbis comments for
 * Opus/Ogg). Returns [{ startMs, title }] sorted by startMs, [] when none.
 */
export async function readChapters(file) {
  const ext = path.extname(file).toLowerCase();
  if (ext === ".opus" || ext === ".ogg") return readVorbisChapters(file);
  let fh;
  try {
    fh = await fs.open(file, "r");
    const head = Buffer.alloc(10);
    await fh.read(head, 0, 10, 0);
    if (head.toString("latin1", 0, 3) !== "ID3") return [];
    const major = head[3];
    const tagSize = SYNCHSAFE(head, 6);
    const tag = Buffer.alloc(tagSize);
    await fh.read(tag, 0, tagSize, 10);

    const chapters = [];
    eachFrame(tag, major, (id, body) => {
      if (id !== "CHAP") return;
      const z = body.indexOf(0);
      if (z < 0 || body.length < z + 1 + 16) return;
      const startMs = U32(body, z + 1);
      let title = "";
      eachFrame(body.subarray(z + 1 + 16), major, (sid, sbody) => {
        if (sid === "TIT2") title = decodeText(sbody);
      });
      chapters.push({ startMs, title });
    });
    chapters.sort((a, b) => a.startMs - b.startMs);
    // Name the unnamed so the UI always has something to show.
    chapters.forEach((c, i) => {
      if (!c.title) c.title = `Chapitre ${i + 1}`;
    });
    return chapters;
  } catch {
    return [];
  } finally {
    await fh?.close();
  }
}
