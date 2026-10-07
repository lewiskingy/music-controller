# Household controller — live playback, queue and catalogue

## Tested integration baseline (Atlas, October 2026)

The deployed read-only Now Playing prototype successfully displayed live Music
Assistant data through the following path:

```text
LAN browser -> Caddy /controller/ma/ws -> Atlas appliance gateway
           -> Music Assistant host port 8095 -> players and queues
```

The gateway authenticates using `/opt/atlas-secrets/music-controller.env`
managed on Atlas; the browser does not receive credentials. The gateway was
initially unable to reach MA because Atlas UFW blocked Docker-to-host TCP 8095.
Allowing the existing Docker subnet to reach that port resolved connectivity.
Port 8097 remains separately required for Sonos/Cast to fetch audio streams.
These are Atlas deployment concerns, not settings for this public repository.

## This iteration (requires live validation)

- Transport: play, pause, explicit stop, next, previous and volume for the
  selected player. Buttons disable during requests and errors are surfaced.
- Sonos: **Pause is disabled while playing** pending investigation of
  [Atlas #587](https://github.com/lewiskingy/atlas/issues/587); Stop remains
  available. No implicit substitution of Stop for Pause.
- Queue: show current queue items and select an index.
- Browse: paginated albums, artists and playlists; album track drill-down.
  Playback requires a selected player and a playable Music Assistant URI.
- Live state: Music Assistant remains the source of truth. UI refreshes on
  player/queue events rather than assuming commands succeeded.
- No new media catalogue, database or queue logic on Atlas.

The exact Music Assistant command signatures and catalogue response shapes
must be validated against the deployed server before release promotion. The
implementation is a functional integration candidate, not a claim of successful
live testing for every provider/player.

## Security boundary

Caddy restricts controller paths to the home LAN. Atlas gateway authenticates
upstream with a dedicated Music Assistant account and validates each allowed
command and its arguments; arbitrary API commands are not forwarded. Because
the controller intentionally has no household login, anyone on the permitted
LAN can control playback. Keep the gateway private, do not expose its container
port or the MA server directly to WAN, and never commit secrets.

## Release and deployment

1. Merge the controller and companion Atlas gateway PR after CI passes.
2. Publish a **new** semver tag/release with the verified static artifacts.
3. Update `deploy/music-controller.version` in Atlas.
4. Deploy with `git-deploy.sh`.
5. Test Now Playing, Stop, play, skip, volume, queue selection, album drill-down,
   and external state changes on a non-Sonos and a Sonos player.
6. Confirm Navidrome public root is unaffected and protected controller routes
   remain inaccessible from mobile data.

## Further work

Search across providers, artist-to-album drill-down, favourites, device
capability-specific controls, and native ESP32 LVGL port are later milestones.
