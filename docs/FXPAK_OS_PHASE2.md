# FXPAK OS Phase 2 — dynamic selected-ROM artwork

Target: **FXPAK Pro Mk.III**. The menu/MCU implementation is exercised end to end
in the NTSC/PAL harness. Both Mk.III MCU firmware images now build and pass
integrity/layout checks using the unchanged mini core extracted from the
pinned official v1.11.0 release. Fresh local FPGA synthesis and physical
hardware validation remain outstanding; see the provenance and build paths below.

## Memory audit and protocol v1

The optional MCU service uses a mailbox at **$FF5000-$FF51FF** and one fixed-size
record at **$FF6000-$FF71BF**. These are physical cartridge SRAM addresses and
SNES addresses in menu mapper 7 (`verilog/sd2snes_base/address.v`). The normal
ROM loader/command registers at $002A00/$002A02 and their parameters are untouched.

Other menu allocations: configuration $FF0100, status $FF1100, system information
$FF1200-$FF1407, ten 256-byte recent entries $FF1420-$FF1E1F, WRAM backup
$FF2000-$FF3FFF, ten favorites $FF4000-$FF49FF, scratchpad $FFFF00 and directory
ID $FFFFF0. SPC occupies $FD0000/$FE0000; save-state handler starts $FE1000.
Menu code/theme/directory pointer tables start $C00000/$C10000/$C20000.
Directory name data starts $C30000 and is now explicitly bounded below
$CFFFFE (the next reserved cheat allocation). Previously enough long names could
overrun later memory. Existing 16,000-entry limit remains; a directory that fills
its name-data capacity is terminated safely instead of corrupting other regions.
Game ROM loading may reuse memory after menu exit; the art service is cancelled
before handling any existing command and runs only in the menu loop.

Mailbox offsets (little endian):

| Offset | Owner | Meaning |
|---|---|---|
| 0..3 | MCU | `FXA1`, capability published last during menu initialization |
| 4..5 | MCU | protocol version 1 |
| 6..7 | MCU | record size 4544 |
| 8 | MCU | response: idle 0, busy 1, ready 2, missing 3, invalid 4 |
| 12..15 | MCU | response generation |
| 16 | SNES | request committed marker $A5, zero while replacing/cancelling |
| 17 | SNES | request kind: ROM 1, cancel/non-ROM 0 |
| 18..19 | SNES | full path byte length, excluding NUL |
| 20..23 | SNES | monotonically increasing nonzero generation |
| 24..27 | SNES | bitwise inverse generation (torn-write detection) |
| 32..287 | SNES | absolute, NUL-terminated ROM path, at most 255 bytes |

MCU takes a stable request snapshot, reads `<absolute ROM path>.fxc` using its
own FIL object and buffers, and publishes response status last. Each poll does
at most one file open or one <=512-byte read. It checks for existing commands,
reset, cancellation and replacement before/after I/O. No menu code waits for
artwork and the service never borrows the global file handle or loader filename.
A FatFs call itself is synchronous; command service can be delayed by at most
one in-progress filesystem operation and its underlying SD-driver timeout.
There is no wait for an entire artwork file or artwork acknowledgement.

## FXC1 cover record

Fixed length **4544 bytes**. All integers little endian; tilemap is implicit.

| Offset | Size | Value |
|---|---|---|
| 0 | 4 | `FXC1` |
| 4 | 2 | version 1 |
| 6 | 2 | total bytes 4544 |
| 8 | 4 | generation (zero on SD, stamped by MCU in staging) |
| 12 | 4 | width 80, height 112, row-major layout 1, flags 0 |
| 16 | 2 | payload bytes 4512 |
| 18 | 2 | reserved zero |
| 20 | 2 | CRC-16/CCITT-FALSE of payload, initial $FFFF, polynomial $1021 |
| 22 | 2 | inverse CRC |
| 24 | 4 | inverse generation |
| 28 | 4 | reserved zero |
| 32 | 4480 | 140 row-major 8x8 SNES 4bpp tiles |
| 4512 | 32 | 16 BGR555 colors; transparent entry zero |

All non-payload fields are either checked against constants or protected by
value/inverse checks and the separately published response generation. No PNG
or JPEG decoding takes place on SNES or MCU.

Generate a sidecar with the existing asset compiler, then:

```sh
python3 tools/fxpakos/pack_cover.py build/Game '/path/to/SD/ROMs/Game.sfc.fxc'
```

