"""LAN-only, command-limited Music Assistant gateway for the household controller."""
import asyncio
import json
import logging
import os
import hmac
import time
from aiohttp import ClientSession, ClientTimeout, WSMsgType, web
from artwork import ArtworkService
from browse import BrowseRequest, SearchRequest, validate_browse, validate_search

logging.basicConfig(level=logging.INFO)
LOG = logging.getLogger("music-controller-gateway")
MA_URL = os.environ.get("MA_WS_URL", "http://host.docker.internal:8095/ws")
USERNAME = os.environ.get("MA_USERNAME", "")
PASSWORD = os.environ.get("MA_PASSWORD", "")
CONTROLLER_ORIGIN = os.environ.get("CONTROLLER_ORIGIN", "")
DEVICE_TOKEN = os.environ.get("CONTROLLER_DEVICE_TOKEN", "")
# Exact supported MA commands. Never permit auth/config/admin or arbitrary methods.
DEVICE_CAPABILITIES = {"seek": True, "artwork_rgb565": True, "queue_item_play": True, "browse": True, "search": True}
ARTWORK = ArtworkService(MA_URL)
ARG_RULES = {
    "players/all": {},
    "player_queues/all": {},
    "player_queues/items": {"queue_id": str, "limit": int, "offset": int},
    "players/cmd/play": {"player_id": str},
    "players/cmd/pause": {"player_id": str},
    "players/cmd/stop": {"player_id": str},
    "players/cmd/next": {"player_id": str},
    "players/cmd/previous": {"player_id": str},
    "players/cmd/volume_set": {"player_id": str, "volume_level": int},
    "player_queues/play_index": {"queue_id": str, "index": (int, str)},
    "player_queues/seek": {"queue_id": str, "position": int},
    "providers": {},
    "music/albums": {"limit": int, "offset": int},
    "music/artists": {"limit": int, "offset": int},
    "music/playlists": {"limit": int, "offset": int},
    "music/album_tracks": {"item_id": str, "provider_instance_id_or_domain": str},
    "player_queues/play_media": {"queue_id": str, "media": (str, dict), "option": str},
}
REQUIRED = {
    "player_queues/items": {"queue_id"},
    "players/cmd/play": {"player_id"},
    "players/cmd/pause": {"player_id"},
    "players/cmd/stop": {"player_id"},
    "players/cmd/next": {"player_id"},
    "players/cmd/previous": {"player_id"},
    "players/cmd/volume_set": {"player_id", "volume_level"},
    "player_queues/play_index": {"queue_id", "index"},
    "player_queues/seek": {"queue_id", "position"},
    "music/album_tracks": {"item_id", "provider_instance_id_or_domain"},
    "player_queues/play_media": {"queue_id", "media"},
}
def validate_command(name, args):
    if name=="controller/browse": return validate_browse(args)
    if name=="controller/search": return validate_search(args)
    spec = ARG_RULES.get(name)
    if spec is None or not isinstance(args, dict) or set(args) - set(spec):
        raise ValueError("Command or argument not allowed")
    if not REQUIRED.get(name, set()).issubset(args):
        raise ValueError("Missing required argument")
    for key, value in args.items():
        expected = spec[key]
        if not isinstance(value, expected) or isinstance(value, bool):
            raise ValueError("Invalid argument type")
        if isinstance(value, str) and (not value or len(value) > 512):
            raise ValueError("Invalid string argument")
        if isinstance(value, dict) and len(json.dumps(value)) > 2048:
            raise ValueError("Media payload too large")
    if name == "players/cmd/volume_set" and not 0 <= args["volume_level"] <= 100:
        raise ValueError("Invalid volume")
    if name == "player_queues/seek" and not 0 <= args["position"] <= 604800:
        raise ValueError("Invalid seek position")
    if "limit" in args and not 1 <= args["limit"] <= 100:
        raise ValueError("Invalid page size")
    for key in ("offset", "index"):
        if key in args and isinstance(args[key], int) and not 0 <= args[key] <= 100000:
            raise ValueError("Invalid position")
    return args
MAX_MESSAGE = 8192


async def handshake(upstream):
    """Authenticate upstream before forwarding any application messages."""
    async def command(message_id, name, args):
        await upstream.send_json({"message_id": message_id, "command": name, "args": args})
        while True:
            msg = await upstream.receive(timeout=15)
            if msg.type != WSMsgType.TEXT:
                raise RuntimeError("Music Assistant closed connection during authentication")
            data = json.loads(msg.data)
            if str(data.get("message_id")) != message_id:
                continue  # Initial server-info and asynchronous events.
            if data.get("error") or data.get("error_code"):
                raise RuntimeError("Music Assistant authentication rejected")
            return data.get("result") or {}

    login = await command("__gateway_login", "auth/login", {
        "username": USERNAME, "password": PASSWORD, "device_name": "Music Controller Gateway"
    })
    token = login.get("access_token") or login.get("token")
    if not login.get("success") or not token:
        raise RuntimeError("Music Assistant appliance account login failed")
    await command("__gateway_auth", "auth", {"token": token})
    ARTWORK.token = token


