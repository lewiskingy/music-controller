"""Live per-device preferences. Devices own persistence; offline writes are rejected."""
import asyncio
import json
import re
import uuid
from urllib.parse import urlsplit
from aiohttp import web

PAGE = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Controller settings</title>
<style>body{font:18px system-ui;background:#101c18;color:#eee;max-width:560px;margin:24px auto;padding:16px}label{display:block;margin:20px 0}input,select,button{font:inherit;padding:12px;max-width:100%;box-sizing:border-box}input,select{display:block;width:100%}button{margin:8px 8px 0 0}p{line-height:1.5}</style>
<h1>Controller settings</h1><p>Settings are saved on the selected device. Auto currently uses the battery timeout because USB detection is unavailable.</p>
<label>Connected controller<select id="device"></select></label><form id="form"><label>Battery timeout (seconds)<input id="battery" type="number" min="0" max="86400" required></label><label>USB / external timeout (seconds)<input id="usb" type="number" min="0" max="86400" required></label><p>0 means always on.</p><label>Power mode<select id="mode"><option value="0">Auto (unknown: battery)</option><option value="1">Battery</option><option value="2">USB / external</option></select></label><button id="save">Save</button><button id="reset" type="button">Reset defaults</button></form><p id="status" role="status"></p>
<script>
const base=location.pathname.replace(/\\/$/,'');let devices={};let busy=false;
const el=id=>document.getElementById(id);
function fill(){const d=devices[el('device').value];if(!d)return;el('battery').value=d.battery_seconds;el('usb').value=d.usb_seconds;el('mode').value=d.power_mode;el('status').textContent='Power source: unknown. Defaults: battery '+d.battery_default+'s; USB '+d.usb_default+'s.';}
async function refresh(){try{const r=await fetch(base+'/api');if(!r.ok)throw Error('Cannot load devices');devices=await r.json();const old=el('device').value;el('device').replaceChildren(...Object.keys(devices).map(id=>new Option(id,id)));if(devices[old])el('device').value=old;el('save').disabled=el('reset').disabled=!Object.keys(devices).length;fill();if(!Object.keys(devices).length)el('status').textContent='No compatible controller connected. Reconnect or update its firmware.';}catch(e){el('status').textContent=e.message;}}
async function save(reset){if(busy)return;busy=true;el('save').disabled=el('reset').disabled=true;el('status').textContent='Waiting for device to save…';try{const r=await fetch(base+'/api/'+encodeURIComponent(el('device').value),{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({battery_seconds:reset?-1:Number(el('battery').value),usb_seconds:reset?-1:Number(el('usb').value),power_mode:reset?0:Number(el('mode').value)})});const d=await r.json();if(!r.ok)throw Error(d.error||'Save failed');devices[el('device').value]=d;fill();el('status').textContent='Saved on device.';}catch(e){el('status').textContent=e.message;}finally{busy=false;el('save').disabled=el('reset').disabled=false;}}
el('device').onchange=fill;el('form').onsubmit=e=>{e.preventDefault();save(false)};el('reset').onclick=()=>save(true);refresh();
</script></html>'''

class DeviceSettings:
    def __init__(self, origin):
        self.origin = origin
        self.devices = {}
        self.pending = {}

    async def receive(self, ws, data):
        if not isinstance(data, dict) or not re.fullmatch(r'[0-9a-f]{12}', str(data.get('device_id', ''))):
            raise ValueError('Invalid device identity')
        for key in ('battery_seconds', 'usb_seconds', 'battery_default', 'usb_default'):
            if type(data.get(key)) is not int or not 0 <= data[key] <= 86400:
                raise ValueError('Invalid timeout')
        if type(data.get('power_mode')) is not int or data['power_mode'] not in (0, 1, 2):
            raise ValueError('Invalid power mode')
        device = data['device_id']
        # A socket cannot claim another controller after registration.
        if any(sock is ws and identity != device for identity, (sock, _) in self.devices.items()):
            raise ValueError('Device identity changed')
        self.devices[device] = (ws, data)
        request_id = data.get('request_id')
        item = self.pending.get(request_id)
        if item and item[0] == device and item[1] is ws and not item[2].done():
            item[2].set_result(data)

    def disconnect(self, ws):
        for device, (sock, _) in list(self.devices.items()):
            if sock is ws:
                del self.devices[device]
        for _, sock, future in self.pending.values():
            if sock is ws and not future.done():
                future.set_exception(ConnectionError('Controller disconnected; reconnect and verify settings'))

    async def page(self, request):
        return web.Response(text=PAGE, content_type='text/html', headers={'Cache-Control':'no-store'})

    async def get(self, request):
        return web.json_response({key: state for key, (ws, state) in self.devices.items() if not ws.closed}, headers={'Cache-Control':'no-store'})

    async def put(self, request):
        origin = request.headers.get('Origin', '')
        parsed = urlsplit(origin)
        same_host = parsed.scheme in ('http', 'https') and parsed.netloc == request.host and not parsed.path and not parsed.query and not parsed.fragment
        if not origin or not (same_host or (self.origin and origin == self.origin)):
            raise web.HTTPForbidden(text='Invalid controller origin')
        try:
            data = await request.json()
            if not isinstance(data, dict) or set(data) != {'battery_seconds','usb_seconds','power_mode'}:
                raise ValueError('Invalid settings')
            bat, usb, mode = (data[k] for k in ('battery_seconds','usb_seconds','power_mode'))
            if any(type(x) is not int for x in (bat, usb, mode)) or mode not in (0,1,2):
                raise ValueError('Invalid power mode or timeout')
            if not ((bat == usb == -1) or (0 <= bat <= 86400 and 0 <= usb <= 86400)):
                raise ValueError('Timeout must be 0–86400 seconds')
            device = request.match_info['device']
            entry = self.devices.get(device)
            if not entry or entry[0].closed:
                return web.json_response({'error':'Controller offline'}, status=409)
            if any(item[0] == device for item in self.pending.values()):
                return web.json_response({'error':'Another save is pending'}, status=409)
            ws = entry[0]
            request_id = uuid.uuid4().hex
            future = asyncio.get_running_loop().create_future()
            self.pending[request_id] = (device, ws, future)
            try:
                await ws.send_json({'event':'controller/settings/set', 'data':{**data,'request_id':request_id}})
                result = await asyncio.wait_for(future, timeout=12)
                if result.get('saved') is not True:
                    return web.json_response({'error':'Device could not save settings'}, status=502)
                return web.json_response(result)
            except (asyncio.TimeoutError, ConnectionError):
                return web.json_response({'error':'Save unconfirmed; reconnect and verify settings'}, status=504)
            finally:
                self.pending.pop(request_id, None)
        except (ValueError, TypeError, json.JSONDecodeError):
            return web.json_response({'error':'Invalid settings'}, status=400)

    def routes(self, app):
        app.router.add_get('/settings', self.page)
        app.router.add_get('/settings/', self.page)
        app.router.add_get('/settings/api', self.get)
        app.router.add_put('/settings/api/{device}', self.put)
