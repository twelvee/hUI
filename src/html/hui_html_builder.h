#ifndef HUI_HTML_BUILDER_H
#define HUI_HTML_BUILDER_H

#include <stddef.h>
#include <stdint.h>

#include "../hui_vec.h"
#include "../hui_arena.h"
#include "../hui_intern.h"
#include <hui/hui.h>

typedef enum {
    HUI_NODE_ELEM = 1,
    HUI_NODE_TEXT = 2
} hui_node_type;

enum {
    HUI_NODE_FLAG_NONE = 0,
    HUI_NODE_FLAG_HOVER = 1u << 0
};

enum {
    HUI_NODE_TF_NONE = 0,
    HUI_NODE_TF_VALUE = 1u << 0,
    HUI_NODE_TF_PLACEHOLDER = 1u << 1,
    HUI_NODE_TF_FOCUSED = 1u << 2,
    HUI_NODE_TF_CARET_VISIBLE = 1u << 3,
    HUI_NODE_TF_HAS_SELECTION = 1u << 4,
    HUI_NODE_TF_MULTILINE = 1u << 5
};

typedef struct hui_dom_node {
    uint32_t gen;
    uint32_t parent;
    uint32_t first_child;
    uint32_t last_child;
    uint32_t next_sibling;
    uint32_t style_id;
    float x, y, w, h;
    uint32_t flags;
    hui_node_type type;
    hui_atom tag;
    hui_atom id;
    HUI_VEC (hui_atom) classes;
    const char *text;
    uint32_t text_len;
    uint32_t text_cache_cp;
    uint32_t text_cache_lines;
    uint32_t text_cache_max_cols;
    uint32_t text_cache_valid;
    hui_atom attr_type;
    const char *attr_placeholder;
    uint32_t attr_placeholder_len;
    const char *attr_value;
    uint32_t attr_value_len;
    uint8_t attr_selected;
    hui_atom binding_text_atom;
    hui_atom binding_value_atom;
    uint32_t binding_text_index;
    uint32_t binding_value_index;
    uint32_t binding_template_index;
    uint32_t tf_flags;
    uint32_t tf_caret;
    uint32_t tf_sel_start;
    uint32_t tf_sel_end;
    float tf_scroll_x;
    float tf_scroll_y;
} hui_dom_node;

typedef struct {
    hui_arena arena;

    HUI_VEC(hui_dom_node) nodes;

    uint32_t root;
    HUI_VEC (int32_t) id_keys;
    HUI_VEC (uint32_t) id_vals;
    HUI_VEC (int32_t) class_keys;
    HUI_VEC (uint32_t) class_offsets;
    HUI_VEC (uint32_t) class_counts;
    HUI_VEC (uint32_t) class_items;
} hui_dom;

typedef struct {
    hui_dom *dom;
    hui_intern *atoms;

    hui_filter_decision (*filter_fn)(const hui_tag_probe *, void *);

    void *filter_user;
    const hui_filter_spec *filter_spec;
    size_t node_count_after_filter;
    int error;
} hui_builder;

void hui_dom_init(hui_dom *dom);

void hui_dom_reset(hui_dom *dom);

uint32_t hui_dom_add_node(hui_dom *dom, hui_node_type type);

void hui_dom_add_index_id(hui_builder *builder, hui_atom atom, uint32_t node_index);

void hui_dom_add_index_class(hui_builder *builder, hui_atom atom, uint32_t node_index);

int hui_build_from_html(hui_builder *builder, const char *html, size_t html_len);

void hui_dom_invalidate_text_cache(hui_dom_node *node);

void hui_dom_text_cache_refresh(hui_dom_node *node);

static inline void hui_dom_sync_last_child(hui_dom *dom, uint32_t parent) {
    if (!dom || parent == 0xFFFFFFFFu) return;
    hui_dom_node *p = &dom->nodes.data[parent];
    uint32_t last = 0xFFFFFFFFu;
    uint32_t child = p->first_child;
    while (child != 0xFFFFFFFFu) {
        last = child;
        child = dom->nodes.data[child].next_sibling;
    }
    p->last_child = last;
}

static inline void hui_dom_link_child_tail(hui_dom *dom, uint32_t parent, uint32_t child) {
    if (!dom || parent == 0xFFFFFFFFu || child == 0xFFFFFFFFu) return;
    hui_dom_node *p = &dom->nodes.data[parent];
    hui_dom_node *c = &dom->nodes.data[child];
    c->parent = parent;
    c->next_sibling = 0xFFFFFFFFu;
    if (p->first_child == 0xFFFFFFFFu) {
        p->first_child = child;
        p->last_child = child;
    } else {
        uint32_t last = p->last_child;
        if (last == 0xFFFFFFFFu) {
            hui_dom_sync_last_child(dom, parent);
            last = p->last_child;
            if (last == 0xFFFFFFFFu) {
                uint32_t iter = p->first_child;
                uint32_t prev = 0xFFFFFFFFu;
                while (iter != 0xFFFFFFFFu) {
                    prev = iter;
                    iter = dom->nodes.data[iter].next_sibling;
                }
                if (prev != 0xFFFFFFFFu) {
                    dom->nodes.data[prev].next_sibling = child;
                    p->last_child = child;
                    return;
                }
                p->first_child = child;
                p->last_child = child;
                return;
            }
        }
        dom->nodes.data[last].next_sibling = child;
        p->last_child = child;
    }
}

#endif
