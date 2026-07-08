# Handoff: Pocket Tunes — Music Player Core for Analogue Pocket (design 1b)

## Overview
A folder/metadata-driven music player interface for the Analogue Pocket, styled as an amber-on-black terminal. Navigation mirrors the on-disk folder structure: **Artist (folder) → Album (folder) → Track (file)**. Playback surfaces in a bottom drawer over the album list, so the user never leaves the browsing context.

## About the design files
The bundled HTML file (`Pocket Tunes.dc.html`) is a **design reference built in HTML/React-like templating for prototyping only** — it is not production code and must not be shipped as-is. It exists to communicate exact layout, color, type, spacing, and interaction behavior. The actual implementation target is the **Analogue Pocket openFPGA "core" environment** — normally a Verilog/FPGA core plus a bare-metal/C companion app for the ARM co-processor (or whatever existing homebrew framework the project already uses). Recreate this UI natively in that environment: your own bitmap/font rendering, sprite blitting, or whatever the project's existing UI toolkit is — matching the pixels and behavior described below, not by embedding a web view.

If the project has no existing rendering framework yet, pick the most appropriate one for openFPGA/Pocket homebrew development (e.g. the standard Analogue Pocket core template) rather than inventing a new one.

## Fidelity
**High-fidelity.** Colors, spacing, typography sizes, and states below are final for this direction (option "1b" of an earlier round of exploration) — implement them precisely. The only intentionally-loose parts are: the placeholder album-art gradients (real designs will use embedded/folder cover art images) and the exact VU-meter/bar animation curve (any pleasant audio-reactive or idle animation in that spirit is fine).

## Target display
Design proportions are based on a 264×238 mockup canvas (11:10 aspect, portrait-ish square). The real Analogue Pocket screen is 1600×1440 px (native LCD resolution). **Scale the whole layout up proportionally** to fill the real screen — keep all relative proportions, paddings, and font-size ratios identical; do not just center a small box. At 1600px width the ~78px sidebar becomes ~473px, an 11px font becomes ~67px, etc. (multiply every value in this doc by ~6.06 to get real-device px, or recompute proportionally against the true screen size your framework reports.)

## Screens / Views

### 1. Library Browser (base view, always mounted)
**Purpose:** browse the Artist → Album folder tree and pick what to open.

**Layout:** two-column layout filling the screen, `flex-direction: row`.
- **Left sidebar** — fixed width 78/264 of screen width (~29.5%), full height, scrollable vertical list, right border 1px solid amber `hsl(45 60% 35%)`ish (`oklch(0.35 0.1 55)`), 6px vertical padding.
  - One row per **Artist** (top-level folder). Row: padding 4px 7px, font-size 11/264 of width (~4.2% → scale accordingly), no wrap (`text-overflow: ellipsis`), color `oklch(0.85 0.15 55)` amber-bright when it's the artist currently shown in the main pane, else dimmer `oklch(0.55 0.1 55)`. When a row is the current *cursor* row (D-pad focus in sidebar), background highlight `rgba(255,150,50,0.15)`.
- **Right main pane** — flex:1, padding 7px, scrollable.
  - Header line: `"{ArtistName}/"` — font-size ~10px, color `oklch(0.65 0.15 55)`, letter-spacing 0.5px, margin-bottom 5px. Reinforces "this is a folder path."
  - Below: one row per **Album** (second-level folder) belonging to the selected artist. Row: flex row, gap 7px, padding 4px, margin-bottom 4px, 1px border — border is transparent normally, becomes bright amber `oklch(0.7 0.18 55)` when that album is the cursor row (D-pad focus in main pane).
    - Left: 26×26px square cover-art thumbnail (album art image if available; gradient placeholder otherwise), `filter: saturate(1.3)` to keep it punchy against the dim palette.
    - Right (stacked): album title, font-size 11px, color `oklch(0.85 0.1 55)`, ellipsis on overflow. Below it: `"{year} · {genre}"`, font-size 9px, color `oklch(0.55 0.1 55)`.

