# FXPAK OS Phase 2 — dynamic selected-ROM artwork

Target: **FXPAK Pro Mk.III**. The menu/MCU implementation is exercised end to end
in the NTSC/PAL harness. Flashable firmware and physical hardware validation
remain dependent on the Quartus mini-core build described below.

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

## Mk.III Quartus build setup and remaining step

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

The real `make mk3` now reaches `quartus_map`, which stops with error 20004
because Cyclone IV device support is not installed yet. The separate
`cyclone-21.1.1.850.qdz` download is pending approval of its vendor agreement.
No substitute or dummy bitstream was used. All Mk.III MCU translation units
except `fpga.c` (which embeds the missing bitstream) compile successfully with
ARM GCC 13.2.Rel1 and the original strict flags; host `genhdr`, `lpcchksum`,
`bin2c`, `rle` and `derle` also build successfully.

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
make -C utils bin2c
make -C src/utils
make -C src CONFIG=config-mk3 all
# Expected firmware output: src/obj-mk3/firmware.im3
```

Review the real timing/build reports and firmware memory-size output before
hardware testing. Additional host compatibility issues may only become visible
once the vendor tool runs. The `.bi3` generation and full firmware link have
**not** run successfully here. Mk.II/Xilinx synthesis is not required for this
target and was not pursued.

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
