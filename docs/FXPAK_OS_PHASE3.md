# FXPAK OS Phase 3 — selected-game dashboard

Target: **FXPAK Pro Mk.III**. Phase 3 adds a generation-bound selected-game
metadata panel and dashboard composition around the preserved dynamic cover.
The host compiler and MCU/menu implementation are complete and software-tested.
NTSC/PAL validation and review provenance are recorded below. Physical hardware
validation and fresh local FPGA synthesis remain separate, outstanding checks.

## Offline assets and SD layout

Metadata is compiled on the host from a small JSON object. There is no runtime
JSON parser, image decoder, network service, scraping, or database on the FXPAK.
All fields except `title` are optional. Text is printable ASCII; unsupported
characters or overlong fields fail compilation instead of silently truncating.

```json
{
  "title": "STARFALL",
  "publisher": "Example Studio",
  "developer": "Example Studio",
  "genre": "Action",
  "year": 1995,
  "min_players": 1,
  "max_players": 2,
  "chips": ["SUPERFX"],
  "msu1": false
}
```

```sh
python3 tools/fxpakos/compile_metadata.py game.json '/media/SD/ROMs/Game.sfc.fxm'
python3 tools/fxpakos/compile_art.py cover.png .build/art/game
python3 tools/fxpakos/pack_cover.py .build/art/game '/media/SD/ROMs/Game.sfc.fxc'
```

```text
SD:/ROMs/Game.sfc
SD:/ROMs/Game.sfc.fxc
SD:/ROMs/Game.sfc.fxm
SD:/ROMs/Subfolder/Game.sfc
SD:/ROMs/Subfolder/Game.sfc.fxc
SD:/ROMs/Subfolder/Game.sfc.fxm
```

The original ROM extension is retained, including when hidden in the browser.
Each sidecar belongs to the full path; identical basenames in separate folders
are independent. The existing file filter excludes `.fxm` and `.fxc` from the
ROM listing. A metadata-only ROM may show current metadata with the bundled
fallback cover; a cover-only ROM shows its filename and dynamic cover.

## Metadata record and optional service

FXC1 artwork and its mailbox are unchanged. The metadata service observes the
same committed full ROM path and monotonically increasing generation. It owns
its own file handle, copied path, buffer, and deadline. It never changes the
artwork request or response. Metadata I/O starts only after a terminal artwork
response for the same generation. Polling metadata before artwork preserves
the combined limit of one open or read per menu-loop invocation. Existing
commands and reset cancel both services before the original command handler.
An individual FatFs call is still synchronous and subject to the underlying
SD-driver timeout; neither service waits for an entire optional asset.

MCU-owned capability/response mailbox: `$FF5200..$FF523F`:

| Offset | Bytes | Meaning |
|---|---|---|
| 0 | 4 | `FXM1`, capability published last |
| 4 | 2 | Protocol version 1 |
| 6 | 2 | Record size 256 |
| 8 | 1 | Idle 0, busy 1, ready 2, missing 3, invalid 4 |
| 12 | 4 | Response generation, written before status |

Staging: `$FF7200..$FF72FF`. The unused gaps leave the Phase 2 artwork mailbox
`$FF5000..$FF51FF` and staging `$FF6000..$FF71BF` intact. Configuration,
recent/favorites, WRAM backup, directory/ROM/cheat data, scratchpad and loader
parameters retain their documented Phase 2 allocations. Optional workers run
only in menu mode and are cancelled before ROM loading reuses cartridge memory.

The little-endian FXM1 record is exactly 256 bytes:

| Offset | Bytes | Meaning |
|---|---|---|
| 0 | 4 | `FXM1` |
| 4 | 2 | Format version 1 |
| 6 | 2 | Total bytes 256 |
| 8 | 4 | Generation, zero on SD, stamped by MCU |
| 12 | 2 | Payload bytes 224 |
| 14 | 2 | Flags, zero |
| 16 | 2 | CRC-16/CCITT-FALSE of bytes 32..255, initial FFFF, polynomial 1021 |
| 18 | 2 | Inverse CRC |
| 20 | 4 | Inverse generation, FFFFFFFF on SD |
| 24 | 8 | Reserved zero |
| 32 | 64 | Display title, required |
| 96 | 32 | Publisher |
| 128 | 32 | Developer |
| 160 | 24 | Genre |
| 184 | 2 | Year: zero unknown or 1900..2199 |
| 186 | 1 | Minimum players |
| 187 | 1 | Maximum players |
| 188 | 2 | Enhancement-chip mask |
| 190 | 2 | Feature mask, bit 0 MSU-1 |
| 192 | 64 | Reserved zero |

