import asyncio
import sys
from pathlib import Path
from io import BytesIO
import pytest
from PIL import Image
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from backdrop import BackdropStore, MAX_UPLOAD


def picture():
    out = BytesIO(); Image.new('RGB', (600, 900), (240, 100, 30)).save(out, format='PNG')
    return out.getvalue()


def test_store_persistence_and_packet(tmp_path):
    store = BackdropStore(tmp_path / 'backdrop.json')
    assert not store.config()['available']
    store.save({**store.state, 'enabled': True, 'image': store.normalize(picture())})
    assert store.packet[:8] == b'MCBG\xe0\x01\x20\x03'
    assert len(store.packet) == 8 + 480 * 800 * 2
    recovered = BackdropStore(store.path)
    assert recovered.packet == store.packet
    version = store.config()['version']
    store.save({**store.state, 'brightness': 50})
    assert store.config()['version'] != version
    store.save({**store.state, 'enabled': False})
    assert store.config()['available'] and not store.config()['enabled']


def test_invalid_image(tmp_path):
    with pytest.raises(Exception): BackdropStore.normalize(b'not an image')


def test_routes(tmp_path):
    async def scenario():
        store = BackdropStore(tmp_path / 'backdrop.json')
        app = web.Application(client_max_size=MAX_UPLOAD); store.routes(app)
        async with TestClient(TestServer(app)) as client:
            assert (await client.get('/backdrop')).status == 200
            assert (await client.get('/device/backdrop')).status == 204
            assert (await client.put('/backdrop/image', data=b'invalid')).status == 400
            assert (await client.put('/backdrop/image', data=picture())).status == 200
            response = await client.get('/device/backdrop')
            assert response.status == 200
            packet = await response.read(); assert len(packet) == 768008
            assert (await client.get('/device/backdrop', headers={'If-None-Match': response.headers['ETag']})).status == 304
            assert (await client.get('/backdrop/preview')).headers['Content-Type'].startswith('image/png')
            assert (await client.put('/backdrop/config', json={'enabled': False, 'brightness': True})).status == 400
            assert (await client.put('/backdrop/config', json={'enabled': False, 'brightness': 40})).status == 200
            assert (await client.get('/device/backdrop')).status == 204
            assert (await client.delete('/backdrop/image')).status == 200
            assert not (await (await client.get('/backdrop/config')).json())['available']
            assert (await client.get('/backdrop/preview')).status == 404
            assert (await client.put('/backdrop/image', data=b'x' * (MAX_UPLOAD + 1))).status == 413
    asyncio.run(scenario())
