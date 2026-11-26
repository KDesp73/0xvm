# 0xvm Visual Language & House Style Guide

**Status:** House Guide (Opinionated Convention)  
**Scope:** `0xvm` official hosts, built-in system ROMs (`boot`, `monitor` / `inspector`), and first-party ROMs  
**Compatibility:** Non-enforced for third-party ROMs (third-party creators may use custom palettes, layouts, fonts, and drawing styles)

---

## 1. Purpose & Boundary

This document defines the **house visual language** for `0xvm` in the spirit of `uxn`. It is a design guide for first-party hosts, system tools, and personal ROMs—not a rigid restriction on third-party ROM authors.

### VM Contract vs. House Guide

| Layer | Enforced by VM? | Role in `0xvm` |
| :--- | :--- | :--- |
| **Video Device & Palette Registers** | **Yes** | Core hardware/device interface allowing ROMs to draw and configure colors cleanly. |
| **Asset Formats (`.icn` / `.chr`)** | **Optional / Helper** | Reference binary tile formats supported by helper tools; ROMs may use any data layout. |
| **$8 \times 8$ Grid, House Font & UI Primitives** | **No (House Guide)** | Shared visual grammar for first-party tools, system ROMs, and personal ROMs. |
| **Host Themes (`theme.cfg`)** | **Host-controlled** | Applies consistent colors across hosts and default ROMs unless a ROM explicitly sets its own palette. |

### Third-Party Styling Freedom
* **Palette overrides:** ROMs may set their own palette values when desired, falling back to the active host theme when left at default/unset.
* **Layout & typography freedom:** Third-party ROMs are not restricted to $8 \times 8$ grid alignment, the house bitmap font, or house UI widgets.
* **Minimal host chrome:** Hosts should avoid imposing heavy borders or intrusive framing that clashes with full-bleed or custom-styled ROMs.

---

## 2. Core Aesthetic Principles

1. **Constraint breeds cohesion:** Keep first-party visuals anchored to a 4-color palette rather than unconstrained RGB styling.
2. **Grid-first geometry:** Align first-party UI elements to an $8 \times 8$ pixel cell (`tile`) with crisp pixel edges and no fractional coordinates or smooth drop shadows.
3. **Zero-chrome windowing:** Keep OS-level chrome minimal; place menus, status readouts, and controls inside the ROM/system viewport when appropriate.
4. **Data is visual:** Render memory, stack, and register views using the same tile and pixel primitives used by interactive programs and games.

---

## 3. Virtual Screen & Palette Convention

### Display & Layer Model
* **Tile Unit:** $8 \times 8$ pixels.
* **Reference Resolution:** $320 \times 200$ pixels ($40 \times 25$ tiles) or $256 \times 256$ pixels ($32 \times 32$ tiles), chosen to scale cleanly at integer multipliers and map to terminal cell/sub-pixel renderers.
* **Layers:**
  * **Background (`BG`):** Persistent surface for static panels, text, and grids.
  * **Foreground (`FG`):** Overlay surface (`index 0` transparent) for cursors, active highlights, sprites, and tooltips.

### 4-Color Palette Slots (`0`–`3`)
First-party tools and ROMs assign consistent semantic meaning to the 4 palette indices while allowing hosts to load themes (`theme.cfg`):

| Index | Semantic Role | Default ("Deep Ink") | Alternate ("Paper") |
| :--- | :--- | :--- | :--- |
| **`0`** | Canvas / Background / Transparent (`FG`) | `#060907` | `#FBFCF8` |
| **`1`** | Primary Ink / Text / Active Borders | `#F2F6F0` | `#0B100C` |
| **`2`** | Structural Accent / Muted Frames / Grids | `#2A352C` | `#C7D0C6` |
| **`3`** | Highlight / Cursor / Focus / Alert / Pale Green Accent | `#B9E5AD` | `#4E8B5A` |

