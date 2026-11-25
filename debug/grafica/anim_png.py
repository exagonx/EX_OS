# debug/grafica/anim_png.py — fotogrammi PPM -> PNG animato + griglia di sei
# uso: python3 -I anim_png.py <dir dei f*.ppm> <nome>   (dalla cartella grafica)
import os, sys, glob
from PIL import Image, ImageDraw

d, nome = sys.argv[1], sys.argv[2]
qui = os.path.dirname(os.path.abspath(__file__))
fot = [Image.open(f).convert("RGB") for f in sorted(glob.glob(os.path.join(d, "f*.ppm")))]
if not fot:
    sys.exit("nessun fotogramma per " + nome)
ciclica = nome in ("07_respiro", "10_luccichio")
# 40 ms a fotogramma; le non cicliche si fermano sull'ultimo e ripartono
dur = [40] * len(fot)
if not ciclica:
    dur[0] = 500
    dur[-1] = 1200
out = os.path.join(qui, "anim_%s.png" % nome)
fot[0].save(out, save_all=True, append_images=fot[1:], duration=dur, loop=0,
            optimize=False, disposal=0)
# la griglia: sei fotogrammi a meta' misura, con t in millesimi
idx = [round(i * (len(fot) - 1) / 5) for i in range(6)]
w, h = fot[0].size[0] // 2, fot[0].size[1] // 2
g = Image.new("RGB", (w * 3 + 8, h * 2 + 8), (20, 20, 28))
dr = ImageDraw.Draw(g)
for k, i in enumerate(idx):
    x, y = (k % 3) * (w + 4), (k // 3) * (h + 4)
    g.paste(fot[i].resize((w, h), Image.LANCZOS), (x, y))
    t = i * 1000 // (len(fot) - 1) if len(fot) > 1 else 1000
    dr.rectangle([x + 4, y + 4, x + 66, y + 18], fill=(0, 0, 0))
    dr.text((x + 7, y + 5), "t=%d" % t, fill=(255, 255, 255))
g.save(os.path.join(qui, "anim_%s_fotogrammi.png" % nome), optimize=True)
print("anim_%s.png (%d fotogrammi)" % (nome, len(fot)))
