#!/usr/bin/env python3
"""Verify the emulator framebuffer and VRAM/CGRAM against the compiled asset."""
import sys
from pathlib import Path
from PIL import Image, ImageChops
out, art = map(Path,sys.argv[1:])
expected=Image.open(art.with_suffix('.preview.png')).convert('RGB').resize((160,112),Image.Resampling.NEAREST)
tiles=art.with_suffix('.4bpp').read_bytes()
palette=art.with_suffix('.pal').read_bytes()
visible=['boot','folder','parent','down','closed','page','marquee','stable']
hidden=['menu','context','favorites','recent']
base=(out/'boot.ppm.vram').read_bytes()
basepal=(out/'boot.ppm.cgram').read_bytes()
for name in visible+hidden:
    screen=Image.open(out/f'{name}.ppm').convert('RGB')
    crop=screen.crop((16,72,176,184))
    assert (ImageChops.difference(crop,expected).getbbox() is None)==(name in visible), f'{name}: cover pixels/visibility'
    vram=(out/f'{name}.ppm.vram').read_bytes()
    cgram=(out/f'{name}.ppm.cgram').read_bytes()
    oam=(out/f'{name}.ppm.oam').read_bytes()
    assert cgram[256:288]==palette, f'{name}: cover palette'
    assert cgram[2:]==basepal[2:], f'{name}: CGRAM changed outside HDMA backdrop'
    assert vram[:0xa000]==base[:0xa000],f'{name}: font/graphics corruption'
    assert vram[0xc000:]==base[0xc000:],f'{name}: sprite VRAM changed'
    for y in range(14):
        assert vram[0xc800+y*512:0xc800+y*512+320]==tiles[y*320:(y+1)*320],f'{name}: tile row {y}'
    assert all(oam[128+i*4+1]==(72+(i//5)*16 if name in visible else 240) for i in range(35)), f'{name}: OAM visibility'
    # Full-resolution raw screenshot plus 256x224 display-aspect preview.
    screen.save(out/f'{name}.png')
    screen.resize((256,224),Image.Resampling.BOX).resize((768,672),Image.Resampling.NEAREST).save(out/f'{name}-crt.png')
print(f'PASS: {len(visible)} pixel-exact cover frames, {len(hidden)} modal frames, stable font/OBJ VRAM and CGRAM')
