#!/usr/bin/env python3
"""Check actual dashboard pixels against glyphs decoded from the original font.

No production rendering helpers or generated wide-font bytes are used to build
expected glyphs. Existing dynamic-cover verification remains independent.
"""
from pathlib import Path
import sys
from PIL import Image, ImageChops

out, rom_path, symbols_path = map(Path, sys.argv[1:])
symbols = {line.split()[1]: int(line.split()[0], 16)
           for line in symbols_path.read_text().splitlines() if len(line.split()) == 2}
rom = rom_path.read_bytes()
font_offset = symbols['font'] & 65535
font = rom[font_offset:font_offset + 2048]


def glyph_mask(text):
    expected = Image.new('L', (len(text) * 16, 8))
    for column, char in enumerate(text):
        data = font[ord(char) * 16:ord(char) * 16 + 16]
        for y in range(8):
            for x in range(8):
                value = 255 if (data[y * 2] | data[y * 2 + 1]) & (128 >> x) else 0
                expected.putpixel((column * 16 + x * 2, y), value)
                expected.putpixel((column * 16 + x * 2 + 1, y), value)
    return expected


def foreground_mask(screen):
    # Font colors are white/shades or lavender; navy panel is below this level.
    mask = Image.new('L', screen.size)
    mask.putdata([255 if min(screen.getpixel((x, y))) > 40 else 0
                  for y in range(screen.height) for x in range(screen.width)])
    return mask


def exact_text(screen, text, x, y, context):
    expected = glyph_mask(text)
    actual = foreground_mask(screen.crop((x, y, x + expected.width, y + 8)))
    assert ImageChops.difference(expected, actual).getbbox() is None, context


def line(screen, row, text, context):
    text = text[:16] + '...' if len(text) > 19 else text
    exact_text(screen, text.ljust(19), 200, row * 8 - 14, context)


def panel(screen, title, year='', players='', publisher='', developer='', genre='', chips=''):
    title = title[:54] + '...' if len(title) > 57 else title
    for index in range(3):
        line(screen, 11 + index, title[index * 19:(index + 1) * 19], 'title line')
    if not year and not publisher and not developer and not genre:
        for row in range(14, 23):
            line(screen, row, 'ROM LIBRARY' if row == 16 else '', 'clean filename fallback')
        return
    summary = year.ljust(6) + players if players else year
    line(screen, 14, summary, 'year and players')
    for row, label, value in [(15, 'PUBLISHER', publisher), (17, 'DEVELOPER', developer), (19, 'GENRE', genre)]:
        line(screen, row, label if value else '', label + ' label')
        line(screen, row + 1, value, label + ' value')
    line(screen, 21, chips, 'enhancement chip labels')
    line(screen, 22, '', 'clean chip overflow row')


screens = {name: Image.open(out / f'{name}.ppm').convert('RGB') for name in
           ('boot', 'first', 'second', 'missing', 'bad-crc', 'bad-version',
            'stale-with-cover', 'rapid', 'restored', 'nested', 'hidden-valid', 'hidden-missing')}
first = dict(title='STARFALL', year='1995', players='1-2 PLAYERS', publisher='LUMEN WORKS',
             developer='NIGHT SKY', genre='Action', chips='SUPERFX MSU1')
second = dict(title='GROVE', year='1994', players='1-1 PLAYERS', publisher='ORCHARD',
              developer='LEAF', genre='Adventure', chips='SA1')
for name in ('first', 'restored', 'hidden-valid'):
    panel(screens[name], **first)
for name in ('second', 'rapid'):
    panel(screens[name], **second)
panel(screens['nested'], title='IN THE GARDEN', year='1993', publisher='LUMEN WORKS',
      developer='NIGHT SKY', genre='Puzzle')
for name, title in [('boot', 'Homebrew/'), ('missing', 'Test Game 0.sfc'),
                    ('bad-crc', 'Test Game 1.sfc'), ('bad-version', 'Test Game 2.sfc'),
                    ('hidden-missing', 'Test Game 0'),
                    ('stale-with-cover', 'A very long game filename for horizontal scrolling regression test.sfc'[:63])]:
    panel(screens[name], title=title)

