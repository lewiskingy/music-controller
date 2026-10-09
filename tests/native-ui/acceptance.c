#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "controller_ui.h"

static controller_ui_t ui;
static lv_color_t draw_buffer[480 * 80];
static unsigned char pixels[480 * 800 * 3];
static lv_indev_data_t pointer;
static ui_intent_t last_intent;
static unsigned intent_count;
static const char *output_dir;
static unsigned checks;
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#value); exit(1); } } while(0)
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            lv_color32_t color; color.full = lv_color_to32(*colors++);
            if (x >= 0 && x < 480 && y >= 0 && y < 800) {
                size_t offset = ((size_t)y * 480 + x) * 3;
                pixels[offset] = color.ch.red;
                pixels[offset + 1] = color.ch.green;
                pixels[offset + 2] = color.ch.blue;
            }
        }
    }
    lv_disp_flush_ready(driver);
}
static void read_pointer(lv_indev_drv_t *driver, lv_indev_data_t *data) {
    (void)driver; *data = pointer;
}
static void advance(unsigned ms) {
    for (unsigned i = 0; i < ms; i += 10) { lv_tick_inc(10); lv_timer_handler(); }
    lv_obj_update_layout(ui.root);
}
static void screenshot(const char *name) {
    lv_obj_invalidate(ui.root); advance(100);
    char path[1024]; snprintf(path,sizeof(path),"%s/%s.ppm",output_dir,name);
    FILE *file = fopen(path,"wb"); CHECK(file != NULL);
    fprintf(file,"P6\n480 800\n255\n");
    CHECK(fwrite(pixels,1,sizeof(pixels),file) == sizeof(pixels)); fclose(file);
}
static void intent(const ui_intent_t *action) { last_intent = *action; ++intent_count; }
static void show_players(lv_event_t *event) { (void)event; controller_ui_show(&ui,UI_PLAYERS); }
static void show_sources(lv_event_t *event) { (void)event; controller_ui_show(&ui,UI_PROVIDERS); }
static void show_playing(lv_event_t *event) { (void)event; controller_ui_show(&ui,UI_PLAYING); }
static void save(lv_event_t *event) { (void)event; lv_label_set_text(ui.provider_feedback,"Selection saved"); }
static lv_area_t bounds(lv_obj_t *object) { lv_area_t area; lv_obj_get_coords(object,&area); return area; }
static void move(int x,int y,bool pressed,unsigned ms) {
    pointer.point.x = x; pointer.point.y = y;
    pointer.state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    advance(ms);
}
static void click(lv_obj_t *object) {
    lv_area_t area = bounds(object);
    int x = (area.x1 + area.x2)/2, y = (area.y1 + area.y2)/2;
    move(x,y,true,80); move(x,y,false,80);
}
/* Internal list scrolling is intentional. Everything outside scrolling content
   must fit inside its parent and the physical framebuffer. */