**Background:** whole screen `#0b0904` (near-black warm brown). A full-screen **CRT scanline overlay** sits above everything (see Design Tokens) for texture.

**Typography:** monospace throughout — `Share Tech Mono` (or nearest monospace bitmap/pixel font available on-device).

### 2. Now Playing Drawer (overlay, slides over bottom of Library Browser)
**Purpose:** shows/controls the currently loaded track without leaving the album list.

**Layout:** absolutely positioned panel, `left:0; right:0; bottom:0; height: 64%` of the screen, background `#120e07`, top border 1px solid `oklch(0.6 0.15 55)`, z-index above the browser, internal padding 8px, `flex-direction: column`.

Contents top to bottom:
1. **Header row** (`flex-direction: row`, gap 8px):
   - 46×46px square cover art (image or gradient placeholder), flex:none.
   - Title/subtitle block (flex:1, min-width:0): track title, font-size 12px, color `oklch(0.88 0.1 55)`, ellipsis; below it `"{Album title} · piste {n}/{total}"`, font-size 9px, color `oklch(0.6 0.1 55)`.
   - Play-state label pinned top-right of the row: font-size 9px, color `oklch(0.7 0.15 55)`, text is `"► LECTURE"` when playing, `"❚❚ PAUSE"` when paused.
2. **Progress bar** — margin-top 6px, height 4px, track color `oklch(0.3 0.05 55)`, fill color `oklch(0.7 0.18 55)`, width = playback % (0–100).
3. **Metadata block** — margin-top 6px, font-size 9.5px, line-height 1.4, color `oklch(0.65 0.1 55)`. Two lines:
   - `"{duration} total · {genre} · {year}"`
   - `"{format} · {bitrate}"` (e.g. `"FLAC · 1411 kbps"`)
   - These three metadata fields (duration/remaining, format, bitrate) must be **individually toggleable** per the product brief — expose a settings/menu option to hide any of them; when hidden, collapse that line rather than leaving blank space.
4. **Control hint footer** — pinned to bottom via `margin-top: auto`, font-size 9px, color `oklch(0.55 0.1 55)`: `"▲▼ piste · A lecture/pause · B fermer"`.

Note: an animated VU-meter/bar visualizer appeared in sibling explorations (12 bars, staggered pulse animation) — optional nice-to-have for this screen if the audio pipeline can expose levels; not required for v1.

## Interactions & Behavior (D-pad / A / B mapping)

