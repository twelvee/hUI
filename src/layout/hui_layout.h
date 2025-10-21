#ifndef HUI_LAYOUT_H
#define HUI_LAYOUT_H

#include <stdint.h>
#include "../style/hui_style.h"

typedef struct {
    float viewport_w;
    float viewport_h;
    float dpi;
} hui_layout_opts;

void hui_layout_run(hui_dom *dom, const hui_style_store *styles, const hui_layout_opts *opts);

#endif