static void no_clipping(lv_obj_t *object,bool inside_scroll) {
    if (lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t area = bounds(object);
    if (!inside_scroll) {
        CHECK(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 480 && area.y2 < 800);
        lv_obj_t *parent = lv_obj_get_parent(object);
        if (parent) {
            lv_area_t enclosing = bounds(parent);
            CHECK(area.x1 >= enclosing.x1 && area.y1 >= enclosing.y1);
            CHECK(area.x2 <= enclosing.x2 && area.y2 <= enclosing.y2);
        }
    }
    bool scroll = inside_scroll || lv_obj_has_flag(object,LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i=0;i<lv_obj_get_child_cnt(object);++i) no_clipping(lv_obj_get_child(object,i),scroll);
}
static void fixed_caption(lv_obj_t *obj) {
    const lv_font_t *font=lv_obj_get_style_text_font(obj,LV_PART_MAIN);
    lv_point_t size;
    lv_txt_get_size(&size,lv_label_get_text(obj),font,0,0,1000,LV_TEXT_FLAG_NONE);
    CHECK(size.x<=lv_obj_get_width(obj) && size.y<=lv_obj_get_height(obj));
}
static void geometry(void) {
    advance(50);
    lv_area_t dock = bounds(lv_obj_get_parent(ui.transport[0]));
    lv_area_t volume = bounds(lv_obj_get_parent(ui.volume_up));
    CHECK(dock.x1==0 && dock.y1==596 && dock.x2==479 && dock.y2==731);
    CHECK(volume.x1==400 && volume.y1==64 && volume.x2==479 && volume.y2==595);
    lv_area_t previous=bounds(ui.transport[0]), play=bounds(ui.transport[1]), next=bounds(ui.transport[2]);
    CHECK(previous.x2 < play.x1 && play.x2 < next.x1);
    CHECK(lv_obj_get_width(ui.volume_up)>=48 && lv_obj_get_height(ui.volume_up)>=48);
    CHECK(lv_obj_get_width(ui.volume_down)>=48 && lv_obj_get_height(ui.volume_down)>=48);
    CHECK(!lv_obj_has_flag(ui.root,LV_OBJ_FLAG_SCROLLABLE));
    const char *tabs[]={"Playing","Queue","Browse","Search"};
    for(unsigned i=0;i<4;i++) {
        lv_obj_t *caption=lv_obj_get_child(ui.navigation[i],0);
        CHECK(!strcmp(lv_label_get_text(caption),tabs[i])); fixed_caption(caption);
        CHECK(lv_obj_has_state(ui.navigation[i],LV_STATE_DISABLED)==(i!=0));
        lv_area_t tab=bounds(ui.navigation[i]);
        CHECK(tab.x1==8+(int)i*118 && tab.y1==742);
    }
    fixed_caption(ui.volume_heading); fixed_caption(ui.heading);
    no_clipping(ui.root,false);
}
int main(int argc,char **argv) {
    CHECK(argc==3); output_dir=argv[1]; lv_init();
    lv_disp_draw_buf_t buffer; lv_disp_draw_buf_init(&buffer,draw_buffer,NULL,480*80);
    lv_disp_drv_t display; lv_disp_drv_init(&display);
    display.hor_res=480; display.ver_res=800; display.draw_buf=&buffer; display.flush_cb=flush;
    lv_disp_drv_register(&display);
    lv_indev_drv_t input; lv_indev_drv_init(&input); input.type=LV_INDEV_TYPE_POINTER; input.read_cb=read_pointer;
    lv_indev_drv_register(&input);
    controller_ui_create(&ui,lv_scr_act(),intent,show_players,show_sources,show_playing,save);
    const char *players[]={"Extension","Home Group","Callum's bedroom","Extension Telly"};
    for(unsigned i=0;i<4;i++) {
        lv_obj_t *row=lv_list_add_btn(ui.players_list,NULL,players[i]);
        controller_ui_style_row(row,i==0,i!=3);
    }
    const char *sources[]={"Navidrome","Spotify","BBC Sounds"};
    for(unsigned i=0;i<3;i++) {
        lv_obj_t *row=lv_checkbox_create(ui.providers_list);
        controller_ui_style_source(row); lv_checkbox_set_text(row,sources[i]); lv_obj_set_size(row,352,72);
        if(i==0) lv_obj_add_state(row,LV_STATE_CHECKED);
    }
    ui_playback_t state={.track="Daydreamer",.artist="Adele",.album="19",.player="Extension",
        .status="Connected",.player_id="room-a",.queue_id="queue-a",.item_id="track-a",
        .volume=45,.position=42,.duration=220,.connected=true,.available=true,.playing=true,
        .can_volume=true,.can_seek=true};
    controller_ui_update(&ui,&state); screenshot("01-now-playing"); geometry();
    click(ui.transport[0]); CHECK(intent_count==1 && last_intent.type==UI_PREVIOUS);
    click(ui.transport[1]); CHECK(intent_count==2 && last_intent.type==UI_PLAY_STOP);
    click(ui.transport[2]); CHECK(intent_count==3 && last_intent.type==UI_NEXT);
    CHECK(!strcmp(last_intent.player_id,"room-a"));
    click(ui.volume_up); CHECK(last_intent.type==UI_VOLUME_UP);
    click(ui.volume_down); CHECK(last_intent.type==UI_VOLUME_DOWN);
    click(ui.player_button); CHECK(!lv_obj_has_flag(ui.players_content,LV_OBJ_FLAG_HIDDEN));
    screenshot("02-players"); geometry();
    click(ui.settings_button); click(ui.sources_button); CHECK(!lv_obj_has_flag(ui.providers_content,LV_OBJ_FLAG_HIDDEN));
    screenshot("03-providers"); geometry(); click(ui.save_button);
    CHECK(!strcmp(lv_label_get_text(ui.provider_feedback),"Selection saved"));
    click(ui.navigation[0]); CHECK(!lv_obj_has_flag(ui.now_content,LV_OBJ_FLAG_HIDDEN));
    unsigned before=intent_count;
    lv_area_t seek=bounds(ui.timeline);
    move(seek.x1+40,seek.y1+3,true,80); move(seek.x1+120,seek.y1+3,true,80);
    CHECK(intent_count==before); screenshot("04-seek-preview");
    move(seek.x1+120,seek.y1+3,false,80);
    CHECK(intent_count==before+1 && last_intent.type==UI_SEEK && last_intent.value>42);
    before=intent_count;
    move(seek.x1+40,seek.y1+3,true,80); state.item_id="track-b"; controller_ui_update(&ui,&state);
    move(seek.x1+120,seek.y1+3,false,80); CHECK(intent_count==before);
    lv_area_t volume=bounds(ui.volume_slider); before=intent_count;
    move(volume.x1+3,volume.y2-40,true,350); move(volume.x1+3,volume.y1+40,true,80);
    CHECK(intent_count>before && last_intent.type==UI_VOLUME_SET);
    move(volume.x1+3,volume.y1+40,false,80); CHECK(last_intent.value>50);
    state.seek_pending=true; controller_ui_update(&ui,&state);
    CHECK(lv_obj_has_state(ui.timeline,LV_STATE_DISABLED));
    CHECK(!lv_obj_has_state(ui.volume_slider,LV_STATE_DISABLED)); screenshot("05-seek-pending");
    state.seek_pending=false; state.playing=false; controller_ui_update(&ui,&state);
    CHECK(!strcmp(lv_label_get_text(ui.transport_text),LV_SYMBOL_PLAY)); screenshot("06-stopped");
    state.live=true; state.duration=0; state.can_seek=false; state.track="BBC Radio 4";
    controller_ui_update(&ui,&state); CHECK(lv_obj_has_state(ui.timeline,LV_STATE_DISABLED));
    CHECK(!strcmp(lv_label_get_text(ui.elapsed),"Live")); screenshot("07-live-radio"); geometry();
    before=intent_count; click(ui.timeline); CHECK(intent_count==before);
    state.live=false; state.track="A very long track title that should remain inside the content pane without moving any controls";
    state.artist="An exceptionally long artist name requiring ellipsis rather than expansion";
    state.album="An album name that must remain on its allocated line";
    controller_ui_update(&ui,&state); screenshot("08-long-metadata"); geometry();
    state.connected=false; state.status="Gateway reconnecting..."; controller_ui_update(&ui,&state);
    before=intent_count; click(ui.transport[1]); click(ui.volume_up); CHECK(intent_count==before);
    screenshot("09-disconnected"); geometry();
    state.connected=true; state.available=false; controller_ui_update(&ui,&state);
    CHECK(lv_obj_has_state(ui.volume_slider,LV_STATE_DISABLED)); screenshot("10-unavailable"); geometry();
    state.track="Daydreamer"; state.artist="Adele"; state.album="19";
    state.available=true; state.can_seek=true; state.duration=220; state.playing=true;
    state.status="Connected"; controller_ui_update(&ui,&state);
    unsigned char *album_packet=malloc(8+288*288*2), *generic_packet=malloc(8+288*288*2);
    CHECK(album_packet && generic_packet);
    const char *fixture_names[]={"album","generic"};
    unsigned char *packets[]={album_packet,generic_packet};
    for(unsigned i=0;i<2;i++) {
        char path[1024]; snprintf(path,sizeof(path),"%s/%s.mcar",argv[2],fixture_names[i]);
        FILE *file=fopen(path,"rb"); CHECK(file);
        CHECK(fread(packets[i],1,8+288*288*2,file)==8+288*288*2); fclose(file);
    }
    state.art_id="mock-album"; controller_ui_update(&ui,&state); controller_ui_show(&ui,UI_PLAYING);
    CHECK(controller_ui_set_artwork(&ui,state.art_id,album_packet,8+288*288*2));
    CHECK(ui.art_loaded && !lv_obj_has_flag(ui.thumbnail_image,LV_OBJ_FLAG_HIDDEN));
    screenshot("11-album-art"); geometry();
    CHECK(!controller_ui_set_artwork(&ui,state.art_id,album_packet,8+288*288*2-1));
    album_packet[0]='X'; CHECK(!controller_ui_set_artwork(&ui,state.art_id,album_packet,8+288*288*2)); album_packet[0]='M';
    state.art_id="generic"; controller_ui_update(&ui,&state);
    CHECK(!ui.art_loaded && lv_obj_has_flag(ui.art_image,LV_OBJ_FLAG_HIDDEN));
    CHECK(!controller_ui_set_artwork(&ui,"mock-album",album_packet,8+288*288*2));
    screenshot("12-art-loading");
    CHECK(controller_ui_set_artwork(&ui,"generic",generic_packet,8+288*288*2));
    screenshot("13-generic-cover"); geometry();
    const char *themes[]={"green","blue","red","orange","purple","grey"};
    for(unsigned p=0;p<6;p++) for(unsigned mode=0;mode<2;mode++) {
        state.art_id="mock-album"; controller_ui_update(&ui,&state);
        CHECK(controller_ui_set_artwork(&ui,state.art_id,album_packet,8+288*288*2));
        click(ui.settings_button); click(ui.palette_buttons[p]); click(ui.mode_buttons[mode]);
        CHECK(ui.palette==p && ui.light==(bool)mode);
        CHECK(lv_obj_has_state(ui.palette_buttons[p],LV_STATE_CHECKED));
        CHECK(lv_obj_has_state(ui.mode_buttons[mode],LV_STATE_CHECKED));
        geometry();
        char name[80]; snprintf(name,sizeof(name),"theme-%s-%s-settings",themes[p],mode?"light":"dark");
        screenshot(name);
        click(ui.navigation[0]); geometry();
        CHECK(lv_color_to32(lv_obj_get_style_bg_color(ui.timeline,LV_PART_INDICATOR))==
              lv_color_to32(lv_obj_get_style_bg_color(ui.transport[1],LV_PART_MAIN)));
        snprintf(name,sizeof(name),"theme-%s-%s-playing",themes[p],mode?"light":"dark");
        screenshot(name);
        state.art_id="generic"; controller_ui_update(&ui,&state);
        CHECK(controller_ui_set_artwork(&ui,state.art_id,generic_packet,8+288*288*2));
        geometry();
        snprintf(name,sizeof(name),"theme-%s-%s-generic",themes[p],mode?"light":"dark"); screenshot(name);
        state.art_id="mock-album"; controller_ui_update(&ui,&state);
        CHECK(controller_ui_set_artwork(&ui,state.art_id,album_packet,8+288*288*2));
        unsigned count=intent_count;
        for(unsigned i=1;i<4;i++) click(ui.navigation[i]);
        CHECK(intent_count==count && !lv_obj_has_flag(ui.now_content,LV_OBJ_FLAG_HIDDEN));
        click(ui.player_button); geometry();
        snprintf(name,sizeof(name),"theme-%s-%s-players",themes[p],mode?"light":"dark"); screenshot(name);
        click(ui.settings_button); click(ui.sources_button); geometry();
        snprintf(name,sizeof(name),"theme-%s-%s-sources",themes[p],mode?"light":"dark"); screenshot(name);
    }
    controller_ui_set_theme(&ui,999,false); CHECK(ui.palette==0);
    printf("PASS: %u layout and interaction checks; 73 screenshots at 480x800\n",checks);
    free(album_packet); free(generic_packet);
    return 0;
}
