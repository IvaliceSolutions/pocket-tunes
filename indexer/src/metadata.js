// Reads tags/technical metadata for one audio file via music-metadata.

import { parseFile } from "music-metadata";
import path from "node:path";

export const AUDIO_EXTS = new Set([".mp3", ".wav", ".flac", ".ogg", ".opus"]);

/** Map extension + codec string to the schema's `format` enum. */
function formatFor(ext, codec = "") {
  switch (ext.toLowerCase()) {
    case ".mp3":
      return "MP3";
    case ".wav":
      return "WAV";
    case ".flac":
      return "FLAC";
    case ".opus":
      return "OPUS";
    case ".ogg":
      return /opus/i.test(codec) ? "OPUS" : "OGG";
    default:
      return "UNKNOWN";
  }
}

/**
 * Returns normalized metadata for a file, or throws on unreadable input.
 * `picture` (Buffer|null) is the first embedded cover, handed to the cover module.
 */
export async function readMetadata(absPath) {
  const ext = path.extname(absPath);
  const meta = await parseFile(absPath, { duration: true, skipCovers: false });
  const { format = {}, common = {} } = meta;

  const picture =
    common.picture && common.picture.length > 0
      ? Buffer.from(common.picture[0].data)
      : null;

  return {
    title: (common.title || "").trim() || null,
    artist: (common.albumartist || common.artist || "").trim() || null,
    album: (common.album || "").trim() || null,
    year: Number.isInteger(common.year) ? common.year : null,
    genre: Array.isArray(common.genre) && common.genre.length ? common.genre[0] : null,
    trackNo: common.track?.no ?? null,
    diskNo: common.disk?.no ?? null,
    durationMs: format.duration != null ? Math.round(format.duration * 1000) : null,
    bitrateKbps: format.bitrate != null ? Math.round(format.bitrate / 1000) : null,
    sampleRate: format.sampleRate ?? null,
    channels: format.numberOfChannels ?? null,
    codec: format.codec || format.container || null,
    format: formatFor(ext, format.codec || ""),
    picture,
  };
}
