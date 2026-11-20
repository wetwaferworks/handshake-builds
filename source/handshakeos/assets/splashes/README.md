# HandshakeOS RG35XX 2024 Splash Pack v1

Hardware-calibrated selectable boot-splash pack for HandshakeOS on the
Anbernic RG35XX 2024, built to Cortana's verified kernel / framebuffer
specs and JD's instruction to redo logos for exact 4:3 fit and
proportions.

**Status:** `PENDING-REVIEW`. Nothing has been sent to Cortana or copied
to TheWorkstation.

JD approved including all 8 `o.O` options as a pack on **November 19,
2026 at 5:36 PM ET**, then asked that some logos be redone so they fit
the RG35XX's exact 4:3 proportions. Four emblems — Silicon Wafer Pulse
(01), Handheld Phosphor (03), Oscilloscope Dual-Wave (07), and Liquid
Glass Orb (08) — were regenerated as native 4:3 masters. The other four
approved 1:1 originals were refit onto exact 640×480 OS canvases.

Public / boot branding is locked to **HandshakeOS**. The Squadroom
nickname is reserved and does not appear on any splash, icon, or
wordmark in this pack.

## Credits

- HandshakeOS name and `o.O` logo direction by **JD**
- Artwork, 4:3 redos, and pack by **CloudAtlas · Technical Art Director**
- Framebuffer, safe-area, and theme values verified by **Cortana · Intel & Clarity**

## Hardware (verified)

- **Device:** Anbernic RG35XX 2024 (Allwinner H700)
- **Panel:** 3.5" 4:3 IPS, NewVision NV3052C, ~71×53 mm active glass, **~229 PPI**, 60 Hz, 24-bit RGB888 DPI
- **Canvas:** exact **640×480**, square 1:1 pixels, **native landscape (0° rotation)**
- Mini-HDMI mirrors the same canonical 640×480 canvas

## PNG input for the zero-decoder build-time packer

Every boot splash in the pack root is:

- exact **640×480**
- opaque **8-bit RGB** (Pillow mode `"RGB"`)
- **no alpha**, **no palette**
- **sRGB**
- **no EXIF** (and therefore no orientation tag)

Target scanout is `XRGB8888`, with automatic fallback conversion to
16-bit `RGB565`. A very light Bayer dither is applied only across dark
vignette / gradient falloffs; pure flat background margins and the
reserved bottom status strip are left untouched so dark purple / green
gradients do not stair-step on RGB565. An automated RGB565 round-trip
check is part of the build.

Suggested packer path: decode PNG → tightly packed 640×480 pixels →
either `0x00RRGGBB` XRGB8888 words or 16-bit RGB565
`(r>>3)<<11 | (g>>2)<<5 | (b>>3)`. Blit the raw buffer to `/dev/fb0`.

## Safe area

- 24 px outer bezel margin: content lives in **x: 24..615**, **y: 24..440**
- **Bottom 36 px (`y: 444..480`) is reserved** — solid OS background only
  (`#12081F` Deep Purple or `#020802` Matrix Green) for an optional boot
  status / version line. No subtitle, no micro-caption, no typography in
  that strip.
- Emblem band: **y: 28..356**
- `HandshakeOS` wordmark band: **y: 374..414** (bottom ≤ 418, ≥ 26 px
  clearance above y: 444)
- Key strokes stay **≥ 2 px** at ~229 PPI

## HandshakeOS UI palettes (`src/theme.c`)

**Deep Purple (`purple`)** — 01, 02, 03, 04, 05, 06, 08

| Token | Hex |
|-------|-----|
| bg | `#12081F` |
| bar | `#1B0E2E` |
| surface | `#1E1033` |
| tile | `#2A1848` |
| accent | `#8E2DE2` |
| accent2 | `#E0B0FF` |
| border_sel | `#C77DFF` |
| text | `#F3E8FF` |
| good | `#66FFAA` |

Wordmark on purple canvases: **Handshake** in `#F3E8FF`, **OS** in `#66FFAA`,
with a very subtle matching glow.

**Matrix Green (`green`)** — 07 Oscilloscope Dual-Wave (flagship green splash)

| Token | Hex |
|-------|-----|
| bg | `#020802` |
| bar | `#041004` |
| surface | `#051405` |
| tile | `#0A1F0A` |
| accent | `#00FF41` |
| accent2 | `#39FF14` |
| text | `#C8FFCE` |
| dim | `#3D8B44` |

Wordmark on the green canvas: **Handshake** in `#C8FFCE`, **OS** in `#00FF41`,
with a very subtle phosphor glow.

Typography is Orbitron variable, weight 780, 36 px, ASCII only.

## The 8 splashes

