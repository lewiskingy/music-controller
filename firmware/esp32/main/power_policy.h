#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Caller serializes this state with the UI lock. All times are LVGL milliseconds. */
typedef struct { uint32_t last_touch, sleep_started; bool asleep, consume, sleep_pending, waiting; } power_activity_t;
/* Waiting is excluded from idle time; readiness grants a fresh full timeout.
 * Never wake an already dark screen for a network event. */
static inline void power_connection(power_activity_t *p,uint32_t now,bool ready) {
    if(!ready || p->waiting) p->last_touch=now;
    p->waiting=!ready;
    if(p->waiting) p->sleep_pending=false;
}
static inline unsigned power_timeout(unsigned battery,unsigned usb,unsigned mode) {
    return mode==2 ? usb : battery; /* Auto/unknown is battery until hardware detection exists. */
}
static inline bool power_expired(const power_activity_t *p,uint32_t now,unsigned seconds) {
    return !p->asleep && !p->waiting && !p->sleep_pending && seconds && (uint32_t)(now-p->last_touch)>=seconds*1000U;
}
/* Return true only for a new wake request; consume the whole waking gesture. */
static inline bool power_touch(power_activity_t *p,uint32_t now,bool pressed) {
    if(pressed) {
        p->last_touch=now;
        if(p->asleep || p->sleep_pending) {
            p->sleep_pending=false; p->consume=true; return true;
        }
    }
    return false;
}
static inline bool power_suppress(power_activity_t *p,bool pressed) {
    bool suppress=p->consume;
    if(!pressed) p->consume=false;
    return suppress;
}

/* Start the two-second interval after the splash has actually been presented. */
static inline void power_begin_sleep(power_activity_t *p,uint32_t presented) {
    p->sleep_pending=true; p->sleep_started=presented;
}
static inline bool power_sleep_ready(const power_activity_t *p,uint32_t now) {
    return !p->waiting && p->sleep_pending && (uint32_t)(now-p->sleep_started)>=2000U;
}
