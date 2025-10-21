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
    float margin[4];
    float padding[4];
    uint32_t bg_color;
    uint32_t color;
    float font_size;
    uint32_t font_weight;
} hui_computed_style;

typedef struct {
    HUI_VEC(hui_computed_style) styles;
} hui_style_store;

void hui_style_store_init(hui_style_store *store);

void hui_style_store_reset(hui_style_store *store);

void hui_apply_styles(hui_style_store *store, hui_dom *dom, hui_intern *atoms, const hui_stylesheet *sheet,
                      uint32_t property_mask);

#endif