"""Deterministic gateway artwork and one Halloween test backdrop; no network."""
from pathlib import Path
import math
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "gateway"))
from backdrop import BackdropStore
from PIL import Image

output=Path(sys.argv[1]); output.mkdir(parents=True,exist_ok=True)
source = Path(__file__).resolve().parent / "fixtures" / "halloween-480x800.png"
with Image.open(source) as image:
    if image.size != (480, 800):
        raise ValueError("Halloween test backdrop must be exactly 480x800")
# Use the production gateway normalization, default brightness and MCBG encoder.
# Do not persist gateway settings or enable a backdrop on a real controller.
backdrop = BackdropStore.__new__(BackdropStore)
backdrop.state = {"enabled": True, "brightness": 35,
                  "image": BackdropStore.normalize(source.read_bytes())}
backdrop.prepare()
(output / "test-theme.mcbg").write_bytes(backdrop.packet)
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
