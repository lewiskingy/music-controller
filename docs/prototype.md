# Browser prototype

The static browser implementation validates the 800×480 touch interaction model and can control Music Assistant through the gateway. It is not ESP32 firmware.

## Layout inspection

From the repository root:

```sh
python3 -m http.server 8081 --directory prototype
```

Open `http://localhost:8081` at an 800×480 viewport. Live API requests will fail without same-origin `/ma/ws` and artwork routing; this mode is for layout inspection. No account password belongs in the JavaScript or browser storage.

## Live operation

Use the [gateway's optional frontend](../gateway/README.md) or equivalent routes. Open `https://YOUR_HOST/controller/`. The JavaScript resolves `./ma/ws` and `./ma/image` relative to that directory, and the gateway checks the exact browser origin. Include the trailing slash.

Select a player, inspect Now Playing or Queue, browse catalogue items and control playback. View navigation replaces the focused panel, and bounded lists scroll within fixed device dimensions. The Search view remains a placeholder on current main. Sonos Pause is disabled while playing; use Stop.

`music-assistant.js` owns request IDs, pending replies and the protocol adapter; `app.js` owns UI state and actions, while `styles.css` and `index.html` define presentation. Music Assistant remains the catalogue/queue authority. Changes made from other clients are reflected via events and refreshes.

## Checks and distribution

```sh
python -m pytest tests/prototype -q
node --input-type=module --check < prototype/app.js
node --input-type=module --check < prototype/music-assistant.js
```

The dedicated prototype workflow runs these checks for prototype/contract changes. Tagged `vX.Y.Z` releases create a deterministic `.tar.gz` of `prototype/` plus SHA-256 checksum; they do not package firmware or gateway code. Serve the extracted files behind the same routes as above. Ordinary commits do not publish releases.
