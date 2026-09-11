from pathlib import Path
import struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from pack_cover import pack
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
data=pack(Path(sys.argv[2]));(out/'FXPAK Demo.sfc.fxc').write_bytes(data)
for name,offset,value in [('version',4,2),('checksum',100,data[100]^1)]:
    modified=bytearray(data);modified[offset]=value;(out/(name+'.sfc.fxc')).write_bytes(modified)
(out/'short.sfc.fxc').write_bytes(data[:-1])
# Second native cover uses a distinct, green palette; pixel layout is identical.
modified=bytearray(data)
for i in range(16):
    c=struct.unpack_from('<H',data,4512+2*i)[0]
    c=((c&31)<<10)|(((c>>10)&31)<<5)|((c>>5)&31)
    struct.pack_into('<H',modified,4512+2*i,c)
import binascii
crc=binascii.crc_hqx(modified[32:],0xffff);struct.pack_into('<HH',modified,20,crc,crc^65535)
(out/'A very long game filename for horizontal scrolling regression test.sfc.fxc').write_bytes(modified)
