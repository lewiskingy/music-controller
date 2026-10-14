import asyncio
import importlib.util
from pathlib import Path
import sys
import pytest
from aiohttp import web, WSMsgType
from aiohttp.test_utils import TestClient, TestServer
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from browse import SearchRequest, validate_search
from test_browse import media

@pytest.mark.parametrize("kind,media_type",[("tracks","track"),("albums","album"),("artists","artist"),("playlists","playlist")])
def test_search_is_single_provider_and_type(kind,media_type):
    request=SearchRequest("s1",{"query":" Adele ","provider":"navidrome","kind":kind})
    assert request.command=="music/search"
    assert request.args=={"search_query":"adele","providers":["navidrome"],"media_types":[media_type],"limit":21}
    request.feed({kind:[media(i,media_type) for i in range(21)],"other_metadata":"omitted"})
    result=request.response()["result"]
    assert len(result["items"])==20 and result["has_more"]
    assert result["items"][0]["kind"]==media_type and "metadata" not in result["items"][0]

@pytest.mark.parametrize("overrides",[{"query":""},{"query":" \t "},{"query":"x"*193},{"query":"é"*97},
    {"query":None},{"provider":""},{"provider":["nav","spotify"]},{"provider":"é"*48},
    {"kind":"all"},{"kind":[]},{"admin":True},{"offset":20}])
def test_search_rejects_invalid_or_multi_provider_arguments(overrides):
    with pytest.raises(ValueError): validate_search({"query":"Adele","provider":"nav","kind":"tracks",**overrides})

def test_empty_and_partial_search_results_are_bounded():
    request=SearchRequest("s2",{"query":"x","provider":"nav","kind":"tracks"})
    request.feed({"tracks":[]}); assert request.response()["result"]["items"]==[]
    request.feed({"tracks":[media(i,"track") for i in range(15)]})
    request.feed({"tracks":[media(i,"track") for i in range(15,50)]})
    assert len(request.items)==20 and request.response()["result"]["has_more"]
    with pytest.raises(ValueError): request.feed({"tracks":{}})

def test_search_utf8_metadata_fits_device_buffers():
    request=SearchRequest("s3",{"query":"é"*96,"provider":"nav","kind":"tracks"})
    item=media(1,"track"); item["name"]="é"*100; item["artists"][0]["name"]="♪"*100
    request.feed({"tracks":[item]})
    row=request.items[0]
    assert len(row["name"].encode())<=159 and len(row["subtitle"].encode())<=159

def test_gateway_search_and_playback_against_mocked_ma(monkeypatch):
    spec=importlib.util.spec_from_file_location("search_gateway",Path(__file__).resolve().parents[1]/"app.py")
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
                    if name=="music/albums/library_items": result=[media(1,"album")]
                    elif name=="music/search":
                        query=data["args"]["search_query"]
                        if query=="older":
                            await socket.send_json({"message_id":mid,"error_code":999,"details":"unexpected keyword argument 'providers'"}); continue
                        if query=="error":
                            await socket.send_json({"message_id":mid,"error_code":42}); continue
                        if query=="none": result={"tracks":[]}
                        else:
                            await socket.send_json({"message_id":mid,"result":{"tracks":[media(1,"track")]},"partial":True})
                            result={"tracks":[media(i,"track") for i in range(2,23)]}
                    else: result=None
                await socket.send_json({"message_id":mid,"result":result})
            return socket
        ma=web.Application(); ma.router.add_get("/ws",ma_handler)
        async with TestServer(ma) as upstream:
            monkeypatch.setattr(gateway,"MA_URL",str(upstream.make_url("/ws")))
            app=web.Application(); app.router.add_get("/device/ws",gateway.websocket_handler)
            async with TestClient(TestServer(app)) as client:
                socket=await client.ws_connect("/device/ws",headers={"X-Device-Token":"test-device"})
                assert (await socket.receive_json())["event"]=="gateway/ready"
                assert (await socket.receive_json())["data"]["search"]
                for mid,query in [("s1","Adele"),("s2","none"),("s3","error")]:
                    await socket.send_json({"message_id":mid,"command":"controller/search","args":{"query":query,"provider":"nav","kind":"tracks"}})
                    response=await socket.receive_json(); assert response["message_id"]==mid
                    if query.casefold()=="adele":
                        assert len(response["result"]["items"])==20 and response["result"]["has_more"]
                        assert response["result"]["items"][0]["uri"]=="library://track/1"
                    elif query=="none": assert response["result"]["items"]==[]
                    else: assert response["error"]=="Music source unavailable"
                assert observed[0]["args"]["providers"]==["nav"]
                await socket.send_json({"message_id":"t1","command":"player_queues/play_media","args":{"queue_id":"room-a","media":"library://track/1","option":"replace"}})
                assert (await socket.receive_json())["message_id"]=="t1"
                assert observed[-1]["args"]=={"queue_id":"room-a","media":"library://track/1","option":"replace"}
                await socket.send_json({"message_id":"s4","command":"controller/search","args":{"query":"older","provider":"nav","kind":"albums"}})
                response=await socket.receive_json()
                assert response["message_id"]=="s4" and response["result"]["scope"]=="library"
                assert observed[-1]["command"]=="music/albums/library_items" and observed[-1]["args"]["provider"]=="nav"
                await socket.close()
    asyncio.run(run())


def test_older_ma_fallback_stays_within_selected_library_provider():
    request=SearchRequest("s4",{"query":"Adele","provider":"nav","kind":"albums"})
    assert not request.retry_older_server({"details":"Provider unavailable"})
    assert request.retry_older_server({"details":"search() got an unexpected keyword argument 'providers'"})
    assert request.command=="music/albums/library_items"
    assert request.args=={"search":"adele","provider":"nav","limit":21,"offset":0}
    assert not request.retry_older_server({"details":"unexpected providers"})
    request.feed([media(1,"album")])
    assert request.response()["result"]["scope"]=="library"


@pytest.mark.parametrize("query",["Adele","ADELE","adele","aDeLe"])
def test_search_normalizes_case_without_changing_symbols(query):
    request=SearchRequest("case",{"query":query+" $ # £ 1975","provider":"nav","kind":"tracks"})
    assert request.args["search_query"]=="adele $ # £ 1975"
