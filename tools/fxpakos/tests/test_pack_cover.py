import binascii
import struct
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from pack_cover import pack

class CoverRecordTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.prefix=Path(self.temp.name)/'cover'
        self.prefix.with_suffix('.4bpp').write_bytes(bytes(range(256))*17+bytes(range(128)))
        self.prefix.with_suffix('.pal').write_bytes(struct.pack('<16H',*range(16)))
        self.prefix.with_suffix('.map').write_bytes(struct.pack('<140H',*range(140)))

    def test_fixed_record_and_checksum(self):
        d=pack(self.prefix)
        self.assertEqual(len(d),4544)
        h=struct.unpack('<4sHHI4BHHHHII',d[:32])
        self.assertEqual(h[:10],(b'FXC1',1,4544,0,80,112,1,0,4512,0))
        self.assertEqual(h[10],binascii.crc_hqx(d[32:],0xffff))
        self.assertEqual(h[11],h[10]^65535)
        self.assertEqual(h[12:],(0xffffffff,0))
        self.assertEqual(d[32:4512],self.prefix.with_suffix('.4bpp').read_bytes())

    def test_incompatible_assets_rejected(self):
        for suffix,contents in [('.4bpp',b'bad'),('.pal',b'bad'),('.map',b'bad'),
                                ('.pal',struct.pack('<16H',1,*range(1,16))),
                                ('.pal',struct.pack('<16H',0,0x8000,*range(2,16)))]:
            with self.subTest(suffix=suffix,contents=contents[:4]):
                path=self.prefix.with_suffix(suffix);original=path.read_bytes();path.write_bytes(contents)
                with self.assertRaises(ValueError):pack(self.prefix)
                path.write_bytes(original)
