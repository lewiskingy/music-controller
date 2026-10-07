# Live Now Playing prototype

This milestone replaces sample metadata with real Music Assistant player and queue state.

## Connection

The prototype uses a same-origin WebSocket at `/controller/ma/ws`. Atlas Caddy must route this LAN-only endpoint to Music Assistant's `/ws` on port 8095. Artwork uses the similarly restricted `/controller/ma/image` route. No public MA API route is introduced.

Users sign in with their **Music Assistant** username and password. Credentials are sent to Music Assistant over the WebSocket authentication command, and are not stored in the public repository, localStorage or a config file. A token is held only in the current connection. Browser reload requires signing in again.

## Scope

- Select an available Music Assistant player.
- Show track, artist, album and artwork from the selected player's current queue/media.
- Refresh state on player/queue events.
- Controls are intentionally disabled until playback command semantics have been validated, especially [Sonos Pause #587](https://github.com/lewiskingy/atlas/issues/587).
- Browse, search and queue navigation are future milestones.

## Deployment

Merge the Atlas Caddy proxy PR first, then merge this project PR. Publish the next versioned release (e.g. `v0.2.0`), change Atlas's pinned release version and run `git-deploy.sh`. Do not overwrite `v0.1.0`.

Test on home Wi-Fi at `https://music.theflat.me.uk/controller/`. Verify that remote access to both API paths returns HTTP 403. If the client shows an authentication or data-shape error, collect the browser console and Music Assistant logs to reconcile the installed MA version's protocol.

## Security

No MA administrator token is injected by Caddy. MA validates each login. Caddy's LAN restriction protects both the static application and WebSocket/image endpoints. Avoid granting the controller user administrator privileges.
