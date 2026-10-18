"""Bounded MA image adapter. Devices receive RGB565, never provider URLs."""
import asyncio
from collections import OrderedDict
from hashlib import sha256
from io import BytesIO
import json
import struct
import time
import warnings
from urllib.parse import urlsplit, urlunsplit, urlencode, quote
from PIL import Image, ImageDraw, ImageOps
from aiohttp import ClientSession, ClientTimeout

SIZE = 288
PACKET_SIZE = 8 + SIZE * SIZE * 2
MAX_SOURCE = 2 * 1024 * 1024
Image.MAX_IMAGE_PIXELS = 4_000_000

def generic_image():
    image = Image.new("RGB", (SIZE, SIZE), (44, 48, 53))
    draw = ImageDraw.Draw(image)
    draw.ellipse((38, 38, 250, 250), fill=(24, 27, 31), outline=(109, 116, 123), width=2)
    for inset in (54, 70, 86):
        draw.ellipse((inset, inset, SIZE-inset, SIZE-inset), outline=(58, 64, 70), width=2)
    draw.ellipse((114, 114, 174, 174), fill=(166, 177, 183))
    draw.ellipse((138, 138, 150, 150), fill=(24, 27, 31))
    return image

def encode_image(image):
    image = ImageOps.fit(image.convert("RGB"), (SIZE, SIZE), method=Image.Resampling.LANCZOS)
    pixels = bytearray(SIZE * SIZE * 2)
    raw = image.tobytes()
    for i in range(SIZE*SIZE):
        r, g, b = raw[i*3:i*3+3]
        struct.pack_into("<H", pixels, i*2, ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return b"MCAR" + struct.pack("<HH", SIZE, SIZE) + pixels

def decode_source(data):
    with warnings.catch_warnings():
        warnings.simplefilter("error", Image.DecompressionBombWarning)
        return _decode_source(data)

def _decode_source(data):
    with Image.open(BytesIO(data)) as image:
        if image.width * image.height > 4_000_000:
            raise ValueError("Artwork dimensions exceed budget")
        image.load()
        if image.mode == "RGBA" or "transparency" in image.info:
            rgba = image.convert("RGBA")
            background = Image.new("RGBA", rgba.size, (44, 48, 53, 255))
            image = Image.alpha_composite(background, rgba)
        return encode_image(image)

class ArtworkService:
    def __init__(self, ma_url):
        parts = urlsplit(ma_url)
        scheme = {"ws": "http", "wss": "https"}.get(parts.scheme, parts.scheme)
        self.base = urlunsplit((scheme, parts.netloc, "", "", ""))
        self.sources = OrderedDict()
        self.cache = OrderedDict()
        self.token = ""
        self.slots = asyncio.Semaphore(2)
        self.generic = encode_image(generic_image())

    def register(self, item):
        if not isinstance(item, dict):
            return "generic"
        image = item.get("image")
        if not isinstance(image, dict):
            media = item.get("media_item") or item
            if not isinstance(media, dict):
                return "generic"
            album = media.get("album") or {}
            if not isinstance(album, dict):
                album = {}
            images = (album.get("metadata") or {}).get("images") or (media.get("metadata") or {}).get("images") or []
            image = next((i for i in images if isinstance(i, dict) and i.get("type") == "thumb"), None)
        if not isinstance(image, dict):
            return "generic"
        source = {k: image[k] for k in ("proxy_id", "path", "provider") if isinstance(image.get(k), str)}
        if not source.get("proxy_id") and not (source.get("path") and source.get("provider")):
            return "generic"
        if any(len(v)>4096 for v in source.values()):
            return "generic"
        key = sha256(json.dumps(source, sort_keys=True).encode()).hexdigest()
        self.sources[key] = source
        self.sources.move_to_end(key)
        while len(self.sources)>256:
            self.sources.popitem(last=False)
        return key

    def annotate(self, message):
        result = message.get("result")
        queues = result if isinstance(result, list) else [message.get("data")]
        for queue in queues:
            if not isinstance(queue, dict) or "queue_id" not in queue:
                continue
            item = queue.get("current_item")
            if isinstance(item, dict):
                item["controller_art_id"] = self.register(item)
        return message

    def source_url(self, source):
        if source.get("proxy_id"):
            return self.base + "/imageproxy/" + quote(source["proxy_id"], safe="") + "?size=288&fmt=png"
        return self.base + "/imageproxy?" + urlencode({"path":source["path"], "provider":source["provider"], "size":288, "fmt":"png"})

    async def fetch_source(self, source):
        headers = {"Authorization": "Bearer " + self.token} if self.token else {}
        async with ClientSession(timeout=ClientTimeout(total=8)) as session:
            async with session.get(self.source_url(source), headers=headers, allow_redirects=False) as response:
                if response.status != 200:
                    raise ValueError("Artwork unavailable")
                data = bytearray()
                async for chunk in response.content.iter_chunked(65536):
                    data.extend(chunk)
                    if len(data)>MAX_SOURCE:
                        raise ValueError("Artwork response exceeds budget")
                return bytes(data)

    async def get(self, key):
        if key == "generic":
            return self.generic, True
        source = self.sources.get(key)
        if source is None:
            return self.generic, True
        async with self.slots:
            cached = self.cache.get(key)
            if cached and cached[0]>time.monotonic():
                self.cache.move_to_end(key)
                return cached[1], cached[2]
            try:
                data = await self.fetch_source(source)
                packet = await asyncio.to_thread(decode_source, data)
                fallback = False
            except Exception:
                packet, fallback = self.generic, True
            self.cache[key] = (time.monotonic()+(30 if fallback else 3600), packet, fallback)
            self.cache.move_to_end(key)
            while len(self.cache)>32:
                self.cache.popitem(last=False)
            return packet, fallback
