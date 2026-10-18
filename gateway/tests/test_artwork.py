import asyncio
import importlib.util
from io import BytesIO
from pathlib import Path
import struct
import sys
from unittest.mock import AsyncMock
import pytest
from PIL import Image
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from artwork import ArtworkService, PACKET_SIZE, decode_source


def image_bytes(size=(800,400), colour=(255,0,0)):
    output=BytesIO(); Image.new("RGB",size,colour).save(output,format="PNG"); return output.getvalue()


def item(proxy="cover-a"):
    return {"queue_item_id":"track-a","image":{"proxy_id":proxy,"path":"cover.jpg","provider":"navidrome"}}


def test_metadata_registration_and_same_album_identity():
    service=ArtworkService("ws://ma:8095/ws")
    message={"result":[{"queue_id":"room","current_item":item()}]}
    assert service.annotate(message) is message
    key=message["result"][0]["current_item"]["controller_art_id"]
    assert len(key)==64 and key==service.register(item())
    assert service.register({})=="generic"
    assert service.source_url(service.sources[key])=="http://ma:8095/imageproxy/cover-a?size=288&fmt=png"
    event={"data":{"queue_id":"room","current_item":{}}}
    service.annotate(event); assert event["data"]["current_item"]["controller_art_id"]=="generic"


def test_legacy_and_album_metadata_images():
    service=ArtworkService("https://ma:8095/ws")
    key=service.register({"media_item":{"album":{"metadata":{"images":[{"type":"thumb","provider":"files","path":"album/cover & art.jpg"}]}}}})
    assert "path=album%2Fcover+%26+art.jpg" in service.source_url(service.sources[key])
    assert service.source_url(service.sources[key]).startswith("https://ma:8095/imageproxy?")


def test_resize_and_little_endian_rgb565():
    packet=decode_source(image_bytes())
    assert len(packet)==PACKET_SIZE
    assert packet[:8]==b"MCAR"+struct.pack("<HH",288,288)
    assert packet[8:10]==struct.pack("<H",0xf800)


@pytest.mark.parametrize("failure", [TimeoutError(), ValueError("bad provider"), b"invalid image", image_bytes((2100,2100))], ids=["timeout", "upstream-error", "corrupt-image", "excessive-dimensions"])
def test_missing_broken_or_oversized_art_gets_generic(failure):
    async def run():
        service=ArtworkService("http://ma/ws")
        key=service.register(item())
        service.fetch_source=AsyncMock(side_effect=failure if isinstance(failure,Exception) else None,
                                      return_value=failure if isinstance(failure,bytes) else None)
        packet,fallback=await service.get(key)
        assert fallback and packet==service.generic and len(packet)==PACKET_SIZE
        assert await service.get("generic")== (service.generic,True)
        assert await service.get("0"*64)==(service.generic,True)
    asyncio.run(run())


def test_cache_reuses_album_and_bounds_memory():
    async def run():
        service=ArtworkService("http://ma/ws")
        service.fetch_source=AsyncMock(return_value=image_bytes())
        key=service.register(item())
        assert not (await service.get(key))[1]
        assert await service.get(key)==await service.get(key)
        assert service.fetch_source.await_count==1
        for i in range(40): await service.get(service.register(item(str(i))))
        assert len(service.cache)==32
        for i in range(300): service.register(item(str(i)))
        assert len(service.sources)==256
    asyncio.run(run())


def test_endpoint_auth_generic_and_invalid_identity(monkeypatch):
    spec=importlib.util.spec_from_file_location("art_gateway",Path(__file__).resolve().parents[1]/"app.py")
    gateway=importlib.util.module_from_spec(spec); spec.loader.exec_module(gateway)
    monkeypatch.setattr(gateway,"DEVICE_TOKEN","test-device")
    async def run():
        app=web.Application(); app.router.add_get("/device/artwork/{art_id}",gateway.artwork_handler)
        async with TestClient(TestServer(app)) as client:
            response=await client.get("/device/artwork/generic"); assert response.status==403
            response=await client.get("/device/artwork/generic",headers={"X-Device-Token":"test-device"})
            assert response.status==200 and response.headers["X-Artwork-Fallback"]=="1"
            assert len(await response.read())==PACKET_SIZE
            response=await client.get("/device/artwork/arbitrary-host",headers={"X-Device-Token":"test-device"})
            assert response.status==400
    asyncio.run(run())


def test_fetch_limits_redirects_and_response_bytes():
    async def run():
        async def route(request):
            assert request.headers.get("Authorization")=="Bearer appliance-token"
            if request.match_info["key"]=="redirect": raise web.HTTPFound("http://untrusted.invalid/image")
            return web.Response(body=b"x"*(2*1024*1024+1))
        app=web.Application(); app.router.add_get("/imageproxy/{key}",route)
        async with TestServer(app) as server:
            service=ArtworkService(str(server.make_url("/ws"))); service.token="appliance-token"
            for key in ("redirect","large"):
                with pytest.raises(ValueError): await service.fetch_source({"proxy_id":key})
    asyncio.run(run())
