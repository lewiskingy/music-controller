import asyncio
import base64
from io import BytesIO
from pathlib import Path
import sys
import pytest
from PIL import Image
from aiohttp import web, WSMsgType
from aiohttp.test_utils import TestClient, TestServer
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from provider_icons import ProviderIcons, normalize, MAX_ICON, PACKET_SIZE

SVG=b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 40 40"><circle cx="20" cy="20" r="18" fill="#1db954"/></svg>'
PROVIDERS=[dict(type='music',domain='spotify',instance_id='spotify-one',name='Spotify'),dict(type='music',domain='opensubsonic',instance_id='navidrome-one',name='Navidrome')]


def test_normalization_transparency_and_limits():
    packet,png=normalize(SVG)
    assert packet[:8]==b'MCPI\x28\x00\x28\x00' and len(packet)==PACKET_SIZE
    assert packet[10]==0  # Transparent corners survive.
    assert Image.open(BytesIO(png)).size==(40,40)
    for data in [b'x',b'x'*(MAX_ICON+1),b'<svg><image href="http://evil/image.png"/></svg>',b'<svg><style>@import "file:///etc/passwd";</style></svg>',b'<svg><use href="file:///etc/passwd"/></svg>',b'<svg style="fill:url(http://evil/a)"/>',b'<!DOCTYPE x [<!ENTITY e SYSTEM "file:///etc/passwd">]><svg>&e;</svg>']:
        with pytest.raises(Exception): normalize(data)


def test_source_identity_is_independent_of_selected_provider(tmp_path):
    store=ProviderIcons(tmp_path/'icons.json')
    providers=[p.copy() for p in PROVIDERS];store.register(providers)
    nav=providers[1]['controller_icon_id']
    queue={'queue_id':'room','current_item':{'media_item':{'provider':'library','provider_mappings':[{'provider_instance':'spotify-one'},{'provider_instance':'navidrome-one'}]},'streamdetails':{'provider':'navidrome-one'}}}
    assert store.annotate({'result':[queue]})['result'][0]['current_item']['controller_source_icon_id']==nav
    del queue['current_item']['streamdetails']
    assert store.annotate({'result':[queue]})['result'][0]['current_item']['controller_source_icon_id']==''
    queue['current_item']['media_item']['provider_mappings']=[{'provider_instance':'navidrome-one'}]
    assert store.annotate({'result':[queue]})['result'][0]['current_item']['controller_source_icon_id']==nav
    queue['current_item']['streamdetails']={'provider':'removed-instance'}
    assert store.annotate({'result':[queue]})['result'][0]['current_item']['controller_source_icon_id']==''


def test_upload_override_persists_and_falls_back_to_ma(tmp_path):
    async def run():
        path=tmp_path/'icons.json'; store=ProviderIcons(path)
        providers=[p.copy() for p in PROVIDERS];store.register(providers)
        key=providers[0]['controller_icon_id']
        await store.receive('spotify','data:image/svg+xml;base64,'+base64.b64encode(SVG).decode())
        assert store.image(key)[1]=='Music Assistant'
        await store.receive('spotify','data:image/svg+xml;base64,'+base64.b64encode(b'<svg><image href="http://evil"/></svg>').decode())
        assert store.image(key)[1]=='Music Assistant'  # Invalid replacement preserves good cache.
        app=web.Application();store.routes(app)
        async with TestClient(TestServer(app)) as client:
            endpoint='/settings/providers/api/'+key
            headers={'Origin':str(client.make_url('/')).rstrip('/')}
            assert (await client.get('/settings/providers')).status==200
            assert (await client.put(endpoint,data=SVG)).status==403
            assert (await client.put(endpoint,data=SVG,headers=headers)).status==200
            assert store.image(key)[1]=='uploaded override'
            assert ProviderIcons(path).image(key)[0]==store.image(key)[0]
            assert (await client.get(endpoint+'/preview')).headers['Content-Type'].startswith('image/png')
            assert (await client.put(endpoint,data=b'x'*(MAX_ICON+1),headers=headers)).status==400
            assert (await client.delete(endpoint,headers=headers)).status==200
            assert store.image(key)[1]=='Music Assistant'
            assert not ProviderIcons(path).overrides
    asyncio.run(run())


@pytest.mark.parametrize('older',[False,True])
def test_authenticated_gateway_icon_discovery_and_serving(tmp_path,monkeypatch,older):
    import app as gateway
    monkeypatch.setattr(gateway,'DEVICE_TOKEN','device')
    monkeypatch.setenv('CONTROLLER_PROVIDER_ICONS_STATE',str(tmp_path/'icons.json'))
    async def run():
        observed=[]
        async def ma(request):
            ws=web.WebSocketResponse();await ws.prepare(request)
            async for message in ws:
                data=message.json();mid=data['message_id'];name=data['command'];observed.append(name)
                result={};error=None
                if name=='auth/login':result={'success':True,'access_token':'test'}
                elif name=='providers':result=[p.copy() for p in PROVIDERS]
                elif name=='providers/icon':
                    if older:error='Unknown command'
                    else:result='data:image/svg+xml;base64,'+base64.b64encode(SVG).decode()
                elif name=='providers/manifests':result=[{'domain':'spotify','icon_svg':SVG.decode()}] if older else []
                await ws.send_json({'message_id':mid,**({'error':error} if error else {'result':result})})
            return ws
        upstream_app=web.Application();upstream_app.router.add_get('/ws',ma)
        async with TestServer(upstream_app) as upstream:
            monkeypatch.setattr(gateway,'MA_URL',str(upstream.make_url('/ws')))
            async with TestClient(TestServer(gateway.create_app())) as client:
                ws=await client.ws_connect('/device/ws',headers={'X-Device-Token':'device'})
                assert (await ws.receive_json())['event']=='gateway/ready'
                assert (await ws.receive_json())['data']['provider_icons']
                await ws.send_json({'message_id':'m','command':'providers'})
                providers=(await ws.receive_json())['result'];key=providers[0]['controller_icon_id']
                # Ordered synchronization after all queued icon requests.
                await ws.send_json({'message_id':'sync','command':'players/all'})
                while (await ws.receive_json()).get('message_id')!='sync':pass
                path='/device/artwork/provider-'+key
                assert (await client.get(path)).status==403
                response=await client.get(path,headers={'X-Device-Token':'device'})
                assert response.status==200 and (await response.read())[:4]==b'MCPI'
                assert response.headers['X-Icon-Source']=='Music Assistant'
                assert 'providers/icon' in observed
                await ws.close()
    asyncio.run(run())
