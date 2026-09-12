"""ARM layout regression plus optional checks of locally built release-mini images.

Run with arm-none-eabi tools on PATH:
  python3 -m unittest discover -s tools/fxpakos/tests -p test_mk3_firmware.py -v
Real-image tests require both build_mk3_firmware.sh --release-mini outputs.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[3]
VERIFY = ROOT / 'tools/fxpakos/verify_mk3_firmware.py'
VERSION = '1.11.0-fxpak-p2'
TOOLS = ('gcc', 'objcopy', 'nm', 'objdump', 'readelf')
HAVE_ARM = all(shutil.which('arm-none-eabi-' + tool) for tool in TOOLS)


def arm(tool, *args):
    return subprocess.check_output(['arm-none-eabi-' + tool, *map(str, args)], text=True)


def symbols(elf):
    return {parts[-1]: int(parts[0], 16)
            for line in arm('nm', elf).splitlines()
            if len(parts := line.split()) in (3, 4)}


class VerifyMixin:
    def verify(self, fw, elf, mini, board, message=None, version=VERSION):
        result = subprocess.run([sys.executable, str(VERIFY), str(fw), str(elf), str(mini),
                                 '--version', version, '--board', board],
                                capture_output=True, text=True)
        if message is None:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(message, result.stderr)
            self.assertNotIn('Traceback', result.stderr)


@unittest.skipUnless(HAVE_ARM, 'arm-none-eabi toolchain required on PATH')
class Stm32LayoutTests(VerifyMixin, unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.mini = self.directory / 'mini.bin'
        self.mini.write_bytes(bytes(range(32)))
        source = self.directory / 'fixture.S'
        source.write_text('''
.syntax unified
.thumb
.section .fwhdr,"a"
.space 512, 255
.section .rodata,"a"
.global cfgware
.type cfgware,%object
cfgware:
.byte ''' + ','.join(map(str, range(32))) + '''
.size cfgware, .-cfgware
.section .text
.global main
.thumb_func
main: b main
.section .data,"aw"
.global initialized
initialized: .word 0x12345678
.section .bss,"aw",%nobits
.global ordinary_bss
ordinary_bss: .space 16
.section .ahbram,"aw",%progbits
.global ptrcache
ptrcache: .space 32
.section .ahbram.extra,"aw",%progbits
.global msu_cltbl, pcm_cltbl
msu_cltbl: .space 16
pcm_cltbl: .space 16
''')
        for src, name in [(source, 'fixture.o'),
                          (ROOT / 'src/stm32f4xx/startup.S', 'startup.o')]:
            arm('gcc', '-mcpu=cortex-m4', '-mthumb', '-c', src,
                '-o', self.directory / name)

    def build(self, script):
        elf, fw = self.directory / 'fixture.elf', self.directory / 'fixture.bin'
        arm('gcc', '-mcpu=cortex-m4', '-mthumb', '-nostdlib', '-Wl,--build-id=none',
            '-T', script, self.directory / 'fixture.o', self.directory / 'startup.o',
            '-o', elf)
        arm('objcopy', '--gap-fill', '0xff', '-O', 'binary', elf, fw)
        body = fw.read_bytes()[512:]
        crc = zlib.crc32(body)
        header = self.directory / 'header.bin'
        header.write_bytes(struct.pack('<4s4I', b'STM3', zlib.crc32(VERSION.encode()),
                                       len(body), crc, crc ^ 0xffffffff) + b'\xff' * 492)
        arm('objcopy', '--update-section', f'.fwhdr={header}', elf)
        arm('objcopy', '--gap-fill', '0xff', '-O', 'binary', elf, fw)
        return elf, fw

    def test_buffers_are_in_startup_zero_range_and_not_flash_payload(self):
        elf, fw = self.build(ROOT / 'src/stm32f401.ld')
        self.verify(fw, elf, self.mini, 'mk3-stm32')
        sym = symbols(elf)
        for name, size in [('ordinary_bss', 16), ('ptrcache', 32),
                           ('msu_cltbl', 16), ('pcm_cltbl', 16)]:
            self.assertLessEqual(sym['__bss_start__'], sym[name])
            self.assertLessEqual(sym[name] + size, sym['__bss_end__'])
        for boundary in ('__data_start', '__data_end', '__bss_start__', '__bss_end__'):
            self.assertEqual(sym[boundary] % 4, 0)
        self.assertEqual(sym['__data_end'] - sym['__data_start'], 4)
        self.assertLessEqual(sym['__data_end'], sym['__bss_start__'])
        self.assertLessEqual(sym['__bss_end__'], sym['__heap_start'])
        self.assertLess(sym['__heap_start'], sym['__stack'])
        self.assertGreaterEqual(sym['__data_start'], 0x20000070)
        self.assertEqual(sym['__data_load_end'], 0x0800c000 + fw.stat().st_size)
        self.assertEqual(fw.read_bytes()[-4:], struct.pack('<I', 0x12345678))
        sections = arm('readelf', '-SW', elf)
        self.assertRegex(sections, r'\.bss\s+NOBITS')
        self.assertNotIn('.ahbram', sections)
        # Program headers must reserve RAM without adding BSS bytes to flash.
        loads = [line.split() for line in arm('readelf', '-lW', elf).splitlines()
                 if line.strip().startswith('LOAD ')]
        self.assertTrue(loads)
        self.assertTrue(any(int(p[5], 16) > int(p[4], 16) for p in loads))
        for p in loads:
            physical, filesz = int(p[3], 16), int(p[4], 16)
            if filesz:
                self.assertLessEqual(physical + filesz, sym['__data_load_end'])

    def test_old_orphan_load_is_rejected_by_strict_flash_end_check(self):
        script = (ROOT / 'src/stm32f401.ld').read_text()
        script = script.replace('.bss (NOLOAD) :', '.bss :')
        script = script.replace('    *(.ahbram)\n', '').replace('    *(.ahbram.*)\n', '')
        old = self.directory / 'old.ld'
        old.write_text(script)
        elf, fw = self.build(old)
        self.assertRegex(arm('readelf', '-SW', elf), r'\.ahbram\s+PROGBITS')
        self.assertGreater(0x0800c000 + fw.stat().st_size, symbols(elf)['__data_load_end'])
        self.verify(fw, elf, self.mini, 'mk3-stm32', 'ELF flash end differs from firmware')


@unittest.skipUnless(HAVE_ARM, 'arm-none-eabi toolchain required on PATH')
class RealFirmwareTests(VerifyMixin, unittest.TestCase):
    def test_real_images_and_corruptions(self):
        mini = ROOT / '.build/upstream/fpga_mini.bi3'
        images = {board: (ROOT / f'src/obj-{board}/firmware.{extension}',
                          ROOT / f'src/obj-{board}/sd2snes.elf', header)
                  for board, extension, header in [('mk3', 'im3', 256),
                                                    ('mk3-stm32', 'stm', 512)]}
        if not mini.exists() or not all(fw.exists() and elf.exists()
                                       for fw, elf, _ in images.values()):
            self.skipTest('Build both --release-mini firmware images first')
        with tempfile.TemporaryDirectory() as temporary:
            modified = Path(temporary) / 'modified.bin'
            wrong_mini = Path(temporary) / 'wrong-mini.bin'
            wrong_mini.write_bytes(bytes([mini.read_bytes()[0] ^ 1]) + mini.read_bytes()[1:])
            for board, (fw, elf, header) in images.items():
                with self.subTest(board=board):
                    self.verify(fw, elf, mini, board)
                    original = fw.read_bytes()
                    cases = [(0, 'Wrong board signature'), (4, 'Wrong firmware version'),
                             (8, 'Invalid firmware size'), (12, 'Firmware CRC mismatch'),
                             (16, 'Firmware CRC mismatch'), (20, 'Invalid header padding'),
                             (header + 8, 'Firmware CRC mismatch')]
                    for offset, message in cases:
                        with self.subTest(offset=offset):
                            changed = bytearray(original)
                            changed[offset] ^= 1
                            modified.write_bytes(changed)
                            self.verify(modified, elf, mini, board, message)
                    for length in (0, 19, header - 1, header, header + 7, len(original) - 1):
                        with self.subTest(length=length):
                            modified.write_bytes(original[:length])
                            self.verify(modified, elf, mini, board,
                                        'Truncated firmware' if length < header + 8
                                        else 'Invalid firmware size')
                    # A self-consistent checksum must still match the linked image.
                    changed = bytearray(original)
                    changed[header + 8] ^= 1
                    crc = zlib.crc32(changed[header:])
                    struct.pack_into('<II', changed, 12, crc, crc ^ 0xffffffff)
                    modified.write_bytes(changed)
                    self.verify(modified, elf, mini, board, 'Firmware differs from linked ELF')
                    self.verify(fw, elf, mini, board, 'Wrong firmware version', version='wrong')
                    other = 'mk3' if board == 'mk3-stm32' else 'mk3-stm32'
                    self.verify(fw, images[other][1], mini, board, 'Firmware differs from linked ELF')
                    self.verify(fw, elf, wrong_mini, board, 'Embedded bitstream differs')
                    wrong_mini.write_bytes(mini.read_bytes()[:-1])
                    self.verify(fw, elf, wrong_mini, board, 'Wrong embedded bitstream size')
                    wrong_mini.write_bytes(bytes([mini.read_bytes()[0] ^ 1]) + mini.read_bytes()[1:])


if __name__ == '__main__':
    unittest.main()
