"""Deterministic metadata fixtures built with the production JSON compiler."""
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compile_metadata import compile_metadata

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
fixtures = {
    'FXPAK Demo.sfc': dict(title='STARFALL', publisher='LUMEN WORKS', developer='NIGHT SKY',
                         genre='Action', year=1995, min_players=1, max_players=2,
                         chips=['SUPERFX'], msu1=True),
    'A very long game filename for horizontal scrolling regression test.sfc':
        dict(title='GROVE', publisher='ORCHARD', developer='LEAF', genre='Adventure',
             year=1994, min_players=1, max_players=1, chips=['SA1']),
    'Homebrew/Inside Folder.sfc': dict(title='IN THE GARDEN', publisher='LUMEN WORKS',
                                     developer='NIGHT SKY', genre='Puzzle', year=1993),
}
for name, metadata in fixtures.items():
    target = out / (name + '.fxm')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(compile_metadata(metadata))
(out / 'metadata-fixtures.json').write_text(json.dumps(fixtures, indent=2, sort_keys=True) + '\n')
first = compile_metadata(fixtures['FXPAK Demo.sfc'])
for name, offset in [('Test Game 1.sfc', 100), ('Test Game 2.sfc', 4)]:
    malformed = bytearray(first)
    malformed[offset] ^= 1
    (out / (name + '.fxm')).write_bytes(malformed)
# Test Game 0 deliberately has neither a cover nor metadata.
(out / 'Test Game 0.sfc.fxm').unlink(missing_ok=True)
