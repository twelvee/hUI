#ifndef HUI_CSS_PARSER_H
#define HUI_CSS_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include "../hui_vec.h"
#include "../hui_intern.h"

typedef enum { HUI_SEL_TAG = 1, HUI_SEL_CLASS = 2, HUI_SEL_ID = 3 } hui_sel_atom_type;

typedef enum { HUI_COMB_END = 0, HUI_COMB_DESC = 1, HUI_COMB_CHILD = 2 } hui_combinator;

enum {
    HUI_SEL_PSEUDO_NONE = 0,
    HUI_SEL_PSEUDO_HOVER = 1u << 0,
    HUI_SEL_PSEUDO_ROOT = 1u << 1
};

enum {
    HUI_DISPLAY_NONE = 0,
    HUI_DISPLAY_BLOCK = 1,
    HUI_DISPLAY_INLINE = 2,
    HUI_DISPLAY_FLEX = 3
};

enum {
    HUI_FLEX_DIRECTION_ROW = 0,
    HUI_FLEX_DIRECTION_COLUMN = 1
};

enum {
    HUI_FLEX_JUSTIFY_FLEX_START = 0,
    HUI_FLEX_JUSTIFY_CENTER = 1,
    HUI_FLEX_JUSTIFY_FLEX_END = 2,
    HUI_FLEX_JUSTIFY_SPACE_BETWEEN = 3,
    HUI_FLEX_JUSTIFY_SPACE_AROUND = 4,
    HUI_FLEX_JUSTIFY_SPACE_EVENLY = 5
};

enum {
    HUI_FLEX_ALIGN_AUTO = 0,
    HUI_FLEX_ALIGN_FLEX_START = 1,
    HUI_FLEX_ALIGN_CENTER = 2,
    HUI_FLEX_ALIGN_FLEX_END = 3,
    HUI_FLEX_ALIGN_STRETCH = 4
};

typedef struct {
    hui_sel_atom_type type;
    hui_atom atom;
} hui_sel_simple;

typedef struct {
    hui_combinator comb;
    hui_sel_simple simple;
    uint32_t pseudo_mask;
} hui_sel_step;

typedef struct {
    HUI_VEC(hui_sel_step) steps;

    uint32_t specificity;
} hui_selector;

typedef enum {
    HUI_DECL_DISPLAY = 1,
    HUI_DECL_WIDTH,
    HUI_DECL_HEIGHT,
    HUI_DECL_MIN_HEIGHT,
    HUI_DECL_MARGIN_TOP,
    HUI_DECL_MARGIN_RIGHT,
    HUI_DECL_MARGIN_BOTTOM,
    HUI_DECL_MARGIN_LEFT,
    HUI_DECL_PADDING_TOP,
    HUI_DECL_PADDING_RIGHT,
    HUI_DECL_PADDING_BOTTOM,
    HUI_DECL_PADDING_LEFT,
    HUI_DECL_BG_COLOR,
    HUI_DECL_COLOR,
    HUI_DECL_FONT_SIZE,
    HUI_DECL_FONT_WEIGHT,
    HUI_DECL_FONT_STYLE,
    HUI_DECL_FONT_FAMILY,
    HUI_DECL_LINE_HEIGHT,
    HUI_DECL_FLEX_DIRECTION,
    HUI_DECL_JUSTIFY_CONTENT,
    HUI_DECL_ALIGN_ITEMS,
    HUI_DECL_FLEX_GROW,
    HUI_DECL_FLEX_SHRINK,
    HUI_DECL_FLEX_BASIS,
    HUI_DECL_FLEX,
    HUI_DECL_ALIGN_SELF
} hui_decl_id;

typedef enum {
    HUI_VAL_AUTO = 0,
    HUI_VAL_PX = 1,
    HUI_VAL_PERCENT = 2,
    HUI_VAL_COLOR = 3,
    HUI_VAL_ENUM = 4,
    HUI_VAL_NUMBER = 5,
    HUI_VAL_ATOM = 6,
    HUI_VAL_STRING = 7
} hui_value_kind;

typedef struct {
    hui_value_kind kind;
    union {
        float num;
        uint32_t u32;
        hui_atom atom;
        struct {
            char *ptr;
            size_t len;
        } str;
    } data;
} hui_value;

typedef struct {
    hui_decl_id id;
    hui_value val;
} hui_decl;

typedef struct {
    HUI_VEC(hui_selector) selectors;

    HUI_VEC(hui_decl) decls;
} hui_rule;

typedef struct {
    hui_atom family_atom;
    char *family_name;
    char *src;
    size_t src_len;
    uint32_t weight;
    uint32_t style;
} hui_css_font_face;

typedef struct {
    hui_atom name;
    char *value;
    size_t value_len;
} hui_css_custom_prop;

typedef struct {
    HUI_VEC(hui_rule) rules;
    HUI_VEC(hui_css_font_face) font_faces;
    HUI_VEC(hui_css_custom_prop) custom_props;
} hui_stylesheet;

void hui_css_init(hui_stylesheet *sheet);

void hui_css_reset(hui_stylesheet *sheet);

int hui_css_parse(hui_stylesheet *sheet, hui_intern *atoms, const char *css, size_t css_len);

#endif
