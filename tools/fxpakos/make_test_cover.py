#!/usr/bin/env python3
"""Draw original STARFALL demo cover artwork (CC0), with no external assets/fonts."""
from pathlib import Path
import sys
from PIL import Image, ImageDraw

def main():
    im = Image.new('RGB', (80, 112), '#080c20')
    d = ImageDraw.Draw(im)
    d.rectangle((1,1,78,110), outline='#7b70bd')
    d.rectangle((4,4,75,15), fill='#423965')
    # Tiny purpose-built pixel lettering, independent of installed fonts.
    font = {
        'A':['010','101','111','101','101'], 'D':['110','101','101','101','110'],
        'E':['111','100','110','100','111'], 'F':['111','100','110','100','100'],
        'L':['100','100','100','100','111'], 'O':['010','101','101','101','010'],
        'R':['110','101','110','101','101'], 'S':['111','100','111','001','111'],
        'T':['111','010','010','010','010'], 'M':['10001','11011','10101','10001','10001'],
    }
    def text(s,y,scale,color):
        width=sum((len(font[c][0])+1)*scale for c in s)-scale
        x=(80-width)//2
        for c in s:
            for j,row in enumerate(font[c]):
                for i,v in enumerate(row):
                    if v=='1': d.rectangle((x+i*scale,y+j*scale,x+(i+1)*scale-1,y+(j+1)*scale-1), fill=color)
            x+=(len(font[c][0])+1)*scale
    text('STARFALL',7,1,'#e7dcff')
    # Starfield, crescent planet, distant ridges, and an ascending spacecraft.
    for x,y in [(9,24),(24,20),(68,30),(13,47),(60,52),(35,30),(70,64),(7,69),(48,22)]:
        d.point((x,y),fill='#b5a5de')
    d.ellipse((44,27,68,51),fill='#7b70bd')
    d.ellipse((40,24,61,45),fill='#080c20')
    d.line((10,35,20,25),fill='#93d8de',width=1)
    d.point((20,25),fill='#e7dcff')
    d.polygon([(4,82),(16,60),(28,74),(40,53),(55,75),(65,65),(75,82)],fill='#423965')
    d.polygon([(27,75),(40,53),(45,69),(40,65),(36,70)],fill='#7b70bd')
    d.polygon([(4,90),(17,79),(29,86),(48,71),(64,87),(75,79),(75,105),(4,105)],fill='#252441')
    d.polygon([(26,64),(35,54),(40,38),(45,54),(54,64),(43,61),(40,65),(37,61)],fill='#e7dcff')
    d.polygon([(35,54),(40,38),(40,59),(37,61),(26,64)],fill='#b5a5de')
    d.polygon([(38,49),(40,43),(42,49),(42,55),(38,55)],fill='#467ca5')
    d.polygon([(37,65),(40,80),(43,65)],fill='#e79274')
    d.polygon([(39,65),(40,73),(41,65)],fill='#ffdda0')
    d.line((8,96,71,96),fill='#7b70bd')
    text('DEMO',100,1,'#b5a5de')
    dest=Path(sys.argv[1]);dest.parent.mkdir(parents=True,exist_ok=True);im.save(dest)
if __name__=='__main__': main()
