#ifndef HUI_STYLE_H
#define HUI_STYLE_H

#include <stdint.h>
#include "../html/hui_html_builder.h"
#include "../css/hui_css_parser.h"

typedef struct {
    uint32_t present_mask;
    uint32_t display;
    float width;
    float height;
    float min_height;
    float margin[4];
    float padding[4];
    uint32_t bg_color;
    uint32_t color;
    float font_size;
    uint32_t font_weight;
    uint32_t font_style;
    float line_height;
    hui_atom font_family;
    uint32_t font_id;
} hui_computed_style;

typedef struct {
    HUI_VEC(hui_computed_style) styles;
} hui_style_store;

enum {
    HUI_STYLE_PRESENT_DISPLAY = 1u << 0,
    HUI_STYLE_PRESENT_WIDTH = 1u << 1,
    HUI_STYLE_PRESENT_HEIGHT = 1u << 2,
    HUI_STYLE_PRESENT_MARGIN = 1u << 3,
    HUI_STYLE_PRESENT_PADDING = 1u << 4,
    HUI_STYLE_PRESENT_BG_COLOR = 1u << 5,
    HUI_STYLE_PRESENT_COLOR = 1u << 6,
    HUI_STYLE_PRESENT_FONT_SIZE = 1u << 7,
    HUI_STYLE_PRESENT_FONT_WEIGHT = 1u << 8,
    HUI_STYLE_PRESENT_FONT_STYLE = 1u << 9,
    HUI_STYLE_PRESENT_FONT_FAMILY = 1u << 10,
    HUI_STYLE_PRESENT_LINE_HEIGHT = 1u << 11,
    HUI_STYLE_PRESENT_MIN_HEIGHT = 1u << 12
};

void hui_style_store_init(hui_style_store *store);

void hui_style_store_reset(hui_style_store *store);

void hui_style_store_release(hui_style_store *store);

void hui_apply_styles(hui_style_store *store, hui_dom *dom, hui_intern *atoms, const hui_stylesheet *sheet,
                      uint32_t property_mask);

#endif