Text fields are NUL-terminated, printable ASCII, with zero padding. Player
counts are both zero for unknown, otherwise `1 <= min <= max <= 8`. Chip bits
0..9 are SA1, SUPERFX, DSP, CX4, SDD1, SPC7110, ST010, ST011, ST018, OBC1.
Unknown flags, malformed fields, bad CRC/version/size, and reserved nonzero
bytes are invalid. The host compiler rejects duplicate JSON keys/chip names,
unknown keys, and invalid types or ranges. Output is deterministic.

## Dashboard renderer

The cover stays at logical `(8,72)`, 80x112 pixels. The right panel uses
19 columns of 8-pixel-wide text, starting at `(100,74)` after the existing
BG scroll. It reserves three rows for a title and shows known year/player,
publisher, developer, genre and chip/MSU-1 fields below it. Unknown fields are
omitted. Long titles and fields have an ellipsis; overflowing chip labels have
a `+`. The header uses a separate lavender Mode 3 palette at CGRAM 112..115.
The normal selection bar is muted purple; active-edit styling remains distinct.
The controller footer reads `A PLAY  B BACK  X MENU  Y MORE`.

The browser keeps its original 18-entry logical page, selection and directory
model. Only presentation changes: a three-row viewport follows the selection
at y186/194/202 (ink begins one scanline lower). The footer starts at y216.
L/R and paging retain
their original meaning; the selected long filename still scrolls. The former
browser clock is omitted to make room for the controller legend; the clock
settings dialog is retained. Existing windows restore the dashboard backing
tiles when closed and hide the cover through the unchanged visibility logic.

Metadata uses private WRAM `$7F1200..$7F12FF` for a snapshot and
`$7F1300..$7F13FF` for accepted data. The Phase 2 private cover record remains
at `$7F0000..$7F11BF`. The foreground validates every header/payload field,
checks the response before/after copying and validation, and only then renders
into the existing tilemap buffers. The existing `screen_dma_disable` flag gates
artwork-generation changes and panel rendering so NMI cannot publish an old
panel with a new cover. No metadata parsing, text rendering or DMA was added
to NMI. Missing/invalid metadata reduces to the selected filename; this also
works with old firmware that has no optional-service capability.

During forced blank, the menu doubles the existing ASCII glyph pixels to make
the panel/footer legible at 256x224. The generated font uses temporary WRAM
`$7F2000..$7F3FFF` and BG1 VRAM bytes `$4000..$5FFF`. Original BG1/BG2 fonts,
tilemaps and fallback/dynamic OBJ regions remain separate and unchanged.
Window backing storage at `$7E2000` is in a different bank. The generated font
stays immutable during browsing. The one-time generation extends startup;
the baseline harness boot wait is 200 frames while its NMI stability threshold
and per-NMI timing requirements remain intact.

The pixel oracle found one concrete defect in the old scroll schedule: its
13-to-15 vertical-scroll transition at scanline 207 clipped an ink scanline
from the third compact row. The two BG scroll tables now change at scanline
215, after all three list rows and before the footer. This changes two duration
bytes (24 to 32); table shape, transfer count, NMI and artwork code stay intact.
The complete third-row glyph and footer pixels are tested. No broader video or
loader redesign was needed.

## Build and software validation

Use the Phase 1 toolchain instructions. With the bundled tools in this checkout:

```sh
export PATH="$PWD/.tools/bin:$PWD/.tools/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin:$PATH"
make -C snes art
make -C snes
BSNES_SOURCE="$PWD/../bsnes-plus" tools/fxpakos/tests/run_emulator.sh
tools/fxpakos/build_mk3_firmware.sh --release-mini
```

The regression entry point now compiles and runs both real C sidecar workers,
the Python compiler/firmware tests, the existing browser/artwork/edge suites,
and the new metadata suite in both video regions. It uses the pinned bsnes-plus
accuracy core documented in Phase 1. No network is needed once dependencies
and the pinned official release archive are present.

All regression phases pass on menu/test candidate `0651311`, **17,728 emulator
frames** in total:

| Suite | Frames per region | NTSC maximum NMI clocks | PAL maximum NMI clocks |
|---|---:|---:|---:|
| Browser/navigation/dialogs/launch | 1,176 | 35,218 | 35,214 |
| Dynamic artwork | 2,813 | 40,478 | 40,472 |
| Artwork boundary/warm-start/wrap cases | 584 | 40,478 | 40,472 |
| Metadata/dashboard | 4,291 | 40,476 | 40,472 |

Every NMI finishes inside VBlank. Artwork remains limited to 640 tile bytes
per VBlank; all active VRAM/OAM and non-backdrop CGRAM write counts are zero.
The worst observed NTSC NMI retains 9,990 master clocks within the harness's
37-scanline VBlank budget, preserving the Phase 2 margin.

