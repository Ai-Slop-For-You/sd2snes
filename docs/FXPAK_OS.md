# FXPAK OS

FXPAK OS is an experimental, artwork-driven frontend for FXPAK Pro based on the sd2snes menu ROM and firmware.

The visual target is a modern console-library experience rendered honestly within SNES hardware constraints: dark dashboard chrome, SNES-purple accents, cover art, metadata, favorites, recent games, homebrew/hacks, controller prompts, and fast navigation on a CRT.

## Base

Development starts from the upstream `themes` work (upstream PR #240), not plain `master`. That branch already provides the pieces we want to preserve and extend:

- custom logo
- custom font
- custom button graphics
- font palettes
- background gradient
- configurable selection-bar colors
- arbitrary OAM sprite placement
- full-screen themed sprite backgrounds

The existing menu is a SNES ROM written in 65C816 assembly under `snes/`. The cartridge MCU firmware under `src/` supplies filesystem/configuration data. The existing renderer already uses BG1, BG2, OBJ, HDMA, color math, palette DMA, and generated font tiles.

## Design principles

1. **Do expensive work off-console.** PNG/JPEG decoding, metadata lookup, resizing, quantization, and indexing happen on a PC. The SNES only consumes compact native assets.
2. **Never make ROM launching less reliable.** The existing loader/MCU protocol remains the foundation. UI work should be separable from game loading.
3. **CRT-first readability.** Important text must remain readable at 240p. The generated concept is a direction, not permission to use microscopic UI.
4. **Fast first frame.** A library screen must appear quickly even with a large SD card. Assets and metadata are indexed ahead of time.
5. **Progressive enhancement.** A ROM without artwork/metadata must still be launchable.
6. **Themeable by construction.** Colors, fonts, backgrounds, buttons, and decorative sprites should stay data-driven where practical.

## Target screens

### Library / Home

The primary screen is a game-library dashboard:

- top navigation: SNES / Game Boy / NES / Favorites / Recent / Homebrew / Settings
- selected game cover at left
- title, publisher/year, genre and players at right
- horizontal/compact neighboring-game strip
- bottom controller hints
- selected-game emphasis through a purple/lavender highlight treatment

Early releases may retain the stock list as a fallback while the artwork view matures.

### Game Details

- cover art
- title and metadata
- short description
- save type
- enhancement-chip badge (SA-1, SuperFX, DSP, CX4, S-DD1, etc.)
- MSU-1 indicator
- screenshots
- favorite toggle
- play / back actions

### Collections

- Favorites
- Recently Played
- Homebrew
- Hacks & Translations
- Multiplayer
- Enhancement Chip
- MSU-1

### Settings

Preserve all existing functional settings while presenting them through the new visual language.

## Rendering budget

The first artwork target is an **80 x 112 pixel cover**:

- 10 x 14 8x8 tiles
- 140 tiles
- 4bpp
- 4,480 bytes of tile graphics
- 32-byte 16-color palette
- 280-byte tilemap

This is intentionally small enough to be practical in VRAM while still reading clearly on a CRT. Later builds can support alternate detail-screen assets.

## Asset pipeline

`tools/fxpakos/compile_art.py` converts a source image into native SNES assets:

- `.4bpp` — SNES planar tile graphics
- `.pal` — 16-entry little-endian SNES BGR555 palette
- `.map` — little-endian SNES tilemap words
- `.json` — dimensions/offsets/check information for tooling
- `.preview.png` — quantized preview of what was encoded

Example:

```bash
python3 tools/fxpakos/compile_art.py \
  covers/Super_Mario_World.png \
  build/Super_Mario_World \
  --width 80 --height 112
```

Pillow is the only Python dependency for the first tool:

```bash
python3 -m pip install Pillow
```

## Planned SD-card layout

```text
sd2snes/
  fxpakos/
    library.fxl
    games/
      <game-id>/
        cover.4bpp
        cover.pal
        cover.map
        meta.bin
        shot0.4bpp
        shot0.pal
        shot0.map
    themes/
      default/
        ...
```

The exact index format is deliberately versioned and will be introduced only after the first renderer proves the asset-loading path.

## Milestones

### V0.1 — Visual shell

- inherit upstream theme support
- establish FXPAK OS branding/style tokens
- CRT-safe header/footer/navigation chrome
- retain stock file list and launch path as fallback
- prove native artwork conversion

### V0.2 — Artwork library

- load one precompiled cover from SD
- render selected-game cover beside a compact list
- simple title/metadata panel
- placeholder art fallback

### V0.3 — Indexed library

- PC-side ROM scanner
- stable game IDs
- metadata index
- artwork cache
- Favorites / Recent integration

### V0.4 — Details

- detail page
- screenshots
- chip/MSU/save badges
- richer metadata

### V1.0 — FXPAK OS

- polished dashboard navigation
- collections
- themes
- library builder
- robust fallback behavior
- large-library performance work

## Non-goals for the first releases

- runtime PNG/JPEG decoding on the SNES
- streaming video previews in the library
- replacing the proven ROM-loader protocol
- requiring metadata/artwork to launch a game
- pretending the SNES is a 1080p UI platform

The goal is not to imitate a PC frontend pixel-for-pixel. The goal is to make the best-looking, fastest, most coherent library interface that feels native to original SNES hardware.
