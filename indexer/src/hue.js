// Deterministic per-album hue for the gradient placeholder shown when no cover
// art exists. Stable across regenerations so placeholders don't shuffle.

/** FNV-1a 32-bit hash of a string. */
function fnv1a(str) {
  let h = 0x811c9dc5;
  for (let i = 0; i < str.length; i++) {
    h ^= str.charCodeAt(i);
    h = Math.imul(h, 0x01000193);
  }
  return h >>> 0;
}

/** Map "artist|album" to a hue in [0,359]. */
export function hueForAlbum(artist, album) {
  return fnv1a(`${artist}|${album}`) % 360;
}