## SNES consumer and display transaction

`fxpakart.a65` runs after the stock browser's input update. It resolves the
highlighted descriptor plus CWD into an absolute path (including the extension
when the browser hides it), compares that identity with the previous selection,
and publishes a new generation only when it changes. Non-ROM selections and
paths longer than 255 bytes cancel the request and select the fallback. The
request marker is cleared before replacement, then written last. The 32-bit
counter carries across words; wrap disables optional artwork instead of reusing
an identity. A SNES-only warm restart advances the surviving mailbox generation,
so a retained capability/response from an older firmware session cannot be reused.

With no `FXA1` capability, unsupported protocol, or incompatible record size,
the menu continues in Phase 1 fallback-only mode. There are no new command IDs,
ACK waits, loader parameters, or changes to the existing launch handshake.

The foreground validates the complete header, current response generation, and
CRC. It copies/checksums at most 256 bytes each iteration into private WRAM
**$7F0000-$7F11BF** (18 iterations for a record). It rechecks the publication
before and after each chunk and compares the frozen header with staging at the
end. A publication replaced mid-copy is discarded. A normal MCU command may
cancel/restart the same request; the consumer retries within its original
120-waiting-frame deadline. Invalid assets remain on fallback. An expired
request is revoked and a later response is ignored.

The private WRAM record does not overlap menu variables/stack ($7E0000-$7E1FFF),
window backing storage ($7E2000 onward), text buffers ($7EA000/$7EB000), stock
$7F text-buffer addresses around $A000, or the menu's $7EF000 launch routines.
The existing launcher clears/reuses WRAM only after disabling NMI. Save-state
restoration and game WRAM ownership occur outside the menu service lifetime.
The new path reader uses a private direct-page pointer. NMI now explicitly sets
D=0; its existing interrupt prologue/epilogue already saves/restores D.

The Phase 1 fallback is permanent in VRAM bytes **$C800-$E3FF**, CGRAM colors
**$80-$8F**. Validated dynamic graphics upload into inactive VRAM bytes
**$E400-$FFFF**, OBJ tiles **288..511**, with the original 16-tile row stride.
NMI transfers two 320-byte tile rows each frame (640 bytes maximum), for seven
frames. An eighth NMI loads 32 palette bytes into colors **$90-$9F**, then swaps
the cover's OAM source. Both covers use the existing 35 reserved 16x16 sprites,
OAM entries 32..66. The selected source changes only after the whole record is
validated and uploaded. Each transfer checks its generation; a selection change
cancels partial work and returns to the immutable fallback. Modal visibility
still uses the original window-stack/screen-DMA checks.

## Build and use

Build the Phase 1 toolchain/menu as described in `FXPAK_OS_PHASE1.md`. Install a
menu **and matching updated MCU firmware** to enable dynamic artwork; replacing
only `m3nu.bin` on an older MCU firmware keeps the fallback.

For each ROM, compile the source image on the host (defaults are 80x112, 15 opaque
colors plus transparent index zero), then put the packed sidecar beside the ROM:

```sh
python3 tools/fxpakos/compile_art.py source.png .build/art/game
python3 tools/fxpakos/pack_cover.py .build/art/game '/media/SD/ROMs/Game.sfc.fxc'
```

The extension/case/path must match the ROM as stored on the SD filesystem.
Subdirectories and identical basenames in different directories are supported.
No runtime image decoder, artwork database, cache-index rebuild or external
network service is required. `.fxc` files are not ROM entries in the existing
browser's file-type filter. The bundled STARFALL source/assets remain unchanged.

Run the complete regression suite:

```sh
BSNES_SOURCE=/path/to/bsnes-plus tools/fxpakos/tests/run_emulator.sh
# ARM GNU Toolchain 13.2.Rel1 arm-none-eabi-gcc on PATH:
tools/fxpakos/tests/check_mcu_compile.sh
```

The ARM check defaults to Mk.III (`config-mk3`) and compiles all changed
production translation units with the real headers and original `-Wall -Werror` build flags. It
explicitly bypasses only the unrelated generated `cfgware.h` prerequisite;
none of those units includes that header. It does not claim to link firmware.

## Mk.III firmware build and mini-core provenance

