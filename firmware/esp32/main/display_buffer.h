#pragma once

#include <stdint.h>
#include <string.h>

/* Logical portrait pixels -> native landscape rows. Keep both PSRAM reads and
 * writes contiguous by transposing a small tile in internal task-stack memory.
 * The BSP's per-pixel column writes otherwise churn the external-memory cache. */
typedef struct { int x1, y1, x2, y2; } display_rect_t;
#define DISPLAY_DAMAGE_MAX 32
#define DISPLAY_RETIRE_EVENTS 2

typedef struct {
    display_rect_t areas[DISPLAY_DAMAGE_MAX];
    unsigned count;
} display_damage_t;

static inline void display_damage_add(display_damage_t *damage, display_rect_t area,
                                      int width, int height) {
    if (damage->count == DISPLAY_DAMAGE_MAX ||
        (area.x1 == 0 && area.y1 == 0 && area.x2 == width-1 && area.y2 == height-1)) {
        damage->count = 1;
        damage->areas[0] = (display_rect_t){0, 0, width-1, height-1};
    } else {
        damage->areas[damage->count++] = area;
    }
}

static inline void display_rotate_90(const uint16_t *source, uint16_t *target,
                                     display_rect_t area, int width, int height) {
    uint16_t tile[16][16];
    for (int y = area.y1; y <= area.y2; y += 16) {
        int rows = area.y2-y+1 < 16 ? area.y2-y+1 : 16;
        for (int x = area.x1; x <= area.x2; x += 16) {
            int columns = area.x2-x+1 < 16 ? area.x2-x+1 : 16;
            for (int row = 0; row < rows; ++row)
                memcpy(tile[row], source+(y+row)*width+x, columns*sizeof(uint16_t));
            for (int column = 0; column < columns; ++column) {
                uint16_t *out = target+(width-1-x-column)*height+y;
                for (int row = 0; row < rows; ++row) out[row] = tile[row][column];
            }
        }
    }
}

/* After retirement, sync only the damaged native rows into the other panel
 * buffer so a future incremental update starts from the same complete image. */
static inline void display_sync_90(const uint16_t *front, uint16_t *retired,
                                   display_rect_t area, int width, int height) {
    for (int x = area.x1; x <= area.x2; ++x) {
        unsigned offset = (width-1-x)*height+area.y1;
        memcpy(retired+offset, front+offset, (area.y2-area.y1+1)*sizeof(uint16_t));
    }
}