* **Contrast & Accent Discipline:** Keep body text high-contrast (index `1` on `0`, or inverted `0` on `1`). The default theme is intentionally extreme: a near-black canvas with near-white ink. The pale-green accent keeps its hue in both themes; the light theme uses a deeper green variant to preserve contrast. Reserve index `3` for focus states, active cursors, key indicators, and primary controls rather than large background fills.

---

## 4. Binary Tile Formats (`.icn` & `.chr`)

* **`.icn` (1-bit tile — 8 bytes):** 8 rows of 8 bits (`0` or `1`). Suitable for monochrome bitmap fonts, icons, borders, and dither patterns.
* **`.chr` (2-bit tile — 16 bytes):** Two 8-byte bitplanes (`plane_low` followed by `plane_high`), producing 2-bit values (`0`–`3`) per pixel for shaded sprites and emblems.

---

## 5. Typography & UI Primitives

### House Bitmap Font (`0xfont.icn`)
* **Cell Size:** $8 \times 8$ pixels per glyph, with clear visual separation for hex characters (`0`–`9`, `A`–`F`) and unambiguous punctuation/brackets.
* **Control / UI Glyphs:** Reserve low control slots for compact icons (arrows, close/check marks, folder/file/ROM markers, play/pause, and pointer indicators).

### Standard House UI Primitives
1. **Hairline Box:** 1-pixel border in color `2` with crisp corners or subtle 1-pixel corner chamfers.
2. **Header Strip:** Single $8\text{px}$ row with inverted styling (index `0` on `1`) showing title/mode on the left and status/address/state on the right.
3. **Buttons & Toggles:** Flat rectangles with square corners. Idle controls use color `2` as fill with color `1` text; primary/active controls use color `3` as fill with color `0` text. No brackets, gradients, rounded corners, or press offset. Focus is a 1px color `3` outline; hover only shifts the border to color `3`.
4. **Dithered Tracks & Scrollbars:** 1-bit checkerboard/dither pattern in color `2` for tracks, with solid thumb markers in color `1` or `3`.
5. **Cursor:** Crisp pointer/crosshair on `FG` with a contrasting outline so it remains readable across light and dark regions.

---

## 6. Host & Ecosystem Alignment

* **Boot / Empty State:** Launching a host without a ROM can boot a built-in system ROM (`boot.rom`) using the house style to display system info, memory/state views, and ROM loading. The browser host does this by default.
* **Boot ROM reference:** `examples/boot.asm` is the canonical first-party splash. It reads `SYS_CAPS` and takes exactly one path: text-only hosts receive the `docs/LOGO.txt` block-glyph grid on the terminal (wordmark excluded) plus the author, license, and VM specifications; screen hosts copy `assets/logo.png` 1:1 into a 200×144 dithered emblem with a hard color-`2` shadow and show centered author, license, and VM specification lines, using the Deep Ink palette slots, an inverted header strip, a hairline content box, and a color-`3` status line; the frame remains visible until Escape or a window close, so it runs indefinitely by default. The wordmark from the source block is not rendered in either path.
* **Browser host controls:** `build/0xvm-wasm.html` opens `boot.rom` automatically. Run/Pause, Restart, load-file, four screen-size presets (320×240, 640×480, 800×600, and 1024×768), and console controls remain available; choosing a preset restarts the current ROM at that resolution. Demo-ROM links and `?rom=` selection are intentionally omitted.
* **Scaling & Framing:** Prefer integer scaling (`1x`, `2x`, `3x`, ...) and optional outer padding in color `0` so content sits cleanly inside the window.
* **Terminal Host Parity:** Map the 4 palette slots to terminal colors and render sub-pixels via half-block or quadrant Unicode glyphs while keeping layout proportions consistent.
* **Naming & Documentation:** Use consistent `0x` hex notation, lowercase identifiers where idiomatic, and clean diagrams aligned with the house visual rules.