Both native C workers pass. All **13 Python tests** pass, including real
firmware corruption checks and the STM32 linker regression. The initial
combined command was interrupted by the tool's 30-second execution boundary;
the tester completed its remaining phases individually. One Python discovery
invocation lacked the ARM PATH and skipped three tests; those exact three
were subsequently run with the absolute bundled toolchain path and passed.
The aggregate evidence is `.build/full-cycle/phase3/full-suite.log`, not a
claim that an interrupted shell command itself returned success.

The new suite validates the whole accepted record against compiler output,
correct covers/titles/fields, missing sidecars, 19 malformed publications
(including CRC-valid semantic violations), stale metadata paired with a newer
cover, rapid selection changes, cancellation/retry during open metadata I/O,
nested paths, hidden-extension identity and fallback display, modal/context
windows, and launch to the original CMD_RESET handshake. It checks generation
agreement at visible NMIs, stable foreground stack and 600 idle NMIs, immutable
fallback graphics, bounded artwork DMA, and no active VRAM/OAM/non-backdrop
CGRAM writes. The existing intentional backdrop-color HDMA remains unchanged.

The pixel oracle decodes the original font independently of the new renderer
and generated wide-font data. It checks actual title/year/player/publisher/
developer/genre/chip pixels, clean fallback rows, header/footer persistence,
all seven ink scanlines of the compact list, and modal restoration. Existing
pixel-exact cover checks also run with metadata enabled.

Both MCU variants compile and link with ARM GCC 13.2.Rel1 and version
`1.11.0-fxpak-p3`. Integrity/layout checks pass using the unchanged, pinned
official v1.11.0 mini core. The Phase 3 worker adds 1,082 bytes of static BSS;
the recorded headroom is a static linker check, not a worst-case runtime
stack/heap measurement.

| Image | Bytes | Flash free | Main RAM heap/stack headroom |
|---|---:|---:|---:|
| LPC Mk.III `firmware.im3` | 142,664 | 70,328 | 3,036 |
| STM32 Mk.III `firmware.stm` | 143,652 | 69,340 | 43,160 |

SHA-256:

```text
495cef9f4f90486857e02bb1ad510928420dc7c820b31101ee861e74383dee0c  m3nu.bin
d9c67dea7224fb8101189a0513f3dfdad654968b2f7837fe23cf15ea8dbd9282  firmware.im3
dc193b7cbc0d5cf3df00a3e2837ba61194faea34ab453e6a20617c1ef15bd336  firmware.stm
9ae79c3028391063338d42ae16b19acf48d0d80858939b015ef6481f55cbefe9  fpga_mini.bi3
```

The MCU images were built from `0de61f9`; later Phase 3 commits do not change
their compiled MCU inputs. The menu/test source candidate is `0651311`.
The independent read-only reviewer approved code/test candidate
`0651311a24d326c382ad66d4b5f998a26ddb5884`, including the cumulative Phase 3
implementation from baseline `ed0292b`. The delivery manifest records the
final reviewed commit including documentation/build-version alignment. Earlier Phase 1/2
documents retain their historical build hashes and test evidence.

## Physical hardware checks still required

No physical FXPAK Pro Mk.III validation is claimed. The harness simulates
cartridge/MCU responses and ends at CMD_RESET. It cannot prove physical SD
latency, cold/warm boot, FPGA reconfiguration, actual game boot, saves, or real
controller/video behavior. Check these on the appropriate Mk.III MCU variant,
including rapid browsing on a real SD card, optional-sidecar failures, context
and Favorites/Recent navigation, CRT overscan/readability, and NTSC/PAL output.
Measure runtime stack/heap headroom on LPC hardware as part of that validation.
Phase 1 emulator results are not hardware validation.

## Quartus / Cyclone IV packaging status

Quartus Lite 21.1.1 is installed and runnable. Fresh local synthesis still
needs `cyclone-21.1.1.850.qdz`; the authenticated download flow has not delivered
that device package in this environment. Existing user agreement/sign-in
authorization remains recorded in the handoff. Once the package is available,
install it into the existing Quartus tree, run `tools/fxpakos/build_mk3_mini.sh`,
then `tools/fxpakos/build_mk3_firmware.sh` without `--release-mini`.

The current images are already packageable using the authorized unchanged
official mini core, with provenance and source compatibility checks described
in Phase 2. They do **not** establish fresh synthesis or FPGA timing closure.
No FPGA-facing source changes were needed for metadata. Nothing was pushed,
merged, or flashed. Carousel, screenshots, online scraping, new dashboard
sections, and optional favorite/recent inline badges remain outside this phase.
