#pragma once

/* Render common Unicode typography with the matching stock punctuation glyph.
 * Only glyph lookup changes: UTF-8 metadata and search queries stay intact. */
static uint32_t punctuation_ascii(uint32_t code) {
    switch (code) {
        case 0x2018: case 0x2019: case 0x201a: case 0x201b: case 0x2032:
        case 0x02bc: return '\'';
        case 0x201c: case 0x201d: case 0x201e: case 0x201f: case 0x2033:
        case 0x00ab: case 0x00bb: return '"';
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014:
        case 0x2212: return '-';
        case 0x00a0: case 0x202f: return ' ';
        default: return code;
    }
}
static bool punctuation_descriptor(const lv_font_t *font, lv_font_glyph_dsc_t *glyph,
                                   uint32_t letter, uint32_t next) {
    uint32_t mapped=punctuation_ascii(letter);
    if (mapped==letter) return false;
    const lv_font_t *base=font->dsc;
    return base->get_glyph_dsc(base,glyph,mapped,punctuation_ascii(next));
}
static const uint8_t *punctuation_bitmap(const lv_font_t *font,uint32_t letter) {
    const lv_font_t *base=font->dsc;
    return base->get_glyph_bitmap(base,punctuation_ascii(letter));
}
static lv_font_t punctuation_small_fallback, punctuation_large_fallback;
static lv_font_t punctuation_text_font, punctuation_large_font;
static void punctuation_fonts_init(void) {
    punctuation_small_fallback=lv_font_montserrat_18;
    punctuation_small_fallback.get_glyph_dsc=punctuation_descriptor;
    punctuation_small_fallback.get_glyph_bitmap=punctuation_bitmap;
    punctuation_small_fallback.dsc=&lv_font_montserrat_18;
    punctuation_small_fallback.fallback=&search_pound_font;
    punctuation_large_fallback=lv_font_montserrat_24;
    punctuation_large_fallback.get_glyph_dsc=punctuation_descriptor;
    punctuation_large_fallback.get_glyph_bitmap=punctuation_bitmap;
    punctuation_large_fallback.dsc=&lv_font_montserrat_24;
    punctuation_text_font=lv_font_montserrat_18;
    punctuation_text_font.fallback=&punctuation_small_fallback;
    punctuation_large_font=lv_font_montserrat_24;
    punctuation_large_font.fallback=&punctuation_large_fallback;
}
