#!/usr/bin/env python3
"""Decode FXC1 independently and compare the actual SNES framebuffer pixelwise."""
import struct, sys
from pathlib import Path
from PIL import Image, ImageChops
out, sd, fallback = map(Path, sys.argv[1:])

def decode(path):
    data=path.read_bytes()
    colors=[tuple(((c>>s)&31)*255//31 for s in (0,5,10)) for c in struct.unpack_from('<16H',data,4512)]
    im=Image.new('RGB',(80,112))
    for t in range(140):
        for y in range(8):
            for x in range(8):
                b=32+t*32+y*2
                index=sum(((data[b+(p//2)*16+p%2]>>(7-x))&1)<<p for p in range(4))
                assert index!=0,'opaque fixture contains transparent pixel'
                im.putpixel((t%10*8+x,t//10*8+y),colors[index])
    return im

first=decode(sd/'FXPAK Demo.sfc.fxc')
second=decode(sd/'A very long game filename for horizontal scrolling regression test.sfc.fxc')
assert ImageChops.difference(first,second).getbbox() is not None
base=(out/'boot.ppm.vram').read_bytes()
basepal=(out/'boot.ppm.cgram').read_bytes()
for name,expected in [('first',first),('second',second),('missing',Image.open(fallback.with_suffix('.preview.png')).convert('RGB'))]:
    screen=Image.open(out/f'{name}.ppm').convert('RGB')
    assert ImageChops.difference(screen.crop((16,72,176,184)),expected.resize((160,112),Image.Resampling.NEAREST)).getbbox() is None,name
    vram=(out/f'{name}.ppm.vram').read_bytes()
    pal=(out/f'{name}.ppm.cgram').read_bytes()
    assert vram[:0xa000]==base[:0xa000],name+': font changed'
    assert vram[0xc000:0xe400]==base[0xc000:0xe400],name+': fallback changed'
    assert pal[2:0x120]==basepal[2:0x120] and pal[0x140:]==basepal[0x140:],name+': unrelated palette changed'
    screen.save(out/f'{name}.png')
    screen.resize((256,224),Image.Resampling.BOX).resize((768,672),Image.Resampling.NEAREST).save(out/f'{name}-crt.png')
print('PASS: pixel-exact dynamic cover changes and missing fallback; font, fallback and unrelated palettes preserved')
