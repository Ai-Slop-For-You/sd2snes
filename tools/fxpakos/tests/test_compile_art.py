import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compile_art as art
from PIL import Image

class ArtworkTests(unittest.TestCase):
    def test_known_planar_tile(self):
        # Ascending palette indices across each row exercises all four planes.
        self.assertEqual(art.encode_tile_4bpp(list(range(8))*8), bytes([0x55,0x33]*8+[0x0f,0]*8))

    def test_opaque_quantization_reserves_zero(self):
        im=Image.new('RGB',(80,112))
        im.putdata([(x*3%256,y*2%256,(x+y)%256) for y in range(112) for x in range(80)])
        indexed=art.quantize_image(im,15,False)
        self.assertTrue(all(1<=p<=15 for p in indexed.tobytes()))
        tiles=art.encode_tiles(indexed)
        self.assertEqual(len(tiles),4480)
        # Independent decoder, compared against every source pixel.
        decoded=bytearray(80*112)
        for t in range(140):
            for y in range(8):
                for x in range(8):
                    b=t*32+y*2
                    v=sum(((tiles[b+(p//2)*16+p%2]>>(7-x))&1)<<p for p in range(4))
                    decoded[(t//10*8+y)*80+(t%10*8+x)]=v
        self.assertEqual(bytes(decoded),indexed.tobytes())
        self.assertEqual(art.encode_palette(indexed)[:2],b'\0\0')

    def test_color_order(self):
        self.assertEqual(art.snes_color_word((255,0,0)),0x001f)
        self.assertEqual(art.snes_color_word((0,255,0)),0x03e0)
        self.assertEqual(art.snes_color_word((0,0,255)),0x7c00)

    def test_tilemap_and_limits(self):
        self.assertEqual(art.encode_tilemap(8,8,0x123,5,True),bytes([0x23,0x35]))
        for args in [(80,112,900,0,False),(8,8,0,8,False)]:
            with self.assertRaises(ValueError): art.encode_tilemap(*args)
        for w,h in [(0,112),(80,-8),(79,112)]:
            with self.assertRaises(ValueError): art.validate_dimensions(w,h)
        with self.assertRaises(ValueError): art.quantize_image(Image.new('RGB',(8,8)),16,False)

if __name__=='__main__': unittest.main()