A clean ARM GCC 13.2.Rel1 build now produces `firmware.im3` for the LPC1756
Mk.III and `firmware.stm` for the STM32F401 Mk.III, both with version
`1.11.0-fxpak-p2`. The menu remains byte-identical to the 9,106-frame validated
binary. MCU C/assembly and SNES sources are unchanged by these build fixes;
the STM32 linker correction below changes buffer placement and initialization.

The STM32 build initially failed `ELF flash end differs from firmware`.
`stm32f401.ld` did not place the shared `.ahbram` input sections, so the linker
emitted 8,992 bytes as an orphan loadable section after `.data`. Startup copied
only `.data` and cleared only `.bss`; the orphan buffers were outside both
initialization ranges. All three objects (`ptrcache`, `msu_cltbl`, `pcm_cltbl`)
are declared without initial values. The STM32 linker now collects `.ahbram`
and `.ahbram.*` inside explicit `NOLOAD` `.bss`, covering them with the existing
startup clear. LPC's separate AHB RAM layout is unchanged. The strict
`__data_load_end == flash_base + firmware_size` check is retained.

In the corrected STM32 image, BSS spans `$20000420..$2000532B` and includes all
three buffers. Its flash payload ends at `$0802EC68`; BSS adds RAM reservation
but no flash bytes. Host layout tests do not establish successful physical boot.

| Built image | Bytes | Flash bytes free | Shared heap/stack bytes remaining |
|---|---:|---:|---:|
| LPC Mk.III `firmware.im3` | 141,460 | 71,532 | 4,120 |
| STM32 Mk.III `firmware.stm` | 142,440 | 70,552 | 44,244 |

SHA-256 of the available images:

- `firmware.im3`: `25b3f9e337715d78cdc323ab01046c9df0e9598ea839dd62f76eb5917e33f6ba`
- `firmware.stm`: `91904ad396194607b2993296d36bc405f736f91b108a443a03050e3062e37e1b`
- `m3nu.bin`: `b2d8599f27607e00f40faaf6be73f6f2b6d601f1df8e4f26a84624b9e8b2cedd`

