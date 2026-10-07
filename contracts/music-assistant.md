# Music Assistant integration contract (spike)

This document defines **desired capabilities**, not confirmed endpoint names. Validate against the deployed Music Assistant version and its API documentation before implementation.

## Read operations
- Enumerate available players, groups and capabilities.
- Obtain selected player state and now-playing metadata.
- Read active queue and upcoming items.
- Browse artists, albums, tracks, playlists and favourites.
- Search across available music providers.
- Retrieve resized artwork suitable for 800×480 and ESP32 memory limits.

## Commands
- Select active player/room.
- Play album or track on selected player.
- Play/pause or stop according to actual supported player semantics.
- Next/previous, set volume and modify queue.

## Events and recovery
- Subscribe to player and queue changes.
- Hydrate from an initial snapshot before applying deltas.
- Reconnect with bounded backoff and resynchronise after disconnect.
- Handle unavailable players, expired credentials, missing artwork and unreachable MA.
- Disable unsupported controls rather than guessing.

## Contract boundaries

Keep API protocol mapping separate from view state. Never create a second catalogue index or media queue. Authentication secrets must be configured at runtime and never committed.

## Spike acceptance

- [ ] Identify and record installed MA version and API docs.
- [ ] Demonstrate authenticated player list and state.
- [ ] Demonstrate queue and search/browse queries.
- [ ] Demonstrate artwork retrieval and suitable resizing.
- [ ] Demonstrate play, pause/stop, skip and volume on a test player.
- [ ] Demonstrate real-time events and recovery after a network interruption.
- [ ] Decide whether the ESP32 can connect directly or requires a narrow Atlas adapter.
- [ ] Document Sonos pause anomaly separately from general transport success.
