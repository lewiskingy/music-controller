"""Deterministic mocked gateway MCAR responses; no network or image library."""
from pathlib import Path
import math
import struct
import sys
output=Path(sys.argv[1]); output.mkdir(parents=True,exist_ok=True)
for name in ("album","generic"):
    pixels=bytearray()
    for y in range(288):
        for x in range(288):
            if name=="album":
                r,g,b=(int(55+x*0.6),int(45+y*0.55),int(145+(x+y)*0.12))
                if abs(x-y)<6 or abs(x+y-288)<6: r,g,b=245,204,104
            else:
                radius=math.hypot(x-144,y-144)
                r,g,b=(44,48,53)
                if radius<106: r,g,b=24,27,31
                if any(abs(radius-v)<1.5 for v in (106,90,74,58)): r,g,b=75,82,89
                if radius<30: r,g,b=166,177,183
                if radius<6: r,g,b=24,27,31
            pixels.extend(struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)))
    (output/(name+'.mcar')).write_bytes(b'MCAR'+struct.pack('<HH',288,288)+pixels)
