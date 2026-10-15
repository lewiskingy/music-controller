"""Bounded provider logos: MA images plus persistent per-instance overrides."""
import asyncio
import base64
from hashlib import sha256
from io import BytesIO
import json
import os
from pathlib import Path
import re
import struct
import tempfile
from urllib.parse import urlsplit
import warnings
from aiohttp import web
from PIL import Image, ImageDraw, ImageOps
from defusedxml import ElementTree
import cairosvg

SIZE = 40
MAX_ICON = 256 * 1024
PACKET_SIZE = 8 + SIZE * SIZE * 3


def encode(image):
    image = ImageOps.contain(image.convert('RGBA'), (SIZE, SIZE), Image.Resampling.LANCZOS)
    canvas = Image.new('RGBA', (SIZE, SIZE)); canvas.alpha_composite(image, ((SIZE-image.width)//2, (SIZE-image.height)//2))
    pixels = bytearray()
    for r,g,b,a in canvas.get_flattened_data():
        pixels.extend(struct.pack('<HB', ((r>>3)<<11)|((g>>2)<<5)|(b>>3), a))
    png = BytesIO(); canvas.save(png, format='PNG')
    return b'MCPI'+struct.pack('<HH',SIZE,SIZE)+pixels, png.getvalue()


def normalize(data):
    if not data or len(data)>MAX_ICON:
        raise ValueError('Icon must be at most 256 KiB')
    if data.lstrip().startswith(b'<'):
        root = ElementTree.fromstring(data)
        if root.tag.split('}')[-1] != 'svg' or len(list(root.iter())) > 2048:
            raise ValueError('Invalid SVG')
        for node in root.iter():
            if node.tag.split('}')[-1] in {'image','foreignObject','script','filter','feImage'}:
                raise ValueError('Embedded content is not supported')
            for key,value in node.attrib.items():
                if key.split('}')[-1] == 'base': raise ValueError('External base is not supported')
                if key.split('}')[-1] in {'href','src'} and not value.startswith('#'):
                    raise ValueError('External resources are not supported')
        text=data.decode('utf-8')
        # No external CSS/font/image fetches, including escaped CSS URLs.
        if '@' in text or '\\' in text or any(not value.strip(' \"\'').startswith('#') for value in re.findall(r'url\s*\((.*?)\)', text, re.I|re.S)):
            raise ValueError('External CSS resources are not supported')
        data=cairosvg.svg2png(bytestring=data,output_width=SIZE,output_height=SIZE)
    with warnings.catch_warnings():
        warnings.simplefilter('error', Image.DecompressionBombWarning)
        with Image.open(BytesIO(data)) as image:
            if image.format not in {'PNG','JPEG','WEBP'} or image.width*image.height>1_000_000:
                raise ValueError('Unsupported image or dimensions')
            image.load()
            return encode(image)


def generic():
    image=Image.new('RGBA',(40,40)); draw=ImageDraw.Draw(image)
    draw.ellipse((2,2,38,38), fill=(110,125,135,255))
    draw.text((15,10),'?',fill='white')
    return encode(image)


PAGE='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Provider icons</title><style>body{font:18px system-ui;background:#101c18;color:#eee;max-width:620px;margin:auto;padding:24px}label{display:block;margin:20px 0}input,button,select{font:inherit;padding:12px;max-width:100%;box-sizing:border-box}select{width:100%}img{width:80px;height:80px;background:#ddd;border-radius:12px;padding:8px}button{margin:8px 4px 8px 0}</style><h1>Provider icons</h1><p>Music Assistant supplies logos automatically where supported. Upload an override for a missing or generic logo, such as a Navidrome server listed as OpenSubsonic.</p><label>Provider<select id="providers"></select></label><img id="preview" alt="Provider logo"><p id="source"></p><label>Logo (SVG, PNG, JPEG or WebP; max 256 KiB)<input id="file" type="file" accept=".svg,.png,.jpg,.jpeg,.webp"></label><button id="upload">Upload override</button><button id="remove">Use Music Assistant icon</button><button id="refresh">Refresh providers</button><p id="status" role="status"></p><script>
const base=location.pathname.replace(/\\/$/,'');const el=id=>document.getElementById(id);let items=[];
function show(){const p=items.find(x=>x.key===el('providers').value);el('upload').disabled=el('remove').disabled=!p;if(!p){el('preview').removeAttribute('src');el('source').textContent='Connect a controller to discover its music providers.';return}el('preview').src=base+'/api/'+p.key+'/preview?v='+Date.now();el('source').textContent=p.domain+' — '+p.source;}
async function refresh(){try{const r=await fetch(base+'/api');if(!r.ok)throw Error('Cannot load icons');items=await r.json();const old=el('providers').value;el('providers').replaceChildren(...items.map(p=>new Option(p.name+' ('+p.instance_id+')',p.key)));if(items.some(p=>p.key===old))el('providers').value=old;show()}catch(e){el('status').textContent=e.message}}
async function change(remove){const p=el('providers').value;if(!p)return;try{const file=el('file').files[0];if(!remove&&!file)throw Error('Choose an image first');const r=await fetch(base+'/api/'+p,{method:remove?'DELETE':'PUT',body:remove?undefined:file,headers:remove?{}:{'Content-Type':'application/octet-stream'}});if(!r.ok)throw Error(await r.text());el('status').textContent='Saved. Connected controllers refresh icons automatically (about 30 seconds).';await refresh()}catch(e){el('status').textContent=e.message}}
el('providers').onchange=show;el('upload').onclick=()=>change(false);el('remove').onclick=()=>change(true);el('refresh').onclick=refresh;refresh();</script></html>'''

class ProviderIcons:
    def __init__(self,path):
        self.path=Path(path)
        self.records={}
        self.overrides={}
        self.ma_images={}
        self.fallback=generic()
        if self.path.exists():
            document=json.loads(self.path.read_text())
            if document.get('version')!=1: raise ValueError('Unsupported provider icon state')
            for key,png in document.get('overrides',{}).items():
                if not re.fullmatch('[a-f0-9]{64}',key): raise ValueError('Invalid stored provider identity')
                self.overrides[key]=normalize(base64.b64decode(png,validate=True))

    def persist(self,values):
        self.path.parent.mkdir(parents=True,exist_ok=True)
        payload=json.dumps({'version':1,'overrides':{k:base64.b64encode(v[1]).decode() for k,v in values.items()}})
        fd,name=tempfile.mkstemp(dir=self.path.parent,prefix='.provider-icons-')
        try:
            with os.fdopen(fd,'w') as stream:
                stream.write(payload); stream.flush(); os.fsync(stream.fileno())
            os.replace(name,self.path)
            directory=os.open(self.path.parent,os.O_RDONLY)
            try: os.fsync(directory)
            finally: os.close(directory)
        finally:
            if os.path.exists(name): os.unlink(name)
        self.overrides=values

    def register(self,providers):
        domains=set()
        for p in providers:
            if not isinstance(p,dict) or p.get('type')!='music': continue
            instance=p.get('instance_id'); domain=p.get('domain')
            if not isinstance(instance,str) or not instance or len(instance)>128: continue
            if not isinstance(domain,str) or not re.fullmatch('[a-z0-9_]{1,64}',domain): continue
            key=sha256(instance.encode()).hexdigest()
            if key not in self.records and len(self.records)>=64: continue
            self.records[key]={'key':key,'instance_id':instance,'domain':domain,'name':str(p.get('name') or domain)[:96]}
            p['controller_icon_id']=key
            domains.add(domain)
        return domains

    async def receive(self,domain,value):
        if not isinstance(value,str) or len(value)>MAX_ICON*2: return
        try:
            # Current MA uses data URIs; older manifests may contain inline SVG.
            if value.startswith('data:'):
                head,encoded=value.split(',',1)
                if head not in {'data:image/svg+xml;base64','data:image/png;base64','data:image/webp;base64','data:image/jpeg;base64'}: return
                raw=base64.b64decode(encoded,validate=True)
            else: raw=value.encode()
            self.ma_images[domain]=await asyncio.to_thread(normalize,raw)
        except Exception:
            return  # Malformed/unsupported logos keep a clearly generic fallback.

    def image(self,key):
        if key in self.overrides: return self.overrides[key],'uploaded override'
        record=self.records.get(key)
        if record and record['domain'] in self.ma_images: return self.ma_images[record['domain']],'Music Assistant'
        return self.fallback,'generic fallback'

    def resolve(self,provider):
        if not isinstance(provider,str) or provider=='library': return None
        matches=[r for r in self.records.values() if r['instance_id']==provider]
        if not matches: matches=[r for r in self.records.values() if r['domain']==provider]
        return matches[0] if len(matches)==1 else None

    def annotate(self,message):
        result=message.get('result')
        queues=result if isinstance(result,list) else [message.get('data')]
        for queue in queues:
            if not isinstance(queue,dict): continue
            item=queue.get('current_item')
            if not isinstance(item,dict): continue
            media=item.get('media_item') or item
            stream=item.get('streamdetails') or {}
            active=stream.get('provider') if isinstance(stream,dict) else None
            record=self.resolve(active)
            has_stream_provider=isinstance(active,str) and bool(active)
            if record is None and not has_stream_provider and isinstance(media,dict): record=self.resolve(media.get('provider'))
            if record is None and not has_stream_provider and isinstance(media,dict):
                mappings=media.get('provider_mappings') or []
                candidates={r['key']:r for m in mappings if isinstance(m,dict) and (r:=self.resolve(m.get('provider_instance')))}
                if len(candidates)==1: record=next(iter(candidates.values()))
            # Never guess using the selected browse provider or the first of several mappings.
            item['controller_source_icon_id']=record['key'] if record else ''
            item['controller_source_name']=record['name'] if record else ''
        return message

    def check_origin(self,request):
        origin=urlsplit(request.headers.get('Origin',''))
        if origin.scheme not in {'http','https'} or origin.netloc!=request.host or origin.path or origin.query or origin.fragment:
            raise web.HTTPForbidden(text='Invalid controller origin')

    async def page(self,request): return web.Response(text=PAGE,content_type='text/html')
    async def listing(self,request):
        return web.json_response([{**r,'source':self.image(k)[1]} for k,r in self.records.items()],headers={'Cache-Control':'no-store'})
    async def preview(self,request):
        image,_=self.image(request.match_info['key'])
        return web.Response(body=image[1],content_type='image/png',headers={'Cache-Control':'no-store'})
    async def upload(self,request):
        self.check_origin(request)
        key=request.match_info['key']
        if key not in self.records: raise web.HTTPNotFound(text='Provider not discovered')
        if key not in self.overrides and len(self.overrides)>=64: raise web.HTTPBadRequest(text='Too many overrides')
        try:
            data=bytearray()
            async for chunk in request.content.iter_chunked(16384):
                data.extend(chunk)
                if len(data)>MAX_ICON: raise ValueError('Icon exceeds upload budget')
            data=bytes(data)
            icon=await asyncio.to_thread(normalize,data)
        except Exception as exc:
            raise web.HTTPBadRequest(text='Unsupported or invalid icon (max 256 KiB)') from exc
        self.persist({**self.overrides,key:icon})
        return web.json_response({'saved':True})
    async def remove(self,request):
        self.check_origin(request)
        self.persist({k:v for k,v in self.overrides.items() if k!=request.match_info['key']})
        return web.json_response({'saved':True})
    def routes(self,app):
        app.router.add_get('/settings/providers',self.page)
        app.router.add_get('/settings/providers/',self.page)
        app.router.add_get('/settings/providers/api',self.listing)
        app.router.add_put('/settings/providers/api/{key:[a-f0-9]{64}}',self.upload)
        app.router.add_delete('/settings/providers/api/{key:[a-f0-9]{64}}',self.remove)
        app.router.add_get('/settings/providers/api/{key:[a-f0-9]{64}}/preview',self.preview)
