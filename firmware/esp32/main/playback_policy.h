#pragma once
#include <stdbool.h>
#include <string.h>

/* Pure command policy: UI state never invents a successful playback change. */
static inline const char *playback_toggle_command(bool playing) {
    return playing ? "players/cmd/stop" : "players/cmd/play";
}
static inline int playback_volume_step(int volume, bool up) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    return up ? (volume >= 100 ? 100 : ((volume / 5) + 1) * 5)
              : (volume <= 0 ? 0 : ((volume - 1) / 5) * 5);
}

/* One independent pending slot for each control family. */
enum { PLAYBACK_TRANSPORT, PLAYBACK_VOLUME, PLAYBACK_SEEK, PLAYBACK_GROUPS };
static inline bool playback_seek_allowed(int duration, bool live, bool active,
                                          bool allow_seek, bool gateway, bool identity) {
    return duration > 0 && duration <= 604800 && !live && active && allow_seek && gateway && identity;
}
static inline int playback_seek_position(int position, int duration) {
    if (duration <= 0 || position < 0) return 0;
    /* Seeking to exactly the end can advance the queue; keep the same track. */
    return position >= duration ? duration - 1 : position;
}
static inline bool playback_target_matches(const char *player, const char *queue, const char *item,
                                           const char *expected_player, const char *expected_queue,
                                           const char *expected_item, bool seeking) {
    return player[0] && !strcmp(player, expected_player) &&
           (!seeking || (queue[0] && item[0] && !strcmp(queue, expected_queue) && !strcmp(item, expected_item)));
}
