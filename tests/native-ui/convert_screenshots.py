"""Convert simulator RGB framebuffers to PNG without extra dependencies."""
from pathlib import Path
import struct
import sys
import zlib

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

root = Path(sys.argv[1])
for source in sorted(root.glob('*.ppm')):
    data = source.read_bytes()
    header = b'P6\n480 800\n255\n'
    assert data.startswith(header)
    pixels = data[len(header):]
    assert len(pixels) == 480 * 800 * 3
    rows = b''.join(b'\0' + pixels[y*1440:(y+1)*1440] for y in range(800))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB',480,800,8,2,0,0,0))
    png += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    source.with_suffix('.png').write_bytes(png)
print(f'Converted {len(list(root.glob("*.png")))} screenshots')
