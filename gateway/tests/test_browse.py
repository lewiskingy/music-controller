import asyncio
import importlib.util
from pathlib import Path
import sys
import pytest
from aiohttp import web, WSMsgType
from aiohttp.test_utils import TestClient, TestServer
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from browse import BrowseRequest, validate_browse

def media(i,kind="album"):
    return {"item_id":str(i),"provider":"library","uri":f"library://{kind}/{i}","media_type":kind,
            "name":f"Album {i}","artists":[{"name":"Adele"}],"metadata":{"lyrics":"x"*10000}}

@pytest.mark.parametrize("kind,command",[("albums","music/albums/library_items"),("artists","music/artists/library_items"),("playlists","music/playlists/library_items"),
    ("artist_albums","music/artists/artist_albums"),("album_tracks","music/albums/album_tracks"),("playlist_tracks","music/playlists/playlist_tracks"),("folders","music/browse")])
def test_browse_translation_is_read_only(kind,command):
    request=BrowseRequest("b1",{"kind":kind,"provider":"navidrome","parent_id":"19","source":"navidrome","path":"navidrome://","limit":20,"offset":20})
    assert request.command==command
    if kind in {"albums","artists","playlists"}: assert request.args=={"provider":"navidrome","limit":21,"offset":20}
    if kind=="artist_albums": assert request.args["provider_filter"]=="navidrome"

@pytest.mark.parametrize("args",[{}, {"kind":"auth/login","provider":"nav"}, {"kind":"albums"}, {"kind":"albums","provider":""},
    {"kind":"albums","provider":"nav","limit":21}, {"kind":"albums","provider":"nav","offset":-1}, {"kind":"albums","provider":"nav","limit":True},
    {"kind":"album_tracks","provider":"nav"}, {"kind":"folders","provider":"nav"}, {"kind":"albums","provider":"nav","admin":True}])
def test_invalid_browse_requests_rejected(args):
    with pytest.raises(ValueError): validate_browse(args)

def test_compact_paged_response_and_source_identity():
    request=BrowseRequest("b1",{"kind":"albums","provider":"nav","limit":20,"offset":20})
    request.feed([media(i) for i in range(21)])
    result=request.response()["result"]
    assert len(result["items"])==20 and result["has_more"] and result["offset"]==20
    assert result["items"][0]["provider"]=="library" and result["items"][0]["subtitle"]=="Adele"
    assert "metadata" not in result["items"][0]


def test_unpaged_details_partial_chunks_are_bounded():
    request=BrowseRequest("b2",{"kind":"album_tracks","provider":"library","parent_id":"19","offset":20,"limit":20})
    request.feed([media(i,"track") for i in range(15)])
    request.feed([media(i,"track") for i in range(15,50)])
    result=request.response()["result"]
    assert len(request.items)==20 and [row["id"] for row in result["items"]]==[str(i) for i in range(20,40)]
    assert result["has_more"]


def test_empty_unavailable_and_folder_identity():
    request=BrowseRequest("b3",{"kind":"folders","provider":"nav","path":"nav://"})
    request.feed([{"media_type":"folder","path":"nav://artists/a","name":"Artists","provider":"nav"},
                  {"item_id":"missing","media_type":"track","provider":"nav","name":"Unavailable","available":False}])
    rows=request.response()["result"]["items"]
    assert rows[0]["available"] and len(rows[0]["id"])==64 and rows[0]["uri"]=="nav://artists/a"
    assert not rows[1]["available"]
    empty=BrowseRequest("empty",{"kind":"albums","provider":"nav"}); empty.feed([])
    assert empty.response()["result"]["items"]==[] and not empty.response()["result"]["has_more"]


def test_gateway_mocked_ma_browse_end_to_end(monkeypatch):
    spec=importlib.util.spec_from_file_location("browse_gateway",Path(__file__).resolve().parents[1]/"app.py")
    gateway=importlib.util.module_from_spec(spec); spec.loader.exec_module(gateway)
    monkeypatch.setattr(gateway,"DEVICE_TOKEN","test-device")
    async def run():
        observed=[]
        async def ma_handler(request):
            socket=web.WebSocketResponse(); await socket.prepare(request)
            async for message in socket:
                if message.type!=WSMsgType.TEXT: continue
                data=message.json(); name=data["command"]; mid=data["message_id"]
                if name=="auth/login": result={"success":True,"access_token":"appliance-token"}
                elif name=="auth": result={}
                else:
                    observed.append(data)
                    if name=="music/albums/library_items": result=[media(i) for i in range(21)]
                    else:
                        await socket.send_json({"message_id":mid,"result":[media(1,"track")],"partial":True})
                        result=[media(2,"track")]
                await socket.send_json({"message_id":mid,"result":result})
            return socket
        ma=web.Application(); ma.router.add_get("/ws",ma_handler)
        async with TestServer(ma) as upstream:
            monkeypatch.setattr(gateway,"MA_URL",str(upstream.make_url("/ws")))
            app=web.Application(); app.router.add_get("/device/ws",gateway.websocket_handler)
            async with TestClient(TestServer(app)) as client:
                socket=await client.ws_connect("/device/ws",headers={"X-Device-Token":"test-device"})
                assert (await socket.receive_json())["event"]=="gateway/ready"
                assert (await socket.receive_json())["data"]["browse"]
                await socket.send_json({"message_id":"b1","command":"controller/browse","args":{"kind":"albums","provider":"nav"}})
                response=await socket.receive_json()
                assert response["message_id"]=="b1" and len(response["result"]["items"])==20 and response["result"]["has_more"]
                assert observed[0]["command"]=="music/albums/library_items" and observed[0]["args"]["provider"]=="nav"
                await socket.send_json({"message_id":"b2","command":"controller/browse","args":{"kind":"album_tracks","provider":"library","parent_id":"19"}})
                response=await socket.receive_json()
                assert [row["id"] for row in response["result"]["items"]]==["1","2"]
                await socket.close()
    asyncio.run(run())
