#ifndef HUI_HUI_H
#define HUI_HUI_H

#include <stddef.h>
#include <stdint.h>
#include "hui_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HUI_VERSION_MAJOR 0
#define HUI_VERSION_MINOR 1
#define HUI_VERSION_PATCH 0

typedef struct hui_ctx hui_ctx;

typedef struct {
    const uint8_t *ptr;
    size_t len;
} hui_bytes;

typedef struct {
    uint32_t index;
    uint32_t gen;
} hui_node_handle;

static const hui_node_handle HUI_NODE_NULL = {0xFFFFFFFFu, 0};

enum {
    HUI_DIRTY_STYLE = 1u << 0,
    HUI_DIRTY_LAYOUT = 1u << 1,
    HUI_DIRTY_PAINT = 1u << 2,
    HUI_DIRTY_ALL = 0x7u
};

typedef enum {
    HUI_FILTER_TAKE = 0,
    HUI_FILTER_SKIP_DESCEND = 1,
    HUI_FILTER_PRUNE_SUBTREE = 2
} hui_filter_decision;

typedef struct {
    const char *tag;
    size_t tag_len;
    const char *id;
    size_t id_len;
    const char *class_list;
    size_t class_list_len;
    int depth;
} hui_tag_probe;

typedef hui_filter_decision (*hui_dom_filter_fn)(const hui_tag_probe *probe, void *user);

typedef struct {
    const char **allow_tags;
    size_t allow_tags_count;
    const char **allow_ids;
    size_t allow_ids_count;
    const char **allow_classes;
    size_t allow_classes_count;
    int max_depth;
    size_t max_nodes;
    uint32_t flags;
} hui_filter_spec;

typedef struct {
    float viewport_w, viewport_h, dpi;
    uint32_t flags;
} hui_build_opts;

enum {
    HUI_PROP_DISPLAY = 1u << 0,
    HUI_PROP_SIZE_POS = 1u << 1,
    HUI_PROP_COLOR = 1u << 2,
    HUI_PROP_FONT = 1u << 3,
    HUI_PROP_MISC = 1u << 4,
    HUI_PROP_ALL = 0xFFFFFFFFu
};

typedef struct {
    const void *data;
    size_t size;
} hui_ir_view;

hui_ctx *hui_create(void * (*alloc_fn)(size_t), void (*free_fn)(void *));

void hui_destroy(hui_ctx *ctx);

void hui_set_dom_filter(hui_ctx *ctx, hui_dom_filter_fn fn, void *user);

void hui_set_filter_spec(hui_ctx *ctx, const hui_filter_spec *spec);

void hui_set_style_property_mask(hui_ctx *ctx, uint32_t property_mask);

int hui_feed_html(hui_ctx *ctx, hui_bytes chunk, int is_last);

int hui_feed_css(hui_ctx *ctx, hui_bytes chunk, int is_last);

int hui_parse(hui_ctx *ctx);

int hui_style(hui_ctx *ctx);

int hui_layout(hui_ctx *ctx, const hui_build_opts *opts);

int hui_paint(hui_ctx *ctx);

int hui_build_ir(hui_ctx *ctx, const hui_build_opts *opts);

hui_ir_view hui_get_ir(hui_ctx *ctx);

int hui_write_ir_file(hui_ctx *ctx, const char *path);

int hui_dump_text_ir(hui_ctx *ctx, const char *path);

hui_draw_list_view hui_get_draw_list(hui_ctx *ctx);

const char *hui_draw_text_utf8(hui_ctx *ctx, const hui_draw *cmd, size_t *len);

hui_node_handle hui_dom_root(hui_ctx *ctx);

hui_node_handle hui_dom_query_id(hui_ctx *ctx, const char *id_utf8);

size_t hui_dom_query_class(hui_ctx *ctx, const char *class_utf8, hui_node_handle *out, size_t max_out);

size_t hui_dom_query_tag(hui_ctx *ctx, const char *tag_utf8, hui_node_handle *out, size_t max_out);

int hui_node_is_null(hui_node_handle h);

int hui_node_is_element(hui_ctx *ctx, hui_node_handle h);

int hui_node_is_text(hui_ctx *ctx, hui_node_handle h);

hui_node_handle hui_node_parent(hui_ctx *ctx, hui_node_handle h);

hui_node_handle hui_node_first_child(hui_ctx *ctx, hui_node_handle h);

hui_node_handle hui_node_next_sibling(hui_ctx *ctx, hui_node_handle h);

int hui_dom_set_attr(hui_ctx *ctx, hui_node_handle h, const char *name, const char *value);

int hui_dom_add_class(hui_ctx *ctx, hui_node_handle h, const char *class_name);

int hui_dom_remove_class(hui_ctx *ctx, hui_node_handle h, const char *class_name);

int hui_dom_set_text(hui_ctx *ctx, hui_node_handle h, const char *text_utf8);

int hui_dom_append_child(hui_ctx *ctx, hui_node_handle parent, hui_node_handle child);

hui_node_handle hui_dom_create_element(hui_ctx *ctx, const char *tag_utf8);

hui_node_handle hui_dom_create_text(hui_ctx *ctx, const char *text_utf8);

void hui_mark_dirty(hui_ctx *ctx, hui_node_handle h, uint32_t flags);

int hui_restyle_and_relayout(hui_ctx *ctx, const hui_build_opts *opts);

const char *hui_last_error(hui_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
