#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "display_test_stubs.h"
#include "controller_display.h"
#include "display_buffer.h"

#define WIDTH 480
#define HEIGHT 800
#define PIXELS (WIDTH*HEIGHT)
static uint16_t fb[3][PIXELS], scanning_snapshot[PIXELS], expected[PIXELS];
static lv_disp_draw_buf_t drawing = {.buf1=fb[2]};
static lv_disp_drv_t driver = {.draw_buf=&drawing,.direct_mode=true,.hor_res=WIDTH,.ver_res=HEIGHT};
static lv_disp_t display = {.driver=&driver};
static esp_lcd_rgb_panel_event_callbacks_t events;
static test_semaphore_t semaphore;
static uint16_t *requested=fb[0], *scanning=fb[0];
static unsigned submissions, completions, ready_calls, pclk;
static bool last_area, stale_after_drain, stale_before_submit;

/* Model the bounce ISR's snapshot of cur_fb_index. An already-running callback
 * can be delivered on the other core after the task submits a new framebuffer. */
static void finish_frame(void) {
    assert(!memcmp(scanning,scanning_snapshot,sizeof(scanning_snapshot)));
    scanning=requested;
    memcpy(scanning_snapshot,scanning,sizeof(scanning_snapshot));
    completions++;
    events.on_frame_buf_complete(NULL,NULL,NULL);
}
SemaphoreHandle_t xSemaphoreCreateCounting(unsigned maximum,unsigned initial) {
    semaphore=(test_semaphore_t){initial,maximum}; return &semaphore;
}
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s,BaseType_t *wake) {
    *wake=pdFALSE;
    if(s->count==s->maximum) return pdFALSE;
    s->count++; return pdTRUE;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout) {
    if(!s->count && timeout) {
        if(stale_after_drain) {
            stale_after_drain=false;
            /* Notification for an old snapshot, without retiring scanning. */
            events.on_frame_buf_complete(NULL,NULL,NULL);
        } else finish_frame();
    }
    if(!s->count) return pdFALSE;
    s->count--; return pdTRUE;
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel,int x1,int y1,int x2,int y2,const void *pixels) {
    (void)panel;
    assert(x1==0 && y1==0 && x2==HEIGHT && y2==WIDTH);
    if(stale_before_submit) { finish_frame(); stale_before_submit=false; }
    requested=(uint16_t *)pixels; submissions++; return ESP_OK;
}
esp_err_t esp_lcd_rgb_panel_get_frame_buffer(esp_lcd_panel_handle_t panel,unsigned n,...) {
    (void)panel; assert(n==3); va_list args; va_start(args,n);
    for(unsigned i=0;i<n;i++) *va_arg(args,void **)=fb[i];
    va_end(args); return ESP_OK;
}
esp_err_t esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t panel,const esp_lcd_rgb_panel_event_callbacks_t *callbacks,void *context) {
    (void)panel;(void)context; events=*callbacks; return ESP_OK;
}
esp_err_t esp_lcd_rgb_panel_set_pclk(esp_lcd_panel_handle_t panel,unsigned hz) { (void)panel; pclk=hz; return ESP_OK; }
int64_t esp_timer_get_time(void) { static int64_t time; return time+=1000; }
bool lvgl_port_lock(int timeout) { (void)timeout; return true; }
void lvgl_port_unlock(void) {}
lv_disp_t *lv_disp_get_default(void) { return &display; }
void *lv_scr_act(void) { return NULL; }
void lv_obj_invalidate(void *screen) { (void)screen; }
bool lv_disp_flush_is_last(lv_disp_drv_t *d) { (void)d;return last_area; }
void lv_disp_flush_ready(lv_disp_drv_t *d) { (void)d;ready_calls++; }

static void set_area(lv_area_t area,unsigned seed) {
    for(int y=area.y1;y<=area.y2;y++) for(int x=area.x1;x<=area.x2;x++)
        fb[2][y*WIDTH+x]=(uint16_t)(seed+x*379+y*83);
}
static void verify_frame(void) {
    for(int y=0;y<HEIGHT;y++) for(int x=0;x<WIDTH;x++)
        expected[(WIDTH-1-x)*HEIGHT+y]=fb[2][y*WIDTH+x];
    assert(!memcmp(expected,fb[0],sizeof(expected)));
    assert(!memcmp(expected,fb[1],sizeof(expected)));
    assert(!memcmp(scanning,scanning_snapshot,sizeof(scanning_snapshot)));
}
int main(void) {
    assert(controller_display_init(NULL)==ESP_OK);
    assert(pclk==12000000 && submissions==1);
    lv_area_t full={0,0,WIDTH-1,HEIGHT-1};
    set_area(full,1); last_area=true;
    driver.flush_cb(&driver,&full,fb[2]); verify_frame();
    for(unsigned i=0;i<200;i++) {
        /* Non tile-aligned rectangles, separate invalidations, thin edges and
         * both ISR interleavings must preserve every undamaged pixel. */
        lv_area_t a={(int)(i%17), (int)(i%23), 113+(int)(i%59), 271+(int)(i%71)};
        lv_area_t b={WIDTH-1,HEIGHT-29,WIDTH-1,HEIGHT-1};
        set_area(a,i+2); set_area(b,i+9);
        unsigned before=submissions; last_area=false;
        driver.flush_cb(&driver,&a,fb[2]); assert(submissions==before);
        stale_after_drain=i%3==0; stale_before_submit=i%3==1;
        last_area=true; driver.flush_cb(&driver,&b,fb[2]);
        assert(submissions==before+1); verify_frame();
    }
    /* Overflowing damage bookkeeping safely falls back to the full canvas. */
    set_area(full,500);
    for(unsigned i=0;i<DISPLAY_DAMAGE_MAX+2;i++) {
        lv_area_t tiny={(int)i,0,(int)i,0}; last_area=false;
        driver.flush_cb(&driver,&tiny,fb[2]);
    }
    last_area=true; driver.flush_cb(&driver,&full,fb[2]); verify_frame();
    /* Exercise the overflow fallback without a final full-screen area too. */
    set_area(full,700);
    for(unsigned i=0;i<DISPLAY_DAMAGE_MAX+2;i++) {
        lv_area_t tiny={(int)i,0,(int)i,0}; last_area=i==DISPLAY_DAMAGE_MAX+1;
        driver.flush_cb(&driver,&tiny,fb[2]);
    }
    verify_frame(); controller_display_log();
    assert(ready_calls>400 && completions>300);
    puts("PASS: 203 complete/incremental frames, rotation, damage overflow and raced retirement");
}
