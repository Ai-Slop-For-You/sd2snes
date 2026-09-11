# FXPAK OS Phase 1 implementation and validation

## Result

The existing menu now displays the original **STARFALL** demo cover at **80x112**
using actual SNES OBJ rendering, beside the existing file browser. STARFALL is
bundled test artwork, not metadata for the highlighted ROM. The default banner
is FXPAK OS, the background is navy, and the existing purple selection bar,
controller handling, fonts, dialogs, favorites/recent and loader remain in use.

The original logo and decorative sprite data remain in the ROM. This dashboard
replaces their default on-screen placement; arbitrary external theme placement
must respect the reservations below. This is a static-art milestone, not yet an
SD artwork loader or a finished theme-compatible dashboard.

## Build

Requirements: GNU make 4.3+, GCC/G++, bash, coreutils, curl, tar; Python 3 and
Pillow for regenerating artwork and testing. No root installation or Qt needed.
From the repository root:

```sh
tools/fxpakos/build_toolchain.sh
export PATH="$PWD/.tools/bin:$PATH"
make -C snes clean
make -C snes -j2
cmp snes/menu.bin snes/m3nu.bin
```

The toolchain script downloads snescom **1.8.1.1**, checks SHA-256
`11583f960e217ddc672848979797d62106d40a284e0c17b8acf75d504ba1dbaa`, and builds
snescom/sneslink locally. Contrary to the old README, this release does not
need Boost. Both menu artifacts are identical **65,536-byte** ROMs. `m3nu.bin`
is the primary Mk.III artifact. Runtime FPGA/MCU firmware has not been replaced.

The generated `snes/fxpakos_cover.i65` is checked in so a normal menu build has
no Python dependency. Regenerate it with:

```sh
make -C snes art
make -C snes
```

`make_test_cover.py` creates original CC0 cover artwork with no downloaded
images or system fonts. `compile_art.py` produces the 4,480-byte planar image,
32-byte BGR555 palette and 280-byte tilemap. Palette index 0 is reserved for
transparency; opaque colors use 1..15. The preview reflects BGR555 quantization.
`bundle_cover.py` consumes those tiles and map, pads rows for OBJ addressing,
and emits assembly data. It does not decode image formats on the SNES.

## Rendering and timing

- BG1/BG2, Mode 3/5 switching, font generation, HDMA and the window stack are
  retained. File descriptors, directory cache/history, ROM selection and MCU
  commands are unchanged. Only file-list X/width and cursor geometry changed.
- Banner text is written to `$7ea000` at setup, with NMI disabled. The old
  overlay called shared, non-reentrant text routines from NMI and wrote its
  header to `$7fa000`. Both problems are removed. The footer uses the existing
  foreground statusbar routine and honors modal statusbars.
- Artwork uses **35 16x16 sprites**, five across/seven down, at `(8,72)`.
  OAM entries **32..66**, high-table bytes **8..16** (last byte partially used),
  and OBJ palette **0**, CGRAM **$80..$8f**, are reserved. Palette 0 avoids
  selection-bar color math affecting the cover.
- VRAM **byte addresses $c800..$e3ff** (PPU word address `$6400`) hold the
  padded tile rows: 7,168 bytes, including unused stride padding. This avoids
  the existing font/tilemap regions and sprite tiles 0..39. OBJ tile IDs start
  at 64; IDs >=256 use the second name table via the OAM attribute bit.
- All cover graphics/palette and size flags upload during forced blank.
  NMI only transfers **140 OAM bytes** when window-stack visibility changes.
  Opening a dialog hides the cover; closing it restores the cover. No text
  renderer or WRAM data-port operations run in this NMI addition.
- Tilemap DMA now copies rows **9..32**, rather than 9..48, on each BG.
  Those rows cover even 239 lines plus the maximum 15-pixel scroll; the removed
  rows were invisible. This saves **2,048 DMA bytes per frame**. Before the
  reduction, visibility changes ran NMI into the next frame (~51,950 master
  clocks). Afterward, the worst measured NMI is below **35,000 master clocks**,
  with every NMI returning inside VBlank in both NTSC and PAL tests.

## Emulator regression test

```sh
export PATH="$PWD/.tools/bin:$PATH"
tools/fxpakos/tests/run_emulator.sh
```

The script fetches bsnes-plus commit
`a9789fab9a26859153c2963defe186fcaaa80ca2`, compiles its **unmodified accuracy
CPU/PPU core** with a small headless frontend, and runs the production `m3nu.bin`.
Set `BSNES_SOURCE=/path/to/bsnes-plus` to reuse that exact checkout.
No ROM-code patches, cheat overrides or forced PCs are used.

The harness models the absent cartridge MCU explicitly: SRAM configuration,
root/file descriptors, directory replacement, and ACK responses delayed three
frames. It records the selected path and command sequence. It is **not** an
emulation of the actual FXPAK MCU, SD card or FPGA.

Each NTSC/PAL run exercises 1,156 frames and asserts:

- boot reaches menu; 600 idle frames each produce exactly one NMI;
- NMI is non-reentrant, returns in VBlank, and the foreground stack balances;
- no VRAM/OAM writes occur during active display;
- Down/Up, Left/Right paging and L/R first/last navigation work;
- A enters a directory; B returns with the saved selection;
- long filenames scroll within the narrowed list;
- X main menu, Y context menu, Select favorites and Start recent open/close;
- modal screens hide the cover and restore it afterward;
- the selected `/FXPAK Demo.sfc` and its descriptor reach `CMD_LOADROM`;
- delayed ACK, `CMD_FPGA_RECONF`, WRAM/BRAM fade and `CMD_RESET` complete.

The separate render verifier checks every pixel of the full 80x112 cover
(on both high-resolution output samples), verifies its tile/palette bytes,
and checks font/OBJ VRAM and non-backdrop CGRAM stability across eight browser
and four modal captures per region. Four compiler tests independently check
planar bit order, every pixel round trip, transparency, color packing and bounds.

Evidence is written under `.build/tests/{NTSC,PAL}`: logs, raw framebuffers,
PNG screenshots and WRAM/VRAM/CGRAM/OAM dumps. `*-crt.png` shows a 256x224 aspect
preview enlarged 3x; the native framebuffer remains available as `*.png`/`*.ppm`.
This preview simulates horizontal sample averaging, not CRT phosphor effects.

## Limits and hardware validation still required

The test stops after `CMD_RESET`. An actual game boot, FPGA reconfiguration,
real SD access, save persistence, warm return from a game, SPC playback,
external themes, enhancement-chip games and physical CRT/controller behavior
still require FXPAK Pro hardware. No physical hardware was available here.
The harness validates the preserved menu-side loader path, not those devices.

## Next step: selected-game artwork from the MCU

Keep the existing LOADROM/READDIR command contract intact. Add a separate,
versioned optional artwork request carrying the selected file identity and a
monotonic generation number. The MCU reads a fixed-size precompiled cover
record into a newly reserved cartridge SRAM region after checking its size,
version and checksum, then publishes completion last. The SNES accepts only
completion for its current selection. Missing/invalid art leaves the bundled
fallback visible and never blocks launch.

Choose the staging SRAM address only after auditing all FPGA/firmware mappings
(`$ff2000` WRAM backup and `$ff4000` favorites are already occupied). Transfer
new OBJ tiles during forced blank initially, or with bounded multi-frame
VBlank DMA into an inactive tile region. Switch tile/palette/OAM together after
the complete asset is ready. Cache a previous/next cover on the MCU, not by
adding image decoding or filesystem operations to SNES code. Dynamic loading
and metadata are deliberately deferred until after this static-cover milestone.
