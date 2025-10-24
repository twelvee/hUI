#ifndef HUI_HUI_DRAW_H
#define HUI_HUI_DRAW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HUI_DRAW_OP_RECT = 1,
    HUI_DRAW_OP_GLYPH_RUN = 2,
    HUI_DRAW_OP_RECT_BATCH = 3
} hui_draw_op;

typedef struct {
    hui_draw_op op;
    uint32_t u0;
    uint32_t u1;
    uint32_t u2;
    float f[7];
} hui_draw;

typedef struct {
    float x;
    float y;
    float w;
    float h;
    uint32_t node_index;
} hui_draw_rect;

typedef struct {
    const hui_draw *items;
    const hui_draw_rect *rects;
    size_t count;
    size_t rect_count;
} hui_draw_list_view;

typedef struct {
    hui_draw_list_view draw;
    uint32_t dirty_flags;
    int changed;
} hui_render_output;

#ifdef __cplusplus
}
#endif

#endif /* HUI_HUI_DRAW_H */