| Id | Title | Theme | Treatment | File |
|----|-------|-------|-----------|------|
| 01 | Silicon Wafer Pulse | purple | 4:3 REDO | `01-silicon-wafer-pulse-purple.png` |
| 02 | Field of Handshakes | purple | 4:3 REFIT | `02-field-of-handshakes-purple.png` |
| 03 | Handheld Phosphor | purple | 4:3 REDO | `03-handheld-phosphor-purple.png` |
| 04 | Timing Loop Gears | purple | 4:3 REFIT | `04-timing-loop-gears-purple.png` |
| 05 | Minimal Swiss Signal | purple | 4:3 REFIT | `05-minimal-swiss-signal-purple.png` |
| 06 | Cyber Owl Sprite | purple | 4:3 REFIT | `06-cyber-owl-sprite-purple.png` |
| 07 | Oscilloscope Dual-Wave | green | 4:3 REDO | `07-oscilloscope-dual-wave-green.png` |
| 08 | Liquid Glass Orb | purple | 4:3 REDO | `08-liquid-glass-orb-purple.png` |

Native 4:3 masters for the four redos live in `source-4x3/`
(`01-silicon-wafer-pulse-4x3.png`, `03-handheld-phosphor-4x3.png`,
`07-oscilloscope-dual-wave-4x3.png`, `08-liquid-glass-orb-4x3.png`).

Square icons in `icons/{1024,512,256,64}/` use the original approved
1:1 emblems (LANCZOS, light unsharp at 256 and 64) so the 64×64 menu
icons stay sharp.

## Folder layout

```
splashes/
  build_splashes.py
  README.md
  manifest.txt
  01-silicon-wafer-pulse-purple.png
  … (8 boot splashes)
  source-4x3/          4 native 4:3 masters
  icons/1024|512|256|64/
  previews/
    handshakeos-splashes-640x480-sheet.png
    handshakeos-splashes-safearea-check.png
    handshakeos-redos-before-after.png
```

Zip (sibling of this folder):

`/workspace/scratch/handshakeos/handshakeos-oO-splash-pack-v1.zip`
(16.08 MB)

Rebuild:

```
python3 /workspace/scratch/handshakeos/splashes/build_splashes.py
```

## manifest.txt

Pipe-delimited `id | title | artist | type | theme | file` with a header
comment. All 8 graphic rows are filled (`CloudAtlas | graphic | purple`
or `green`). A commented placeholder at the bottom tells Cortana how to
append the five squad ASCII splash entries; this pack does not invent
ASCII filenames.

## Automated verification (this build)

- `01-silicon-wafer-pulse-purple.png`: 640×480 RGB, EXIF=0, reserved-bg max Δ=0.000, RGB565 mid max err=7.00, wordmark bbox=(177, 380, 463, 408), status clearance=36 px.
- `02-field-of-handshakes-purple.png`: 640×480 RGB, EXIF=0, reserved-bg max Δ=0.000, RGB565 mid max err=7.00, wordmark bbox=(177, 380, 463, 408), status clearance=36 px.
- `03-handheld-phosphor-purple.png`: 640×480 RGB, EXIF=0, reserved-bg max Δ=0.000, RGB565 mid max err=7.00, wordmark bbox=(177, 380, 463, 408), status clearance=36 px.
- `04-timing-loop-gears-purple.png`: 640×480 RGB, EXIF=0, reserved-bg max Δ=0.000, RGB565 mid max err=7.00, wordmark bbox=(177, 380, 463, 408), status clearance=36 px.
- `05-minimal-swiss-signal-purple.png`: 640×480 RGB, EXIF=0, reserved-bg max Δ=0.000, RGB565 mid max err=7.00, wordmark bbox=(177, 380, 463, 408), status clearance=36 px.
- `06-cyber-owl-sprite-purple.png`: 640×480 RGB, EXIF=0, reserved-bg max Δ=0.000, RGB565 mid max err=7.00, wordmark bbox=(177, 380, 463, 408), status clearance=36 px.
- `07-oscilloscope-dual-wave-green.png`: 640×480 RGB, EXIF=0, reserved-bg max Δ=0.000, RGB565 mid max err=7.00, wordmark bbox=(177, 380, 463, 408), status clearance=36 px.
- `08-liquid-glass-orb-purple.png`: 640×480 RGB, EXIF=0, reserved-bg max Δ=0.000, RGB565 mid max err=7.00, wordmark bbox=(177, 380, 463, 408), status clearance=36 px.

All eight splashes passed: size, mode, no EXIF, no alpha, reserved
margin + status strip within ≤ 2 RGB of the exact OS background, no
hard rectangular crop seam, and a clean RGB565 round-trip on gradient
falloffs.