async def websocket_handler(request):
    # Browser: strict Origin. Native device: independently provisioned bearer.
    if request.path == "/device/ws":
        supplied = request.headers.get("X-Device-Token", "")
        if not DEVICE_TOKEN or not hmac.compare_digest(supplied, DEVICE_TOKEN):
            raise web.HTTPForbidden(text="Invalid device credential")
    elif not CONTROLLER_ORIGIN or request.headers.get("Origin", "") != CONTROLLER_ORIGIN:
        raise web.HTTPForbidden(text="Invalid controller origin")
    browser = web.WebSocketResponse(max_msg_size=MAX_MESSAGE, heartbeat=30)
    await browser.prepare(request)
    try:
        async with ClientSession(timeout=ClientTimeout(total=None)) as session:
            async with session.ws_connect(MA_URL, heartbeat=30, max_msg_size=2**20) as upstream:
                await handshake(upstream)
                await browser.send_json({"event": "gateway/ready"})
                await browser.send_json({"event": "gateway/capabilities", "data": DEVICE_CAPABILITIES})
                browse_requests={}
                browse_sequence=0
                async def to_upstream():
                    nonlocal browse_sequence
                    async for msg in browser:
                        if msg.type != WSMsgType.TEXT:
                            continue
                        data=None
                        try:
                            data = json.loads(msg.data)
                            if not isinstance(data, dict):
                                raise ValueError("Expected request object")
                            name = data.get("command")
                            message_id = data.get("message_id")
                            if not isinstance(message_id, (str, int)):
                                raise ValueError("Invalid message ID")
                            args = validate_command(name, data.get("args", {}))
                            if name in {"controller/browse", "controller/search"}:
                                for key in list(browse_requests):
                                    if time.monotonic()-browse_requests[key].created>15: browse_requests.pop(key)
                                if len(browse_requests)>=8: raise ValueError("Too many browse requests")
                                browse_sequence+=1
                                upstream_id=f"__gateway_browse_{browse_sequence}"
                                adapter = SearchRequest if name=="controller/search" else BrowseRequest
                                browse=adapter(str(message_id),args); browse_requests[upstream_id]=browse
                                await upstream.send_json({"message_id":upstream_id,"command":browse.command,"args":browse.args})
                            else:
                                await upstream.send_json({
                                    "message_id": str(message_id), "command": name, "args": args
                                })
                        except (ValueError, TypeError):
                            await browser.send_json({"message_id": data.get("message_id") if isinstance(data, dict) else None, "error": "Invalid or disallowed request"})
                async def to_browser():
                    async for msg in upstream:
                        if msg.type == WSMsgType.TEXT:
                            try:
                                data = json.loads(msg.data)
                            except ValueError:
                                continue
                            upstream_id=str(data.get("message_id", ""))
                            if upstream_id in browse_requests:
                                browse=browse_requests[upstream_id]
                                if data.get("error") or data.get("error_code"):
                                    if isinstance(browse,SearchRequest) and browse.retry_older_server(data):
                                        await upstream.send_json({"message_id":upstream_id,"command":browse.command,"args":browse.args})
                                        continue
                                    await browser.send_json({"message_id":browse.message_id,"error":"Music source unavailable"})
                                    browse_requests.pop(upstream_id); continue
                                try: browse.feed(data.get("result"))
                                except ValueError:
                                    await browser.send_json({"message_id":browse.message_id,"error":"Invalid catalogue response"})
                                    browse_requests.pop(upstream_id); continue
                                if not data.get("partial"):
                                    await browser.send_json(browse.response()); browse_requests.pop(upstream_id)
                                continue
                            # Do not forward server metadata or authentication responses.
                            if str(data.get("message_id", "")).startswith("__gateway_"):
                                continue
                            if data.get("event") or data.get("message_id") is not None:
                                await browser.send_json(ARTWORK.annotate(data))
                tasks = [asyncio.create_task(to_upstream()), asyncio.create_task(to_browser())]
                done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
                for task in pending:
                    task.cancel()
                await asyncio.gather(*pending, return_exceptions=True)
                for task in done:
                    if task.exception():
                        raise task.exception()
    except Exception:
        LOG.exception("Music Assistant gateway session failed (no credentials logged)")
        if not browser.closed:
            await browser.send_json({"event": "gateway/error", "data": {"message": "Music Assistant connection failed"}})
    finally:
        await browser.close()
    return browser


async def artwork_handler(request):
    supplied = request.headers.get("X-Device-Token", "")
    if not DEVICE_TOKEN or not hmac.compare_digest(supplied, DEVICE_TOKEN):
        raise web.HTTPForbidden(text="Invalid device credential")
    key = request.match_info["art_id"]
    if key != "generic" and (len(key) != 64 or any(c not in "0123456789abcdef" for c in key)):
        raise web.HTTPBadRequest(text="Invalid artwork identity")
    packet, fallback = await ARTWORK.get(key)
    return web.Response(body=packet, content_type="application/octet-stream",
                        headers={"X-Artwork-Fallback": "1" if fallback else "0", "Cache-Control": "private, max-age=30"})


async def health(_):
    return web.json_response({"status": "ok" if USERNAME and PASSWORD else "unconfigured"},
                             status=200 if USERNAME and PASSWORD else 503)


def create_app():
    app = web.Application(client_max_size=MAX_MESSAGE)
    app.router.add_get("/ws", websocket_handler)
    app.router.add_get("/device/ws", websocket_handler)
    app.router.add_get("/health", health)
    app.router.add_get("/device/artwork/{art_id}", artwork_handler)
    return app


if __name__ == "__main__":
    if not USERNAME or not PASSWORD:
        raise SystemExit("MA_USERNAME and MA_PASSWORD are required")
    app = create_app()
    web.run_app(app, host="0.0.0.0", port=8080, access_log=None)
