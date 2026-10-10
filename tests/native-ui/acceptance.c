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
static unsigned checks, screenshots, theme_saves;
static unsigned saved_palette;
static bool saved_light;
static void theme_saved(unsigned palette,bool light) { theme_saves++; saved_palette=palette; saved_light=light; }
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
    screenshots++;
    lv_obj_invalidate(ui.root); advance(350);
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
    advance(50);
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
        CHECK(lv_obj_has_state(ui.navigation[i],LV_STATE_DISABLED)==false);
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
        controller_ui_source_row(ui.providers_list,sources[i],i==0);
    }
    ui_playback_t state={.track="Daydreamer",.artist="Adele",.album="19",.player="Extension",
        .status="Connected",.player_id="room-a",.queue_id="queue-a",.item_id="track-a",
        .volume=45,.position=42,.duration=220,.connected=true,.available=true,.playing=true,
        .can_volume=true,.can_seek=true,.can_queue=true,.can_browse=true,.can_search=true};
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
    screenshot("03-providers"); geometry();
    lv_obj_t *first_source=lv_obj_get_child(ui.providers_list,0), *second_source=lv_obj_get_child(ui.providers_list,1);
    click(second_source);
    CHECK(!lv_obj_has_state(first_source,LV_STATE_CHECKED) && lv_obj_has_state(second_source,LV_STATE_CHECKED));
    click(second_source); CHECK(lv_obj_has_state(second_source,LV_STATE_CHECKED));
    click(first_source);
    CHECK(lv_obj_has_state(first_source,LV_STATE_CHECKED) && !lv_obj_has_state(second_source,LV_STATE_CHECKED));
    screenshot("provider-exclusive-choice"); click(ui.save_button);
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
    ui.theme_cb=theme_saved;
    for(unsigned p=0;p<6;p++) for(unsigned mode=0;mode<2;mode++) {
        state.art_id="mock-album"; controller_ui_update(&ui,&state);
        CHECK(controller_ui_set_artwork(&ui,state.art_id,album_packet,8+288*288*2));
        click(ui.settings_button); click(ui.palette_buttons[p]); click(ui.mode_buttons[mode]);
        CHECK(ui.palette==p && ui.light==(bool)mode);
        CHECK(saved_palette==p && saved_light==(bool)mode);
        CHECK(lv_obj_has_state(ui.palette_buttons[p],LV_STATE_CHECKED));
        CHECK(lv_obj_has_state(ui.mode_buttons[mode],LV_STATE_CHECKED));
        geometry();
        if(p==1 && mode==1) screenshot("theme-edit-settings");
        click(ui.navigation[0]); geometry();
        CHECK(lv_color_to32(lv_obj_get_style_bg_color(ui.timeline,LV_PART_INDICATOR))==
              lv_color_to32(lv_obj_get_style_bg_color(ui.transport[1],LV_PART_MAIN)));
        CHECK(ui.art_loaded && ui.art_pixels[0].full==((unsigned)album_packet[8]|((unsigned)album_packet[9]<<8)));
        if(p==1 && mode==1) screenshot("theme-edit-applied");
    }
    CHECK(theme_saves==24);
    controller_ui_set_theme(&ui,0,false);
    click(ui.navigation[1]); CHECK(last_intent.type==UI_QUEUE_OPEN);
    CHECK(!lv_obj_has_flag(ui.queue_content,LV_OBJ_FLAG_HIDDEN));
    controller_ui_queue(&ui,NULL,0,0,25,"","Loading queue...",true,false);
    CHECK(lv_obj_has_state(ui.queue_refresh,LV_STATE_DISABLED)); screenshot("queue-loading"); geometry();
    ui_queue_item_t rows[UI_QUEUE_PAGE_SIZE]={0};
    for(unsigned i=0;i<UI_QUEUE_PAGE_SIZE;i++) {
        snprintf(rows[i].id,sizeof(rows[i].id),"queue-track-%u",i+1);
        snprintf(rows[i].title,sizeof(rows[i].title),"Track %u",i+1);
        snprintf(rows[i].artist,sizeof(rows[i].artist),"Adele"); rows[i].duration=220; rows[i].available=true;
    }
    snprintf(rows[0].title,sizeof(rows[0].title),"Daydreamer");
    snprintf(rows[2].title,sizeof(rows[2].title),"A long queue track name that must stay inside its allocated row");
    rows[3].available=false;
    state.item_id="queue-track-1"; controller_ui_update(&ui,&state);
    controller_ui_queue(&ui,rows,20,0,25,state.item_id,"Tap a track to play",false,true);
    CHECK(ui.queue_count==20 && lv_obj_has_state(ui.queue_back,LV_STATE_DISABLED));
    CHECK(!lv_obj_has_state(ui.queue_more,LV_STATE_DISABLED));
    before=intent_count; click(ui.queue_rows[1]);
    CHECK(intent_count==before+1 && last_intent.type==UI_QUEUE_PLAY);
    CHECK(!strcmp(last_intent.item_id,"queue-track-2") && !strcmp(last_intent.queue_id,"queue-a"));
    before=intent_count; click(ui.queue_rows[3]); CHECK(intent_count==before);
    screenshot("queue-tracks"); geometry();
    click(ui.queue_more); CHECK(last_intent.type==UI_QUEUE_MORE);
    controller_ui_queue(&ui,rows,5,20,25,state.item_id,"Tap a track to play",false,true);
    CHECK(!lv_obj_has_state(ui.queue_back,LV_STATE_DISABLED));
    CHECK(lv_obj_has_state(ui.queue_more,LV_STATE_DISABLED)); screenshot("queue-last-page"); geometry();
    click(ui.queue_back); CHECK(last_intent.type==UI_QUEUE_BACK);
    click(ui.queue_refresh); CHECK(last_intent.type==UI_QUEUE_REFRESH);
    state.player_id="other-room"; controller_ui_update(&ui,&state);
    before=intent_count; click(ui.queue_rows[0]); CHECK(intent_count==before);
    state.player_id="room-a"; state.transport_pending=true; controller_ui_update(&ui,&state);
    CHECK(lv_obj_has_state(ui.queue_rows[0],LV_STATE_DISABLED));
    state.transport_pending=false; state.connected=false; controller_ui_update(&ui,&state);
    CHECK(lv_obj_has_state(ui.queue_rows[0],LV_STATE_DISABLED)); screenshot("queue-disconnected");
    state.connected=true; controller_ui_update(&ui,&state);
    controller_ui_queue(&ui,NULL,0,0,0,"","Queue is empty",false,true); screenshot("queue-empty"); geometry();
    controller_ui_queue(&ui,NULL,0,0,0,"","Could not load queue - tap Refresh",false,false); screenshot("queue-error"); geometry();
    controller_ui_set_theme(&ui,0,false);
    click(ui.navigation[2]); CHECK(last_intent.type==UI_BROWSE_OPEN);
    CHECK(!lv_obj_has_flag(ui.browse_content,LV_OBJ_FLAG_HIDDEN));
    ui_media_item_t catalogue[UI_QUEUE_PAGE_SIZE]={0};
    snprintf(catalogue[0].id,sizeof(catalogue[0].id),"navidrome");
    snprintf(catalogue[0].provider,sizeof(catalogue[0].provider),"navidrome");
    snprintf(catalogue[0].title,sizeof(catalogue[0].title),"Navidrome");
    snprintf(catalogue[0].subtitle,sizeof(catalogue[0].subtitle),"Albums, artists and playlists");
    catalogue[0].kind=UI_MEDIA_SOURCE; catalogue[0].available=true;
    controller_ui_browse(&ui,catalogue,1,"Music sources","Choose a music source",false,true,false,false,false,"",100);
    screenshot("browse-sources"); geometry();
    click(ui.browse_rows[0]); CHECK(last_intent.type==UI_BROWSE_SELECT && last_intent.generation==100);
    CHECK(!strcmp(last_intent.provider,"navidrome"));
    const char *category_names[]={"Albums","Artists","Playlists","Provider folders"};
    const char *category_ids[]={"albums","artists","playlists","folders"};
    for(unsigned i=0;i<4;i++) {
        snprintf(catalogue[i].id,sizeof(catalogue[i].id),"%s",category_ids[i]);
        snprintf(catalogue[i].title,sizeof(catalogue[i].title),"%s",category_names[i]);
        snprintf(catalogue[i].provider,sizeof(catalogue[i].provider),"navidrome");
        catalogue[i].subtitle[0]=0; catalogue[i].uri[0]=0; catalogue[i].kind=UI_MEDIA_CATEGORY; catalogue[i].available=true;
    }
    controller_ui_browse(&ui,catalogue,4,"Navidrome","Choose a category",false,true,true,false,false,"",101);
    screenshot("browse-categories"); geometry(); click(ui.browse_rows[0]);
    CHECK(!strcmp(last_intent.item_id,"albums"));
    for(unsigned i=0;i<20;i++) {
        snprintf(catalogue[i].id,sizeof(catalogue[i].id),"album-%u",i+1);
        snprintf(catalogue[i].title,sizeof(catalogue[i].title),i==0?"19":"Album %u",i+1);
        snprintf(catalogue[i].provider,sizeof(catalogue[i].provider),"library");
        snprintf(catalogue[i].uri,sizeof(catalogue[i].uri),"library://album/%u",i+1);
        snprintf(catalogue[i].subtitle,sizeof(catalogue[i].subtitle),"Adele");
        catalogue[i].kind=UI_MEDIA_ALBUM; catalogue[i].available=true;
    }
    controller_ui_browse(&ui,catalogue,20,"Albums","Choose an album",false,true,true,false,true,"",102);
    screenshot("browse-albums"); geometry(); click(ui.browse_rows[0]);
    CHECK(last_intent.type==UI_BROWSE_SELECT && !strcmp(last_intent.media_uri,"library://album/1"));
    click(ui.browse_more); CHECK(last_intent.type==UI_BROWSE_MORE);
    controller_ui_browse(&ui,catalogue,5,"Albums","Choose an album",false,true,true,true,false,"",103);
    click(ui.browse_previous); CHECK(last_intent.type==UI_BROWSE_PREVIOUS);
    click(ui.browse_refresh); CHECK(last_intent.type==UI_BROWSE_REFRESH);
    catalogue[0].kind=UI_MEDIA_ARTIST; snprintf(catalogue[0].title,sizeof(catalogue[0].title),"Adele");
    controller_ui_browse(&ui,catalogue,1,"Artists","Choose an artist",false,true,true,false,false,"",104); screenshot("browse-artists"); geometry();
    catalogue[0].kind=UI_MEDIA_PLAYLIST; snprintf(catalogue[0].title,sizeof(catalogue[0].title),"Evening music");
    controller_ui_browse(&ui,catalogue,1,"Playlists","Choose a playlist",false,true,true,false,false,"",105); screenshot("browse-playlists"); geometry();
    for(unsigned i=0;i<20;i++) {
        catalogue[i].kind=UI_MEDIA_TRACK;
        snprintf(catalogue[i].uri,sizeof(catalogue[i].uri),"library://track/%u",i+1);
        snprintf(catalogue[i].title,sizeof(catalogue[i].title),i==0?"Daydreamer":"Track %u",i+1);
    }
    catalogue[2].available=false;
    controller_ui_browse(&ui,catalogue,20,"19","Tap a track or Play all",false,true,true,false,false,"library://album/1",106);
    screenshot("browse-album-tracks"); geometry();
    click(ui.browse_rows[0]); CHECK(last_intent.type==UI_BROWSE_PLAY && !strcmp(last_intent.media_uri,"library://track/1"));
    CHECK(!strcmp(last_intent.player_id,"room-a") && !strcmp(last_intent.queue_id,"queue-a"));
    before=intent_count; click(ui.browse_rows[2]); CHECK(intent_count==before);
    click(ui.browse_play_all); CHECK(last_intent.type==UI_BROWSE_PLAY_ALL && !strcmp(last_intent.media_uri,"library://album/1"));
    state.transport_pending=true; controller_ui_update(&ui,&state);
    CHECK(lv_obj_has_state(ui.browse_play_all,LV_STATE_DISABLED));
    state.transport_pending=false; state.player_id="other-room"; controller_ui_update(&ui,&state);
    before=intent_count; click(ui.browse_rows[0]); click(ui.browse_play_all); CHECK(intent_count==before);
    state.player_id="room-a"; controller_ui_update(&ui,&state);
    controller_ui_browse(&ui,NULL,0,"19","Loading music...",true,false,true,false,false,"library://album/1",107);
    controller_ui_update(&ui,&state); CHECK(lv_obj_has_state(ui.browse_play_all,LV_STATE_DISABLED));
    screenshot("browse-loading"); geometry(); click(ui.browse_back); CHECK(last_intent.type==UI_BROWSE_BACK);
    controller_ui_browse(&ui,NULL,0,"Albums","No items in this source",false,true,true,false,false,"",108); screenshot("browse-empty"); geometry();
    controller_ui_browse(&ui,NULL,0,"Albums","Could not browse - tap Refresh",false,false,true,false,false,"",109); screenshot("browse-error"); geometry();
    catalogue[0].kind=UI_MEDIA_FOLDER; snprintf(catalogue[0].title,sizeof(catalogue[0].title),"Recommended albums");
    snprintf(catalogue[0].uri,sizeof(catalogue[0].uri),"navidrome://albums/recommended");
    controller_ui_browse(&ui,catalogue,1,"Provider folders","Choose a folder",false,true,true,false,false,"",110); screenshot("browse-folders"); geometry();
    click(ui.browse_rows[0]); CHECK(last_intent.type==UI_BROWSE_SELECT && !strcmp(last_intent.media_uri,"navidrome://albums/recommended"));
    state.connected=false; controller_ui_update(&ui,&state);
    CHECK(lv_obj_has_state(ui.browse_rows[0],LV_STATE_DISABLED)); screenshot("browse-disconnected"); geometry();
    state.connected=true; controller_ui_update(&ui,&state);
    /* Search input and keyboard stay inside the content pane; both docks persist. */
    click(ui.navigation[3]); CHECK(last_intent.type==UI_SEARCH_OPEN);
    CHECK(!lv_obj_has_flag(ui.search_content,LV_OBJ_FLAG_HIDDEN));
    controller_ui_search(&ui,NULL,0,"Navidrome","Enter a title or artist",false,true,200);
    screenshot("search-start"); geometry();
    click(ui.search_input); CHECK(!lv_obj_has_flag(ui.search_keyboard,LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_obj_has_flag(ui.search_list,LV_OBJ_FLAG_HIDDEN));
    lv_area_t keyboard=bounds(ui.search_keyboard);
    CHECK(keyboard.x1>=24 && keyboard.x2<400 && keyboard.y1>=64 && keyboard.y2<596);
    /* Exercise the real keyboard default handler with its selected key. */
    unsigned key=0;
    while(strcmp(lv_btnmatrix_get_btn_text(ui.search_keyboard,key),"a")) { key++; CHECK(key<100); }
    lv_btnmatrix_set_selected_btn(ui.search_keyboard,key);
    lv_event_send(ui.search_keyboard,LV_EVENT_VALUE_CHANGED,NULL);
    CHECK(!strcmp(lv_textarea_get_text(ui.search_input),"a") && last_intent.type==UI_SEARCH_EDIT);
    lv_textarea_set_text(ui.search_input,"Adele");
    screenshot("search-keyboard"); geometry();
    lv_event_send(ui.search_keyboard,LV_EVENT_READY,NULL);
    CHECK(last_intent.type==UI_SEARCH_SUBMIT && !strcmp(last_intent.query,"Adele") && last_intent.value==0);
    CHECK(lv_obj_has_flag(ui.search_keyboard,LV_OBJ_FLAG_HIDDEN));
    controller_ui_search(&ui,NULL,0,"Navidrome","Searching...",true,false,201);
    screenshot("search-loading"); geometry();
    for(unsigned i=0;i<20;i++) {
        snprintf(catalogue[i].id,sizeof(catalogue[i].id),"track-%u",i+1);
        snprintf(catalogue[i].uri,sizeof(catalogue[i].uri),"navidrome://track/%u",i+1);
        snprintf(catalogue[i].provider,sizeof(catalogue[i].provider),"navidrome");
        snprintf(catalogue[i].title,sizeof(catalogue[i].title),i==0?"Daydreamer":"Track %u",i+1);
        snprintf(catalogue[i].subtitle,sizeof(catalogue[i].subtitle),"Adele");
        catalogue[i].kind=UI_MEDIA_TRACK; catalogue[i].available=i!=2;
    }
    controller_ui_search(&ui,catalogue,20,"Navidrome","First 20 results - refine your search",false,true,202);
    screenshot("search-tracks"); geometry();
    click(ui.search_rows[0]); CHECK(last_intent.type==UI_SEARCH_PLAY && last_intent.generation==202);
    CHECK(!strcmp(last_intent.media_uri,"navidrome://track/1") && !strcmp(last_intent.queue_id,"queue-a"));
    before=intent_count; click(ui.search_rows[2]); CHECK(intent_count==before);
    state.player_id="other-room"; controller_ui_update(&ui,&state);
    before=intent_count; click(ui.search_rows[0]); CHECK(intent_count==before);
    state.player_id="room-a"; state.transport_pending=true; controller_ui_update(&ui,&state);
    CHECK(lv_obj_has_state(ui.search_rows[0],LV_STATE_DISABLED));
    state.transport_pending=false; controller_ui_update(&ui,&state);
    /* Editing makes old results untappable immediately, even before a render. */
    lv_textarea_set_text(ui.search_input,"New query"); CHECK(ui.search_count==0 && last_intent.type==UI_SEARCH_EDIT);
    before=intent_count; click(ui.search_rows[0]); CHECK(intent_count==before);
    const ui_media_kind_t search_kinds[]={UI_MEDIA_ALBUM,UI_MEDIA_ARTIST,UI_MEDIA_PLAYLIST};
    const char *search_names[]={"search-albums","search-artists","search-playlists"};
    for(unsigned i=0;i<3;i++) {
        lv_dropdown_set_selected(ui.search_filter,i+1); lv_event_send(ui.search_filter,LV_EVENT_VALUE_CHANGED,NULL);
        CHECK(last_intent.type==UI_SEARCH_EDIT && last_intent.value==(int)i+1);
        click(ui.search_submit); CHECK(last_intent.type==UI_SEARCH_SUBMIT && last_intent.value==(int)i+1);
        catalogue[0].kind=search_kinds[i];
        snprintf(catalogue[0].title,sizeof(catalogue[0].title),"%s",i==0?"19":(i==1?"Adele":"Evening favourites"));
        snprintf(catalogue[0].uri,sizeof(catalogue[0].uri),"navidrome://%s/1",i==0?"album":(i==1?"artist":"playlist"));
        controller_ui_search(&ui,catalogue,1,"Navidrome","Tap a result",false,true,203+i);
        screenshot(search_names[i]); geometry();
        click(ui.search_rows[0]); CHECK(last_intent.type==UI_SEARCH_SELECT && last_intent.generation==203+i);
        CHECK(!strcmp(last_intent.provider,"navidrome"));
    }
    controller_ui_search(&ui,NULL,0,"Navidrome","No results - try another search",false,true,206);
    screenshot("search-empty"); geometry();
    controller_ui_search(&ui,NULL,0,"Navidrome","Search failed - retry Search",false,false,207);
    screenshot("search-error"); geometry();
    controller_ui_search(&ui,NULL,0,"Navidrome","Search timed out - retry Search",false,false,208);
    screenshot("search-timeout"); geometry();
    catalogue[0].kind=UI_MEDIA_TRACK;
    controller_ui_search(&ui,catalogue,1,"Navidrome","Disconnected - reconnect to search",false,false,209);
    state.connected=false; controller_ui_update(&ui,&state);
    before=intent_count; click(ui.search_rows[0]); CHECK(intent_count==before);
    screenshot("search-disconnected"); geometry();
    state.connected=true; state.can_search=false; controller_ui_update(&ui,&state);
    CHECK(lv_obj_has_state(ui.search_rows[0],LV_STATE_DISABLED));
    controller_ui_search(&ui,NULL,0,"Navidrome","Gateway update required for Search",false,false,210);
    screenshot("search-unsupported"); geometry();
    controller_ui_set_theme(&ui,999,false); CHECK(ui.palette==0);
    printf("PASS: %u layout and interaction checks; %u screenshots at 480x800\n",checks,screenshots);
    free(album_packet); free(generic_packet);
    return 0;
}