base_vram = (out / 'boot.ppm.vram').read_bytes()
exact_text(screens['boot'], 'FXPAK OS', 32, 8, 'brand glyphs')
exact_text(screens['boot'], 'SUPER NINTENDO COLLECTION', 32, 24, 'collection heading glyphs')
exact_text(screens['boot'], 'LIBRARY   FAVORITES   RECENT', 32, 40, 'navigation glyphs')
header = screens['boot'].crop((0, 0, 512, 56))
footer = screens['boot'].crop((0, 216, 512, 224))
for name, screen in screens.items():
    assert screen.size == (512, 224), name
    exact_text(screen, 'A PLAY  B BACK  X MENU  Y MORE', 24, 216, name + ': readable footer')
    assert ImageChops.difference(screen.crop((0, 0, 512, 56)), header).getbbox() is None, name + ': header stable'
    assert ImageChops.difference(screen.crop((0, 216, 512, 224)), footer).getbbox() is None, name + ': footer stable'
    vram = (out / f'{name}.ppm.vram').read_bytes()
    assert vram[:0xa000] == base_vram[:0xa000], name + ': font graphics immutable'
    # All three compact rows have rendered entries; old virtual list rows in
    # the cover's text-map area remain clear instead of overwriting the panel.
    for row in (25, 26, 27):
        assert any(vram[0xb000 + row * 64:0xb000 + row * 64 + 64]), name + ': compact list row'
    for row in range(9, 25):
        for base in (0xa000, 0xb000):
            assert not any(vram[base + row * 64:base + row * 64 + 24]), name + ': no old list over cover'
    screen.save(out / f'{name}.png')
    screen.resize((256, 224), Image.Resampling.BOX).resize((768, 672), Image.Resampling.NEAREST).save(out / f'{name}-crt.png')


# Legacy compact-list glyphs occupy eight raw pixels per character. Verify the
# complete seven ink rows independently; the eighth blank row borders the
# existing selection-bar HDMA and is intentionally not a font pixel.
def list_text(screen, row, text):
    for column, char in enumerate(text):
        for y in range(7):
            for x in range(8):
                expected = bool(font[ord(char) * 16 + y * 2] & (128 >> x))
                actual = max(screen.getpixel((16 + column * 8 + x, row * 8 - 14 + y))) > 180
                assert expected == actual, 'compact list filename pixels'

list_text(screens['first'], 25, 'Homebrew/')
list_text(screens['first'], 26, 'FXPAK Demo.sfc')
list_text(screens['first'], 27, 'A very long game filename')
list_text(screens['second'], 25, 'FXPAK Demo.sfc')
list_text(screens['second'], 27, 'Test Game 0.sfc')
list_text(screens['missing'], 26, 'Test Game 0.sfc')
list_text(screens['nested'], 26, 'Inside Folder.sfc')
list_text(screens['hidden-missing'], 26, 'Test Game 0')

# The cover remains pixel-identical to the independently decoded first/second
# or fallback image even when its associated metadata is rejected or absent.
for name, reference in [('stale-with-cover', 'second'), ('rapid', 'second'),
                        ('restored', 'first'), ('hidden-valid', 'first'),
                        ('nested', 'missing'), ('hidden-missing', 'missing')]:
    assert ImageChops.difference(screens[name].crop((16, 72, 176, 184)),
                                screens[reference].crop((16, 72, 176, 184))).getbbox() is None, name + ': cover/metadata isolation'

# Different data must change the actual panel while modal dismissal restores it.
area = (200, 74, 504, 170)
assert ImageChops.difference(screens['first'].crop(area), screens['second'].crop(area)).getbbox()
assert ImageChops.difference(screens['first'].crop(area), screens['restored'].crop(area)).getbbox() is None
for index in range(4):
    modal = Image.open(out / f'modal-{index}.ppm').convert('RGB')
    assert ImageChops.difference(modal, screens['first']).getbbox(), 'modal visibly rendered'
    assert ImageChops.difference(modal.crop((16, 72, 176, 184)), screens['first'].crop((16, 72, 176, 184))).getbbox(), 'modal hides cover'
print('PASS metadata pixels: decoded original-font title/year/players/publisher/developer/genre/chips, clean fallback, header/footer, three list rows, modal restore')
