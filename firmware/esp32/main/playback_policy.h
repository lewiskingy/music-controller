#pragma once
#include <stdbool.h>

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