The embedded mini core is recovered verbatim from the
[official v1.11.0 firmware archive](https://sd2snes.de/files/sd2snes_firmware_v1.11.0.zip).
It is **not a fresh local synthesis**. The source comparison against upstream
commit `31dca4678ee8acbef8da1d05aeda35061eeccbfd` verifies every mini-core file,
except the Makefile dependency change, with exactly one permitted QSF change:
removal of the nonexistent `data.v` entry. All RTL, PLL, pin assignments and
timing constraints match the release. Other game cores are not extracted or
replaced; some have pre-existing changes beyond v1.11.0.

`extract_release_mini.py` checks the complete archive and original firmware
SHA-256, validates the firmware header/CRC, checks the Thumb call site and
literal pointer used by `fpga_rompgm`, then extracts 54,754 bytes at file offset
`0x14b08`. It verifies the resulting bitstream hash before writing it under
`.build/upstream/`; it never creates a pretend synthesis output in `verilog/`.
The original bytes, including their final padding, are preserved without
recompression.

| Asset | SHA-256 |
|---|---|
| Official release ZIP | `8a56c4a23be13eed51f11e82525f8d812a8fe23567ca2708de507ad51c62fb64` |
| Official `firmware.im3` | `8393cd381d71bc30b363802c718b39b80987171a09a6b83a27d5e000305ba5ab` |
| Extracted mini core | `9ae79c3028391063338d42ae16b19acf48d0d80858939b015ef6481f55cbefe9` |

Reproduce the available firmware build from the repository root:

```sh
export PATH="$PWD/.tools/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin:$PATH"
# If the pinned upstream commit is absent, fetch upstream tag v1.11.0 first.
tools/fxpakos/build_mk3_firmware.sh --release-mini
```

This builds the host packaging utilities, cleans and compiles both Mk.III
firmwares with the original strict warnings, and validates each resulting
image against the ELF, board signature, version, payload size/CRC, header
padding, vectors, flash/RAM bounds, and exact embedded mini bytes. Logs and
hashes are in `.build/mk3-firmware/`. Compiler/verifier failures stop the wrapper;
verifier error text is retained in each board's log. Prior combined success
manifests are removed before compilation and regenerated only after both pass.
The revision log identifies HEAD; check the working-tree status as well when
building locally with uncommitted changes.

With the ARM tools still on PATH, run the focused firmware regression suite:

```sh
python3 -m unittest discover -s tools/fxpakos/tests -p test_mk3_firmware.py -v
```

All three test methods pass, without skips, after the dual-board release-mini
build. A native ARM fixture links the actual STM32 startup assembly and checks
data/BSS boundaries, word alignment, heap placement, NOBITS sections, program
headers and initialized data. Reconstructing the former orphan-section layout
must fail the strict flash-end check. The real-image checks cover both boards
and reject changed signatures, versions, sizes, CRCs/inverses, header padding,
payloads, truncated inputs, a payload with a recomputed CRC, the wrong ELF,
and mismatched mini contents/length. The real-image test skips if its two built
images or release mini are missing; the ARM tests skip without the toolchain.
Treat skips as missing verification, not passing firmware validation.
The main RAM headroom reported by the validator is shared heap/stack space,
not a measured runtime stack bound.

The resulting files are an overlay for an existing working Mk.III SD setup:
replace `sd2snes/m3nu.bin` and the firmware file appropriate to the MCU
(`firmware.im3` or `firmware.stm`). Keep the existing game cores, support files,
configuration, saves and ROMs. No SD card has been modified here.

## Fresh Quartus synthesis (still outstanding)

The FPGA source and build flow are present. `verilog/sd2snes_mini/main.qsf`
targets **Cyclone IV E EP4CE15F17C8** and records Quartus Lite 21.1.1; its PLL IP
is already checked in. No FPGA RTL, pin assignments, timing constraints, mapper,
or command interface has been changed for artwork.

`tools/fxpakos/build_mk3_mini.sh` pins **Quartus Lite 21.1.1 Build 850**, builds the
host RLE utility, invokes only `make mk3`, retains version/build/timing logs, and
checks that decompressing `fpga_mini.bi3` exactly recovers `main.rbf`. It overrides
the machine-specific Windows path in `settings.mk` on the command line. Build
maintenance changes skip the unrelated Xilinx setup probe for Intel-only goals,
track QSF/QPF/QIP/PLL dependencies, require a nonempty timing report, and remove
the mini project's obsolete reference to nonexistent, unused `data.v`.

Quartus Lite 21.1.1 Build 850 is now installed locally in this Codex environment
at `.tools/intelFPGA_lite/21.1`. The official browser download succeeded after
accepting the vendor agreement; the installer matched the pinned SHA-1. Direct
command-line requests to the vendor CDN still returned HTTP 403.

The shell and Analysis & Synthesis executables run on Fedora 44. The latter
requires `libcrypt.so.1`: Fedora's signed `libxcrypt-compat-4.5.2-3.fc44.x86_64`
RPM was downloaded and extracted under `.tools/quartus-deps/root`, without
changing system libraries. Export its `usr/lib64` directory through
`LD_LIBRARY_PATH` before invoking the build wrapper.

The real `make mk3` reaches `quartus_map`, which stops with error 20004
because Cyclone IV device support is not installed. Both download agreements
are approved, and the user completed MyAltera sign-in. The signed-in flow now
opens and dismisses the agreement correctly, but still does not deliver
`cyclone-21.1.1.850.qdz` in this browser; direct CDN requests return HTTP 403.
Supply that pinned package in `.tools/quartus-downloads/` and run the installer
again with Cyclone IV selected to finish the native synthesis environment.
Do not repeat license approval or sign-in requests already completed.

Use the [official Quartus 21.1.1 Linux download page](https://www.altera.com/downloads/fpga-development-tools/quartus-prime-lite-edition-design-software-version-21-1-1-linux)
to obtain these pinned packages together (Questa and other FPGA families are
not needed for the mini core):

- `QuartusLiteSetup-21.1.1.850-linux.run`
- `cyclone-21.1.1.850.qdz`

Their vendor-published SHA-1 values are in
`tools/fxpakos/quartus-21.1.1.sha1`. Verify them with `sha1sum -c` from the download
directory, then run the vendor installer and install Quartus plus Cyclone IV
support. Keep the install outside the tracked source tree, e.g. `.tools/quartus/`.
The installer used here was run with `--mode unattended --accept_eula 1`,
`--installdir "$PWD/.tools/intelFPGA_lite/21.1"`, and
`--disable-components quartus_help,arria_lite,cyclone10lp,cyclonev,max,max10,questa_fse,questa_fe,modelsim_ase,modelsim_ae`.
Only accept the license once authorized to do so. The initial installation did
not include the missing Cyclone IV package.

The exact remaining build steps, from the repository root, after installing
Cyclone IV support are:

```sh
# Adjust to the actual installed Quartus directory; bin/ must be inside it.
export QUARTUS_ROOTDIR="$PWD/.tools/intelFPGA_lite/21.1/quartus"
# Fedora 44 compatibility library, extracted from the signed Fedora RPM:
export LD_LIBRARY_PATH="$PWD/.tools/quartus-deps/root/usr/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
tools/fxpakos/build_mk3_mini.sh
# Produces verilog/sd2snes_mini/fpga_mini.bi3 after passing timing checks.

# Put arm-none-eabi-gcc on PATH (13.2.Rel1 used for the compile checks here).
tools/fxpakos/build_mk3_firmware.sh
# Uses the locally synthesized asset; builds and verifies both Mk.III images.
```

Review the real timing/build reports and firmware memory-size output before
hardware testing. Additional host compatibility issues may only become visible
once full synthesis runs. Fresh `.bi3` generation has **not** succeeded here;
the completed firmware link uses the release asset described above. No new
FPGA timing report is available. Mk.II/Xilinx synthesis was not pursued.

Physical SNES, SD-card latency and actual FPGA reconfiguration are not validated
by the host harness. It executes the production menu binary and production C
artwork worker, but simulates SD/SRAM access and existing cartridge command
acknowledgements. Its ROM-launch assertion reaches `CMD_RESET`; it does not
boot a commercial game or emulate cartridge FPGA reconfiguration.

## Regression findings and evidence

The PAL dynamic test exposed an existing shared-PPU-latch race: HDMA writes to
BG1 scroll at scanline 183 landed between the two Mode 7 multiplier operand
writes during menu layout. A four-entry menu acquired height 2 and its border
loop underflowed. Four byte-sized geometry/index calculations in `menu.a65` and
`ui.a65` now use the CPU 8x8 multiplier with its required eight-cycle delay.
NMI does not touch these registers. The dialog layouts, input flow and command
handling are unchanged; the regression passes with the original two-frame
button presses, without extending their timing.

The regression suite includes:

- The original Phase 1 NTSC/PAL navigation, directory/parent, pagination,
  favorites, recent games, dialogs, marquee, 600-frame stability and ROM launch
  tests, including pixel-exact STARFALL fallback rendering with no art capability.
- Real C worker + real SNES code: full-path requests, separate STARFALL/GROVE
  native sidecars, pixel-exact changes, missing fallback, hidden extensions,
  nested paths, command-interrupted reads and resume of the unchanged selection.
- Rejected version, CRC and file size in the MCU; independent version/CRC checks
  in SNES with MCU validation deliberately bypassed; stale/late publication,
  mid-snapshot corruption/replacement, cancellation during VRAM DMA, rapid
  four-frame selection changes, and correct ROM launch while artwork is pending.
- 255-byte path success, overlong-path cancellation, retained-mailbox warm boot,
  generation carry through 65535, and safe exhaustion at 2^32 without reuse.
- Current-generation assertion at every visible dynamic-cover NMI; at most
  640 artwork tile bytes per NMI; no active-display VRAM/OAM writes and no
  active-display non-backdrop CGRAM writes. The original scanline backdrop HDMA
  remains intentional. NMI must finish inside the same VBlank without reentry.
- Native framebuffer decoding independent of the compiler, immutable fallback
  tiles/palette, unchanged fonts and unrelated palettes, balanced foreground
  stack, SRAM staging canaries and at most 512 bytes per MCU read.

The final run passed **9,106 emulated frames**: 1,156 baseline + 2,813 dynamic
+ 584 edge-case frames in each region, plus six Python tests and the production
C worker tests.

Generated evidence is under `.build/tests/{NTSC,PAL,art-NTSC,art-PAL}`; `run.log`,
`edge.log`, `render.log`, screenshots and raw VRAM/CGRAM/OAM/WRAM snapshots can be
recreated with the test command above. The maximum observed full NMI is
**40,482 NTSC / 40,472 PAL master clocks**, below the approximately 50,468-clock
NTSC VBlank. Tile uploads never exceed 640 bytes per NMI. All changed MCU units
also compiled cleanly with strict warnings for mk2/mk3/mk3-stm32 before Mk.III
was selected as the sole hardware target.

The optional selected-game information area and cover carousel are not added.
