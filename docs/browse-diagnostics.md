# Browse API error — diagnostic follow-up

The first single-focus Browse screenshot showed `Browse failed: API error`
while live Now Playing and Queue worked. This means the gateway connection
and basic queue reads are functional, but it does **not** prove that the
catalogue method names, arguments or Music Assistant permissions are correct.

This change removes the duplicate header Back button and improves the
client's handling of structured API errors. It does not claim to have
resolved the underlying catalogue API error.

## Reproduction after deploying the next release

1. Open `/controller/` on the LAN, enter Browse, then Albums.
2. Note the full status text (without any credentials or tokens).
3. Repeat with Artists and Playlists.
4. Inspect `docker logs --tail 100 music-controller-gateway` for exceptions.
5. Confirm the installed Music Assistant version and supported catalogue
   WebSocket method signatures before changing gateway command mappings.
6. Verify the appliance account has permission to browse the library.

## UX contract

Exactly one context-sensitive Back button appears on each secondary screen.
On an album drill-down it returns to the album list; otherwise it returns to
Now Playing. No duplicate header navigation, no document scrolling and
one active view within 800 × 480.
