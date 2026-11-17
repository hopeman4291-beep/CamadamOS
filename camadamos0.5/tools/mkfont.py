from PIL import Image, ImageDraw, ImageFont
import sys
W,H=8,16
f=ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",13)
out=["/* Generated from DejaVu Sans Mono (Bitstream Vera license) */","#include <stdint.h>","const uint8_t font_alpha[95][16][8]={"]
for c in range(32,127):
    im=Image.new("L",(W,H),0); d=ImageDraw.Draw(im)
    d.text((0,0),chr(c),font=f,fill=255)
    rows=[]
    for y in range(H):
        rows.append("{"+",".join(str(im.getpixel((x,y))) for x in range(W))+"}")
    out.append("{"+",".join(rows)+"},")
out.append("};")
open(sys.argv[1],"w").write("\n".join(out))
