# ppm2png.py — converte una fotografia di qemu_drive in PNG, tutta o a ritaglio.
#
#     python3 tools/ppm2png.py foto.ppm fuori.png [x y larghezza altezza]
#
# ! SERVE A GUARDARE, NON A CONFRONTARE: per dire «identico» si confrontano i
#   byte della PPM. Questo e' per quando bisogna capire COSA c'e' storto.

import sys, zlib, struct
def conv(src, dst, box=None):
    d=open(src,'rb').read(); h=d.index(b'255\n')+4
    W,H=800,600; px=d[h:]
    if box: x0,y0,w,hh=box
    else:   x0,y0,w,hh=0,0,W,H
    raw=bytearray()
    for y in range(y0,y0+hh):
        raw.append(0)
        raw+=px[(y*W+x0)*3:(y*W+x0+w)*3]
    def ch(t,b): return struct.pack(">I",len(b))+t+b+struct.pack(">I",zlib.crc32(t+b)&0xffffffff)
    out=b"\x89PNG\r\n\x1a\n"+ch(b"IHDR",struct.pack(">IIBBBBB",w,hh,8,2,0,0,0))
    out+=ch(b"IDAT",zlib.compress(bytes(raw),6))+ch(b"IEND",b"")
    open(dst,'wb').write(out)
if __name__=="__main__":
    box=None
    if len(sys.argv)>3: box=tuple(int(v) for v in sys.argv[3:7])
    conv(sys.argv[1], sys.argv[2], box)