**State model:** `focus` ('sidebar' | 'main'), `sidebarCursor` (index into artist list), `currentArtistId`, `mainCursor` (index into current artist's album list), `drawerOpen` (bool), `currentTrackIndex`, `isPlaying`, `progress` (0–1).

- **D-pad Up/Down:**
  - If drawer is open: skip to previous/next track in the current album (wraps), pause and reset progress to 0 (playback of the new track should then auto-start — see note below; current prototype pauses on skip, but real implementation should auto-play the new track for a real player).
  - Else if focus = sidebar: move sidebar cursor up/down (wraps).
  - Else if focus = main: move main-pane cursor up/down (clamped, no wrap, since it's a flat list not a ring in this state).
- **D-pad Left/Right:** unused in this design (reserved — could later map to seek ±10s while drawer is open).
- **A (activate):**
  - If drawer open: toggle play/pause.
  - Else if focus = sidebar: select that artist (`currentArtistId` = chosen, `focus` → 'main', `mainCursor` → 0) — main pane now shows that artist's albums.
  - Else if focus = main: open the drawer for the selected album (`drawerOpen` = true, `currentTrackIndex` = 0, start paused, `progress` = 0). Real implementation: auto-start playback on drawer open.
- **B (back):**
  - If drawer open: close drawer, return focus to wherever it was (main pane), keep the album context.
  - Else if focus = main: return focus to sidebar (does not change selected artist).
  - (At sidebar focus with no drawer, B is a no-op / could map to "exit to system menu.")
- **L/R shoulder buttons:** not used by this design (reserved for a future theme-switcher or quick artist-jump, e.g. jump to next letter of the alphabet).

## State Management
- `library`: tree of `{ artistId, artistName, albums: [{ albumId, title, year, genre, coverArt, tracks: [{ title, duration, format, bitrate, filePath }] }] }` — built by scanning the SD card's music folder structure and reading embedded/sidecar metadata + cover art at boot or on library refresh.
- `ui`: `{ focus, sidebarCursor, currentArtistId, mainCursor, drawerOpen, currentTrackIndex, isPlaying, progress }`.
- `settings.visibleMetaFields`: `{ duration: bool, format: bool, bitrate: bool }` — persisted user preference for which metadata lines show in the drawer.
- Playback progress should be driven by the actual audio decoder's sample position, polled/pushed at whatever cadence the audio pipeline allows (prototype simulates with a 200 ms timer incrementing progress by 0.6%/tick while playing).

## Design Tokens

**Colors** (all amber/warm-brown monochrome family, expressed here as OKLCH — convert to sRGB hex for a fixed-palette/indexed-color renderer if the FPGA output requires it):
- Screen background: `#0b0904`
- Drawer background: `#120e07`
- Sidebar divider: `oklch(0.35 0.1 55)`
- Artist name (active): `oklch(0.85 0.15 55)`
- Artist name (inactive): `oklch(0.55 0.1 55)`
- Sidebar cursor highlight bg: `rgba(255,150,50,0.15)`
- Folder path label: `oklch(0.65 0.15 55)`
- Album title: `oklch(0.85 0.1 55)`
- Album meta (year/genre): `oklch(0.55 0.1 55)`
- Album row focus border: `oklch(0.7 0.18 55)`
- Drawer track title: `oklch(0.88 0.1 55)`
- Drawer subtitle / play-state label: `oklch(0.6 0.1 55)` / `oklch(0.7 0.15 55)`
- Progress track: `oklch(0.3 0.05 55)`; progress fill: `oklch(0.7 0.18 55)`
- Metadata text: `oklch(0.65 0.1 55)`
- Footer hint text: `oklch(0.55 0.1 55)`
- CRT scanline overlay: repeating horizontal lines, `rgba(0,0,0,0.35)` 2px band / transparent 2px band, full-screen, non-interactive, drawn above all content.

**Typography:** monospace family (`Share Tech Mono` in the prototype — substitute the nearest bitmap/monospace font available for on-device text rendering). Sizes (at 264px-wide mockup scale — multiply by ~6 for real 1600px screen):
- Sidebar artist row: 11px
- Folder path header: 10px
- Album title: 11px / Album meta: 9px
- Drawer track title: 12px / Drawer subtitle: 9px / play-state label: 9px
- Metadata lines: 9.5px
- Footer hint: 9px

**Spacing:** sidebar row padding 4px 7px; main-pane padding 7px; album row padding 4px, gap 7px, margin-bottom 4px; drawer padding 8px; drawer header gap 8px.

**Borders/radii:** everything square (0 border-radius) — consistent with the terminal aesthetic. 1px hairline borders throughout.

## Assets
- Album/artist cover art: sourced from the music files themselves (embedded ID3/FLAC picture tags) or a same-folder cover image (`cover.jpg`/`folder.png` convention) — read from the SD card, not bundled. Prototype uses CSS gradient placeholders keyed by a per-album hue value; replace with real decoded thumbnails, letterboxed/cropped to the 26×26 (list) and 46×46 (drawer) squares.
- Font: Share Tech Mono (Google Fonts) used only for prototyping fidelity — substitute the project's real on-device font rendering.

## Files
- `Pocket Tunes.dc.html` — full interactive prototype. Option **1b** (this handoff's subject) is the second card in the first row, anchor `#1b`. The file also contains sibling explorations (1a, 2a, 2b) for context on directions not chosen — safe to ignore, included for reference only.
- `screenshots/01-library-browser.png` — Library Browser screen (sidebar + album list).
- `screenshots/02-now-playing-drawer.png` — Now Playing drawer open over the album list.
