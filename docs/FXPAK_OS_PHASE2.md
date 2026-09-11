# FXPAK OS Phase 2 (implementation in progress)

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

## Validation so far

The actual C worker compiles with strict host warnings and passes file-backed
success/missing/version/CRC/size tests, request replacement, command cancellation,
SRAM boundary canaries, and <=512-byte I/O checks. SNES integration and emulator
regression evidence follow in subsequent commits.
