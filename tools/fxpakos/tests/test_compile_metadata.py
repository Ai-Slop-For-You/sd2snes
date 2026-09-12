import binascii
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compile_metadata import CHIPS, compile_metadata, unique_object


class MetadataTests(unittest.TestCase):
    def test_canonical_record(self):
        source = dict(title='STARFALL', publisher='Example', developer='Team', genre='Action',
                      year=1995, min_players=1, max_players=2, chips=list(CHIPS), msu1=True)
        record = compile_metadata(source)
        self.assertEqual(len(record), 256)
        self.assertEqual(struct.unpack_from('<4sHHIHH', record), (b'FXM1', 1, 256, 0, 224, 0))
        crc, inverse, generation_inverse = struct.unpack_from('<HHI', record, 16)
        self.assertEqual(crc, binascii.crc_hqx(record[32:], 0xffff))
        self.assertEqual(crc ^ inverse, 65535)
        self.assertEqual(generation_inverse, 0xffffffff)
        self.assertEqual(record[24:32], bytes(8))
        self.assertEqual(record[32:96], b'STARFALL' + bytes(56))
        self.assertEqual(struct.unpack_from('<HBBHH', record, 184), (1995, 1, 2, 1023, 1))
        self.assertEqual(record[192:], bytes(64))
        self.assertEqual(record, compile_metadata(dict(reversed(list(source.items())))))
        source['chips'].reverse()
        self.assertEqual(record, compile_metadata(source))

    def test_defaults_and_boundaries(self):
        self.assertEqual(compile_metadata({'title': 'A'})[96:], bytes(160))
        for year in (0, 1900, 2199):
            compile_metadata(dict(title='~' * 63, publisher=' ' * 31, developer='D' * 31,
                                  genre='G' * 23, year=year, min_players=1, max_players=8))
        for chip, mask in CHIPS.items():
            self.assertEqual(struct.unpack_from('<H', compile_metadata(dict(title='A', chips=[chip])), 188)[0], mask)

    def test_rejections(self):
        for source in (None, [], 'title', {}, {'title': ''}):
            with self.subTest(source=source), self.assertRaises(ValueError):
                compile_metadata(source)
        cases = dict(title=[None, 2, 'é', '\n', '\x00', '\x7f', 'x' * 64],
                     publisher=['x' * 32, False], developer=['x' * 32], genre=['x' * 24],
                     year=[True, 1999.0, '1999', 1899, 2200, -1],
                     min_players=[True, -1, 9, 1], max_players=[True, -1, 9, 1],
                     chips=['SA1', ['unknown'], ['SA1', 'SA1'], [None]],
                     msu1=[1, 'true', None], unknown=[1])
        for key, values in cases.items():
            for value in values:
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    compile_metadata({'title': 'Valid', key: value})
        with self.assertRaises(ValueError):
            compile_metadata(dict(title='Valid', min_players=3, max_players=2))
        with self.assertRaises(ValueError):
            json.loads('{"title":"A","title":"B"}', object_pairs_hook=unique_object)

    def test_cli_and_no_output_on_invalid_input(self):
        script = Path(__file__).resolve().parents[1] / 'compile_metadata.py'
        with tempfile.TemporaryDirectory() as temporary:
            source, output = Path(temporary) / 'meta.json', Path(temporary) / 'game.sfc.fxm'
            source.write_text('{"title":"A"}')
            subprocess.run([sys.executable, script, source, output], check=True)
            self.assertEqual(output.read_bytes(), compile_metadata({'title': 'A'}))
            source.write_text('{"title":"A","extra":1}')
            result = subprocess.run([sys.executable, script, source, output], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn('Traceback', result.stderr)
            self.assertEqual(output.read_bytes(), compile_metadata({'title': 'A'}))


if __name__ == '__main__':
    unittest.main()
