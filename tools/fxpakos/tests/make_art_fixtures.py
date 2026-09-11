from pathlib import Path
import struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from pack_cover import pack
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
data=pack(Path(sys.argv[2]));(out/'FXPAK Demo.sfc.fxc').write_bytes(data)
for name,offset,value in [('version',4,2),('checksum',100,data[100]^1)]:
    modified=bytearray(data);modified[offset]=value;(out/(name+'.sfc.fxc')).write_bytes(modified)
(out/'short.sfc.fxc').write_bytes(data[:-1])
# A second, original geometric forest cover exercises different tiles AND palette.
from PIL import Image, ImageDraw
from compile_art import quantize_image, encode_tiles, encode_palette, encode_tilemap
im=Image.new('RGB',(80,112),'#082c24');draw=ImageDraw.Draw(im)
draw.rectangle((1,1,78,110),outline='#9ce89c')
draw.rectangle((4,4,75,17),fill='#24583c');draw.text((22,6),'GROVE',fill='#e8f0ac')
draw.ellipse((31,23,57,49),fill='#e8f0ac')
for x,y in [(7,71),(23,59),(48,76),(67,60)]:
    draw.rectangle((x-1,y,x+2,103),fill='#5c8850')
    draw.polygon([(x,y-33),(x-12,y+8),(x+12,y+8)],fill='#347c4c')
    draw.polygon([(x,y-22),(x-14,y+20),(x+14,y+20)],fill='#48985c')
draw.polygon([(4,104),(24,87),(35,98),(55,88),(75,101),(75,107),(4,107)],fill='#9ce89c')
indexed=quantize_image(im,15,False);prefix=out/'grove'
prefix.with_suffix('.4bpp').write_bytes(encode_tiles(indexed))
prefix.with_suffix('.pal').write_bytes(encode_palette(indexed))
prefix.with_suffix('.map').write_bytes(encode_tilemap(80,112,0,0,False))
(out/'A very long game filename for horizontal scrolling regression test.sfc.fxc').write_bytes(pack(prefix))
