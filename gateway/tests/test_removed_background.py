"""Legacy uploaded-background URLs are absent after the firmware splash move."""
import asyncio
from aiohttp.test_utils import TestClient, TestServer
import app as gateway


def test_background_endpoints_removed(monkeypatch, tmp_path):
    monkeypatch.setenv('CONTROLLER_BACKDROP_FILE', str(tmp_path / 'missing.png'))
    monkeypatch.setenv('CONTROLLER_BACKDROP_STATE', str(tmp_path / 'old.json'))
    monkeypatch.setenv('CONTROLLER_PROVIDER_ICONS_STATE', str(tmp_path / 'icons.json'))

    async def run():
        async with TestClient(TestServer(gateway.create_app())) as client:
            for path in ('/backdrop', '/backdrop/config', '/backdrop/preview', '/device/backdrop'):
                assert (await client.get(path)).status == 404
            assert (await client.put('/backdrop/image', data=b'old upload')).status == 404
            assert (await client.delete('/backdrop/image')).status == 404
        assert not (tmp_path / 'old.json').exists()
    asyncio.run(run())
