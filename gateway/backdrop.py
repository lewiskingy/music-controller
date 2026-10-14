"""One persistent LAN-managed backdrop, prepared for the portrait display."""
import asyncio
import base64
import json
import os
from pathlib import Path
from io import BytesIO
from hashlib import sha256
import struct
import warnings
from aiohttp import web
from PIL import Image, ImageOps, ImageEnhance

WIDTH, HEIGHT = 480, 800
MAX_UPLOAD = 8 * 1024 * 1024
PAGE = '''<!doctype html><html><meta name="viewport" content="width=device-width,initial-scale=1"><title>Controller backdrop</title>
<style>body{font:16px system-ui;max-width:650px;margin:24px auto;padding:0 18px;background:#162620;color:#f0f5ed}button,input{font:inherit;margin:10px 0}button{padding:10px 18px}img{max-height:400px;max-width:100%;border-radius:12px}label{display:block}</style>
<h1>Controller backdrop</h1><p>Upload a seasonal image or photograph. Centre-cropped to 480 × 800; colour palettes remain on your controller.</p>
<form id="upload"><label>Image (JPEG, PNG or WebP, up to 8 MiB)<input id="file" type="file" accept="image/jpeg,image/png,image/webp" required></label><button>Upload / replace</button></form>
<form id="settings"><label><input id="enabled" type="checkbox"> Enable gateway backdrop</label><label>Image brightness <input id="brightness" type="range" min="10" max="70" value="35"><output id="level"></output>%</label><button>Save settings</button></form>
<button id="remove">Remove image</button><p id="status" role="status"></p><img id="preview" alt="Prepared backdrop preview" hidden>
<script>
const base=location.pathname.replace(/\\/$/,'');
const el=id=>document.getElementById(id);
async function request(path,options){const r=await fetch(base+path,options);if(!r.ok)throw Error(await r.text());return r;}
async function refresh(){const s=await(await request('/config')).json();el('enabled').checked=s.enabled;el('brightness').value=s.brightness;el('level').value=s.brightness;el('preview').hidden=!s.available;if(s.available)el('preview').src=base+'/preview?v='+s.version;}
async function run(fn){try{await fn();await refresh();el('status').textContent='Saved';}catch(e){el('status').textContent=e.message;}}
el('brightness').oninput=()=>el('level').value=el('brightness').value;
el('upload').onsubmit=e=>{e.preventDefault();run(()=>request('/image',{method:'PUT',headers:{'Content-Type':'application/octet-stream'},body:el('file').files[0]}));};
el('settings').onsubmit=e=>{e.preventDefault();run(()=>request('/config',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:el('enabled').checked,brightness:Number(el('brightness').value)})}));};
el('remove').onclick=()=>run(()=>request('/image',{method:'DELETE'}));
refresh().catch(e=>el('status').textContent=e.message);
</script></html>'''

class BackdropStore:
    def __init__(self, path):
        self.path = Path(path)
        self.lock = asyncio.Lock()
        self.state = {'enabled': False, 'brightness': 35, 'image': ''}
        if self.path.exists():
            self.state.update(json.loads(self.path.read_text()))
        self.packet = self.preview = b''
        self.prepare()

    @staticmethod
    def normalize(data):
        with warnings.catch_warnings():
            warnings.simplefilter('error', Image.DecompressionBombWarning)
            with Image.open(BytesIO(data)) as im:
                if im.format not in ('JPEG', 'PNG', 'WEBP') or im.width * im.height > 4_000_000:
                    raise ValueError('Use a JPEG, PNG or WebP up to 4 megapixels')
                im = ImageOps.exif_transpose(im)
                im = ImageOps.fit(im.convert('RGBA'), (WIDTH, HEIGHT), method=Image.Resampling.LANCZOS)
                canvas = Image.new('RGBA', im.size, (0, 0, 0, 255))
                canvas.alpha_composite(im)
                out = BytesIO(); canvas.convert('RGB').save(out, format='PNG')
                return base64.b64encode(out.getvalue()).decode()

    def prepare(self):
        self.packet = self.preview = b''
        if not self.state['image']:
            return
        with Image.open(BytesIO(base64.b64decode(self.state['image']))) as im:
            im = ImageEnhance.Brightness(im.convert('RGB')).enhance(self.state['brightness'] / 100)
            out = BytesIO(); im.save(out, format='PNG'); self.preview = out.getvalue()
            raw = im.tobytes(); pixels = bytearray(WIDTH * HEIGHT * 2)
            for i in range(WIDTH * HEIGHT):
                r, g, b = raw[i*3:i*3+3]
                struct.pack_into('<H', pixels, i*2, ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            self.packet = b'MCBG' + struct.pack('<HH', WIDTH, HEIGHT) + pixels

    def save(self, state):
        # Publish only after preparation and atomic persistence both succeed.
        candidate = BackdropStore.__new__(BackdropStore)
        candidate.state = state; candidate.prepare()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix('.tmp')
        temporary.write_text(json.dumps(state)); os.replace(temporary, self.path)
        self.state, self.packet, self.preview = state, candidate.packet, candidate.preview

    def config(self):
        return {'enabled': self.state['enabled'], 'brightness': self.state['brightness'],
                'available': bool(self.packet), 'width': WIDTH, 'height': HEIGHT,
                'version': sha256(self.packet).hexdigest() if self.packet else ''}

    async def page(self, request):
        return web.Response(text=PAGE, content_type='text/html')

    async def configuration(self, request):
        if request.method == 'PUT':
            try:
                data = await request.json()
                if set(data) != {'enabled', 'brightness'} or type(data['enabled']) is not bool or type(data['brightness']) is not int or not 10 <= data['brightness'] <= 70:
                    raise ValueError()
            except (ValueError, TypeError, KeyError):
                raise web.HTTPBadRequest(text='Specify enabled and brightness (10–70)')
            async with self.lock:
                await asyncio.to_thread(self.save, {**self.state, **data})
        return web.json_response(self.config(), headers={'Cache-Control': 'no-store'})

    async def image(self, request):
        image = ''
        if request.method == 'PUT':
            data = await request.read()
            try:
                image = await asyncio.to_thread(self.normalize, data)
            except Exception:
                raise web.HTTPBadRequest(text='Invalid image: use JPEG, PNG or WebP up to 4 megapixels')
        async with self.lock:
            await asyncio.to_thread(self.save, {**self.state, 'image': image, 'enabled': bool(image)})
        return web.json_response(self.config())

    async def preview_image(self, request):
        if not self.preview: raise web.HTTPNotFound()
        return web.Response(body=self.preview, content_type='image/png', headers={'Cache-Control': 'no-store'})

    async def device(self, request):
        if not self.state['enabled'] or not self.packet: raise web.HTTPNoContent()
        etag = '"' + self.config()['version'] + '"'
        if request.headers.get('If-None-Match') == etag:
            return web.Response(status=304, headers={'ETag': etag})
        return web.Response(body=self.packet, content_type='application/octet-stream',
                            headers={'ETag': etag, 'Cache-Control': 'no-cache'})

    def routes(self, app):
        app.router.add_get('/backdrop', self.page)
        app.router.add_get('/backdrop/config', self.configuration)
        app.router.add_put('/backdrop/config', self.configuration)
        app.router.add_put('/backdrop/image', self.image)
        app.router.add_delete('/backdrop/image', self.image)
        app.router.add_get('/backdrop/preview', self.preview_image)
        app.router.add_get('/device/backdrop', self.device)
