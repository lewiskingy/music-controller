# Music Assistant gateway contract

This is the implemented control/metadata boundary shared by the browser and native device. `gateway/app.py` is the executable allowlist. Additional desired capabilities must be verified against the installed Music Assistant API before adding commands.

## Authentication and paths

| Client | Gateway path | Admission check |
| --- | --- | --- |
| ESP32 | /device/ws | `X-Device-Token` equals configured nonempty `CONTROLLER_DEVICE_TOKEN` |
| Browser | /ws | `Origin` equals configured nonempty `CONTROLLER_ORIGIN` |

The example TLS frontend exposes these at `/controller/device/ws` and `/controller/ma/ws`. Admission happens before connecting to Music Assistant. The gateway logs in upstream with `MA_USERNAME`/`MA_PASSWORD`, then authenticates with the returned token. Upstream credentials/tokens are not forwarded. Clients wait for `{"event":"gateway/ready"}` before commands. The next `gateway/capabilities` event advertises `seek`, `artwork_rgb565`, `queue_item_play`, `browse` and `search` as true; older firmware may ignore it. Upstream failure yields `gateway/error` and closure.

## Requests and replies

```json
{"message_id":"1","command":"players/all","args":{}}
```

Requests require a string/integer message ID, an allowlisted command and an object of allowed typed arguments. The relay stringifies IDs and forwards matching replies plus asynchronous Music Assistant events. `__gateway_` IDs are reserved for server-side authentication. Unsupported/malformed requests receive `error: "Invalid or disallowed request"` without being forwarded.

Client messages are capped at 8 KiB; upstream messages at 1 MiB. Current firmware reassembles responses within a separate 128 KiB bound. Pagination is required for large catalogues/queues.

## Current allowlist

| Commands | Arguments (required unless marked optional) |
| --- | --- |
| players/all, player_queues/all, providers | None |
| players/cmd/play, pause, stop, next, previous | player_id |
| players/cmd/volume_set | player_id, volume_level (0–100) |
| player_queues/items | queue_id; optional limit (1–100), offset (0–100000) |
| player_queues/play_index | queue_id, index (integer 0–100000 or nonempty stable queue-item ID up to 512 characters) |
| player_queues/seek | queue_id, position (integer seconds 0–604800) |
| music/albums, music/artists, music/playlists | Optional limit (1–100), offset (0–100000) |
| music/album_tracks | item_id, provider_instance_id_or_domain |
| player_queues/play_media | queue_id, media (string or object); optional option |

IDs/options are nonempty strings up to 512 characters; media objects are limited to 2048 serialised characters. Unknown arguments, invalid types (including booleans as integers), out-of-range numbers and missing required arguments are rejected. `auth/*`, configuration and arbitrary administrative commands are unavailable to clients.

## State, recovery and remaining capabilities

Music Assistant owns player/queue state. Clients fetch initial snapshots and resynchronise after reconnect; other apps' changes remain authoritative. Device player/provider preferences persist locally in NVS. Sonos Pause is disabled while playing due to observed queue hopping; do not infer universal player behaviour from that workaround.

The gateway now includes the following adapters and artwork endpoint from Atlas PR #614. The original main-branch firmware does not use them; the pending portrait firmware PR #20 does. Browser artwork remains a separate frontend `/image` proxy with placeholder fallback. Audio streaming is outside this contract.

## Compact Browse adapter

`controller/browse` accepts `kind` (albums, artists, playlists, artist_albums, album_tracks, playlist_tracks, folders), one `provider`, optional `source`, a detail `parent_id` or folder `path`, and integer `offset` (0–100000) / `limit` (1–20). Unknown arguments/kinds and missing detail identities are rejected. Library requests use provider filters and one extra pagination sentinel; detail/folder requests map to fixed upstream commands.

The original client message ID receives `{items, offset, has_more, kind}`. Each row contains bounded `id`, `provider`, `uri`, `name`, `subtitle`, `kind`, `available`; upstream metadata/images are omitted. Partial upstream responses accumulate at most 20 output rows. Provider/library identities remain available for drill-down, independently of output-player selection. Playback uses existing `player_queues/play_media` with captured queue ID, stable URI and `option: replace`.

## Single-provider Search adapter

`controller/search` arguments are exactly `query` (nonblank, at most 192 UTF-8 bytes), `provider` (one nonempty ID, at most 95 UTF-8 bytes), and `kind` (tracks/albums/artists/playlists). It strips and Unicode-casefolds the query while retaining symbols, then uses `music/search` with `search_query`, `providers: [id]`, one `media_types` value and `limit: 21`. Results reuse the compact Browse rows, at most 20 items plus `has_more`, and add `scope: provider`.

Only an explicit upstream rejection of the `providers` argument permits one compatibility retry using `music/{kind}/library_items` with the same provider, `search`, `limit: 21`, `offset: 0`. Results declare `scope: library` so the client can label the limitation. Other errors remain failures; there is no all-provider fallback.

Adapters use private upstream message IDs; their replies retain the original client ID. No more than eight pending adapter requests are allowed per session; expired requests are pruned before admission. Unknown/expired private replies are dropped. Firmware must discard obsolete request generations and capture player/queue targets before playback.

## Authenticated device artwork

Queue `current_item` objects receive `controller_art_id`: an opaque SHA-256 descriptor identity or `generic`. A device requests `GET /device/artwork/{art_id}` using the same `X-Device-Token`; admission precedes image work. The example frontend exposes `/controller/device/artwork/{art_id}`. IDs are registered from Music Assistant metadata, not arbitrary client-provided URLs.

The gateway fetches through MA's authenticated image proxy (`proxy_id` preferred, path/provider compatibility otherwise), with an eight-second timeout, no redirects, at most 2 MiB encoded bytes and four-million decoded pixels. It centre-crops/resizes to 288×288. Missing, unregistered, broken or oversized images return a generic record cover with HTTP 200 and `X-Artwork-Fallback: 1`. Successful covers cache for an hour, failing sources for 30 seconds; at most 32 converted covers, 256 source descriptors and two concurrent conversions are retained.

Packets are exactly **165,896 bytes**, `application/octet-stream`: `MCAR` (four bytes), little-endian uint16 width=288 and height=288, then 82,944 little-endian RGB565 pixels in row-major order. No NVS/token changes are required to enable these endpoints. Live provider/physical-device acceptance remains pending despite mocked tests.
