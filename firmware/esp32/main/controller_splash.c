#include "controller_splash.h"

#if !LV_USE_PNG
#error "Music Controller requires CONFIG_LV_USE_PNG=y for its bundled splash"
#endif
extern const lv_img_dsc_t controller_splash_image;
static lv_obj_t *overlay;

bool controller_splash_visible(void) { return overlay!=NULL; }
void controller_splash_show(bool loading) {
    if(overlay) controller_splash_hide();
    overlay=lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_pos(overlay,0,0);
    lv_obj_set_size(overlay,480,800);
    lv_obj_clear_flag(overlay,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(overlay,lv_color_hex(0x101019),0);
    lv_obj_set_style_bg_opa(overlay,LV_OPA_COVER,0);
    lv_obj_t *image=lv_img_create(overlay);
    lv_img_set_src(image,&controller_splash_image);
    lv_obj_set_pos(image,0,0);
    if(loading) {
        lv_obj_t *caption=lv_label_create(overlay);
        lv_label_set_text(caption,"Loading...");
        lv_obj_set_style_text_font(caption,&lv_font_montserrat_24,0);
        lv_obj_set_style_text_color(caption,lv_color_white(),0);
        lv_obj_align(caption,LV_ALIGN_CENTER,0,0);
    }
    lv_obj_move_foreground(overlay);
}
void controller_splash_hide(void) {
    if(!overlay) return;
    lv_obj_del(overlay); overlay=NULL;
    /* Decoded PNG pixels are temporary; do not retain an idle background cache. */
    lv_img_cache_invalidate_src(&controller_splash_image);
    lv_obj_invalidate(lv_scr_act());
}
