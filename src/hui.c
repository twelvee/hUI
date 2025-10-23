#include "../include/hui/hui.h"

#include "hui_err.h"
#include "hui_arena.h"
#include "hui_intern.h"
#include "hui_vec.h"
#include "html/hui_html_builder.h"
#include "css/hui_css_parser.h"
#include "style/hui_style.h"
#include "layout/hui_layout.h"
#include "paint/hui_paint.h"
#include "ir/hui_ir.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>

typedef struct {
    hui_atom name;
    hui_binding_type type;

    union {
        int32_t *i32;
        float *f32;

        struct {
            char *ptr;
            size_t capacity;
        } str;

        void *ptr;
    } target;

    union {
        int32_t i32;
        float f32;
    } cached;

    size_t string_cap;
    char *string_value;
    size_t cached_length;
    HUI_VEC(uint32_t) text_nodes;

    HUI_VEC(uint32_t) input_nodes;
} hui_binding_entry;

typedef struct {
    hui_text_field field;
    char *buffer;
    size_t buffer_capacity;
    char *placeholder;
    uint32_t dom_index;
    uint32_t binding_index;
} hui_auto_text_field;

typedef struct {
    size_t start;
    size_t end;
    hui_atom atom;
} hui_bound_text_token;

typedef struct {
    uint32_t node_index;
   char *template_str;
    size_t template_len;
    char *rendered;
    size_t rendered_len;
    size_t rendered_cap;
    HUI_VEC(hui_bound_text_token) tokens;
} hui_bound_text_template;

typedef struct {
    hui_font_resource resource;
    hui_atom family_atom;
    char *src_path;
    hui_font_id id;
} hui_font_entry;

static void hui_auto_text_fields_reset(hui_ctx *ctx);

static int hui_auto_text_fields_rebuild(hui_ctx *ctx);

static uint32_t hui_auto_text_fields_step(hui_ctx *ctx, float dt);

typedef struct {
    hui_node_handle option;
    hui_atom value_atom;
    char *label;
    size_t label_len;
} hui_select_option;

typedef struct {
    hui_node_handle node;
    hui_node_handle display_node;
    hui_node_handle display_text;
    hui_node_handle menu_node;
    uint32_t binding_index;
    int selected_index;
    int open;
    int hovered_index;
    HUI_VEC(hui_select_option) options;
} hui_auto_select_field;

static void hui_auto_select_fields_reset(hui_ctx *ctx);

static int hui_auto_select_fields_rebuild(hui_ctx *ctx);

static uint32_t hui_auto_select_fields_step(hui_ctx *ctx, float dt);

static uint32_t hui_auto_select_apply_selection(hui_ctx *ctx, hui_auto_select_field *field,
                                                int new_index, int from_binding);

static int hui_node_text_entry_info(hui_ctx *ctx, const hui_dom_node *node, int *out_multiline);

static void hui_binding_prepare_dom(hui_ctx *ctx);

static int hui_binding_find_entry(const hui_ctx *ctx, hui_atom name);

static void hui_binding_relink_entry(hui_ctx *ctx, size_t entry_index);

static uint32_t hui_binding_apply_text_nodes(hui_ctx *ctx, hui_binding_entry *entry);

static uint32_t hui_binding_apply_inputs_from_binding(hui_ctx *ctx, size_t entry_index, int force);

static uint32_t hui_binding_sync_ctx(hui_ctx *ctx);

static hui_atom hui_binding_atom_from_mustache(hui_ctx *ctx, const char *text, size_t len);

static hui_atom hui_binding_atom_from_attr(hui_ctx *ctx, const char *text, size_t len);

static int hui_binding_push_from_string(hui_ctx *ctx, size_t entry_index, const char *text, int *string_applied);

static hui_auto_text_field *hui_find_auto_field(hui_ctx *ctx, uint32_t dom_index);

static void hui_bound_texts_reset(hui_ctx *ctx);

static size_t hui_bound_text_prepare_node(hui_ctx *ctx, uint32_t node_index, hui_dom_node *node);

static uint32_t hui_bound_text_apply_index(hui_ctx *ctx, uint32_t template_index);

static const char *hui_binding_string_for_atom(const hui_ctx *ctx, hui_atom atom, size_t *len);

static void hui_ctx_accumulate_dirty(hui_ctx *ctx, uint32_t flags);

static hui_build_opts hui_normalize_build_opts(const hui_build_opts *opts);

static int hui_render_opts_equal(const hui_ctx *ctx, const hui_build_opts *opts);

void hui_dom_invalidate_text_cache(hui_dom_node *node) {
    if (!node) return;
    node->text_cache_valid = 0;
    node->text_cache_cp = 0;
    node->text_cache_lines = 0;
    node->text_cache_max_cols = 0;
}

void hui_dom_text_cache_refresh(hui_dom_node *node) {
    if (!node || node->type != HUI_NODE_TEXT) return;
    if (node->text_cache_valid) return;
    const char *text = node->text ? node->text : "";
    size_t len = node->text_len;
    uint32_t cp = 0;
    uint32_t lines = 1;
    uint32_t cols = 0;
    uint32_t max_cols = 0;
    size_t pos = 0;
    while (pos < len) {
        unsigned char ch = (unsigned char) text[pos];
        size_t advance = 1;
        while (pos + advance < len && ((unsigned char) text[pos + advance] & 0xC0u) == 0x80u) advance++;
        if (ch == '\r') {
            pos += advance;
            continue;
        }
        cp++;
        if (ch == '\n') {
            if (cols > max_cols) max_cols = cols;
            cols = 0;
            lines++;
        } else {
            cols++;
        }
        pos += advance;
    }
    if (cols > max_cols) max_cols = cols;
    if (len == 0) {
        lines = 1;
        cols = 0;
        max_cols = 0;
        cp = 0;
    }
    node->text_cache_cp = cp;
    node->text_cache_lines = lines;
    node->text_cache_max_cols = max_cols;
    node->text_cache_valid = 1;
}

static void hui_render_cache_store(hui_ctx *ctx, const hui_build_opts *opts);

static uint32_t hui_hit_test(const hui_ctx *ctx, float x, float y);

static uint32_t hui_hit_test_subtree(const hui_ctx *ctx, uint32_t idx, float x, float y);

static uint32_t hui_hit_test_siblings(const hui_ctx *ctx, uint32_t idx, float x, float y);

static int hui_path_is_absolute(const char *path) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    if ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/'))
        return 1;
    if (isalpha((unsigned char) path[0]) && path[1] == ':')
        return 1;
#else
    if (path[0] == '/')
        return 1;
#endif
    return 0;
}

static FILE *hui_open_asset_stream(const char *base_dir, const char *path) {
    if (!path || !path[0]) return NULL;
    FILE *f = fopen(path, "rb");
    if (f) return f;
    if (!base_dir || !base_dir[0]) return NULL;
    if (hui_path_is_absolute(path)) return NULL;
    char combined[1024];
    size_t base_len = strlen(base_dir);
    int needs_sep = (base_len > 0) &&
                    base_dir[base_len - 1] != '/' &&
                    base_dir[base_len - 1] != '\\';
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (needs_sep) {
        snprintf(combined, sizeof(combined), "%s%c%s", base_dir, sep, path);
    } else {
        snprintf(combined, sizeof(combined), "%s%s", base_dir, path);
    }
    f = fopen(combined, "rb");
    return f;
}

struct hui_ctx {
    void * (*alloc_fn)(size_t);

    void (*free_fn)(void *);

    char *html;
    size_t html_len;
    size_t html_cap;
    char *css;
    size_t css_len;
    size_t css_cap;
    int html_finished;
    int css_finished;
    hui_intern atoms;
    hui_dom dom;
    hui_stylesheet stylesheet;
    hui_style_store styles;
    hui_draw_list draw;
    HUI_VEC(hui_font_entry) fonts;
    hui_dom_filter_fn filter_fn;
    void *filter_user;
    hui_filter_spec filter_spec;
    int filter_spec_enabled;
    uint32_t prop_mask;
    float pointer_x;
    float pointer_y;
    uint32_t pointer_buttons;
    uint32_t pointer_buttons_prev;
    uint32_t pointer_pressed;
    uint32_t pointer_released;
    uint32_t hovered_node;
    int pointer_active;
    uint32_t key_modifiers;
    hui_node_handle focus_node;
    HUI_VEC(hui_input_event) input_events;

    HUI_VEC(uint32_t) text_input;

    HUI_VEC(uint32_t) key_pressed;

    HUI_VEC(uint32_t) key_released;

    HUI_VEC(uint32_t) key_held;

    hui_input_state input_state;
    HUI_VEC(hui_binding_entry) bindings;

    HUI_VEC(hui_auto_text_field) auto_text_fields;

    HUI_VEC(hui_auto_select_field) auto_select_fields;

    HUI_VEC(hui_bound_text_template) bound_texts;

    struct {
        hui_clipboard_iface clipboard;
        int clipboard_set;
        hui_text_field_keymap keymap;
        int keymap_set;
        float backspace_initial_delay;
        float backspace_repeat_delay;
        int delays_set;
        size_t buffer_capacity;
    } text_input_defaults;

    char asset_base[512];

    uint32_t dirty_flags;
    int draw_valid;
    uint64_t draw_version;

    struct {
        float viewport_w;
        float viewport_h;
        float dpi;
        uint32_t flags;
        int valid;
    } render_cache;

    char last_error[256];
};

static void hui_set_error(hui_ctx *ctx, const char *msg) {
    snprintf(ctx->last_error, sizeof(ctx->last_error), "%s", msg);
}

static void hui_ctx_accumulate_dirty(hui_ctx *ctx, uint32_t flags) {
    if (!ctx) return;
    uint32_t masked = flags & HUI_DIRTY_ALL;
    if (masked == 0) return;
    ctx->dirty_flags |= masked;
    ctx->draw_valid = 0;
}

static void hui_binding_push_node_unique(void *vec_ptr, uint32_t node_index) {
    if (!vec_ptr) return;
    struct hui_vec_u32 {
        uint32_t *data;
        size_t len;
        size_t cap;
    };
    struct hui_vec_u32 *vec = (struct hui_vec_u32 *) vec_ptr;
    for (size_t i = 0; i < vec->len; i++) {
        if (vec->data[i] == node_index) return;
    }
    hui_vec_push(vec, node_index);
}

static const char *hui_binding_string_for_atom(const hui_ctx *ctx, hui_atom atom, size_t *len) {
    if (len) *len = 0;
    if (!ctx || atom == 0) return "";
    int entry_index = hui_binding_find_entry(ctx, atom);
    if (entry_index < 0 || (size_t) entry_index >= ctx->bindings.len) return "";
    const hui_binding_entry *entry = &ctx->bindings.data[entry_index];
    if (!entry->string_value) return "";
    if (len) *len = strlen(entry->string_value);
    return entry->string_value;
}

static void hui_bound_texts_reset(hui_ctx *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->bound_texts.len; i++) {
        hui_bound_text_template *templ = &ctx->bound_texts.data[i];
        if (templ->template_str) ctx->free_fn(templ->template_str);
        if (templ->rendered) ctx->free_fn(templ->rendered);
        hui_vec_free(&templ->tokens);
        templ->template_str = NULL;
        templ->rendered = NULL;
        templ->template_len = 0;
        templ->rendered_len = 0;
        templ->rendered_cap = 0;
    }
    ctx->bound_texts.len = 0;
}

static uint32_t hui_bound_text_apply_index(hui_ctx *ctx, uint32_t template_index) {
    if (!ctx || template_index >= ctx->bound_texts.len) return 0;
    hui_bound_text_template *templ = &ctx->bound_texts.data[template_index];
    if (!templ->template_str) return 0;
    if (templ->node_index >= ctx->dom.nodes.len) return 0;
    const char *src = templ->template_str;
    size_t cursor = 0;
    size_t required = 0;
    for (size_t i = 0; i < templ->tokens.len; i++) {
        hui_bound_text_token *tok = &templ->tokens.data[i];
        if (tok->start > cursor) required += tok->start - cursor;
        size_t value_len = 0;
        (void) hui_binding_string_for_atom(ctx, tok->atom, &value_len);
        required += value_len;
        cursor = tok->end;
    }
    required += templ->template_len - cursor;
    if (templ->rendered_cap < required + 1) {
        size_t new_cap = required + 1;
        char *new_buf = (char *) ctx->alloc_fn(new_cap);
        if (!new_buf) return 0;
        if (templ->rendered) ctx->free_fn(templ->rendered);
        templ->rendered = new_buf;
        templ->rendered_cap = new_cap;
    }
    cursor = 0;
    size_t out = 0;
    for (size_t i = 0; i < templ->tokens.len; i++) {
        hui_bound_text_token *tok = &templ->tokens.data[i];
        if (tok->start > cursor) {
            size_t lit_len = tok->start - cursor;
            memcpy(templ->rendered + out, src + cursor, lit_len);
            out += lit_len;
        }
        size_t value_len = 0;
        const char *value = hui_binding_string_for_atom(ctx, tok->atom, &value_len);
        if (value_len > 0) {
            memcpy(templ->rendered + out, value, value_len);
            out += value_len;
        }
        cursor = tok->end;
    }
    if (templ->template_len > cursor) {
        size_t tail = templ->template_len - cursor;
        memcpy(templ->rendered + out, src + cursor, tail);
        out += tail;
    }
    templ->rendered[out] = '\0';
    templ->rendered_len = out;
    hui_dom_node *node = &ctx->dom.nodes.data[templ->node_index];
    node->text = templ->rendered;
    node->text_len = (uint32_t) out;
    hui_dom_invalidate_text_cache(node);
    return HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
}

static size_t hui_bound_text_prepare_node(hui_ctx *ctx, uint32_t node_index, hui_dom_node *node) {
    if (!ctx || !node || node->type != HUI_NODE_TEXT || !node->text) return 0;
    const char *text = node->text;
    size_t len = node->text_len;
    if (len < 4) return 0;
    HUI_VEC(hui_bound_text_token) tokens;
    hui_vec_init(&tokens);
    size_t pos = 0;
    while (pos + 1 < len) {
        if (text[pos] == '{' && pos + 1 < len && text[pos + 1] == '{') {
            size_t close = pos + 2;
            while (close + 1 < len && !(text[close] == '}' && text[close + 1] == '}')) close++;
            if (close + 1 >= len) break;
            size_t segment_len = (close + 2) - pos;
            hui_atom atom = hui_binding_atom_from_mustache(ctx, text + pos, segment_len);
            if (atom) {
                hui_bound_text_token tok;
                tok.start = pos;
                tok.end = close + 2;
                tok.atom = atom;
                hui_vec_push(&tokens, tok);
            }
            pos = close + 2;
        } else {
            pos++;
        }
    }
    if (tokens.len == 0) {
        hui_vec_free(&tokens);
        return 0;
    }
    size_t trimmed_start = 0;
    size_t trimmed_end = len;
    while (trimmed_start < trimmed_end && isspace((unsigned char) text[trimmed_start])) trimmed_start++;
    while (trimmed_end > trimmed_start && isspace((unsigned char) text[trimmed_end - 1])) trimmed_end--;
    if (tokens.len == 1) {
        hui_bound_text_token tok = tokens.data[0];
        if (tok.start == trimmed_start && tok.end == trimmed_end) {
            node->binding_text_atom = tok.atom;
            node->text = "";
            node->text_len = 0;
            hui_dom_invalidate_text_cache(node);
            hui_vec_free(&tokens);
            return 1;
        }
    }

    hui_bound_text_template templ;
    memset(&templ, 0, sizeof(templ));
    templ.node_index = node_index;
    templ.template_len = len;
    templ.template_str = (char *) ctx->alloc_fn(len + 1);
    if (!templ.template_str) {
        hui_vec_free(&tokens);
        return tokens.len;
    }
    memcpy(templ.template_str, text, len);
    templ.template_str[len] = '\0';
    hui_vec_init(&templ.tokens);
    for (size_t i = 0; i < tokens.len; i++) {
        hui_vec_push(&templ.tokens, tokens.data[i]);
    }
    templ.rendered = NULL;
    templ.rendered_len = 0;
    templ.rendered_cap = 0;

    uint32_t template_index = (uint32_t) ctx->bound_texts.len;
    hui_vec_push(&ctx->bound_texts, templ);
    node->binding_template_index = template_index;
    node->text = "";
    node->text_len = 0;
    hui_dom_invalidate_text_cache(node);
    hui_vec_free(&tokens);

    hui_bound_text_apply_index(ctx, template_index);
    return ctx->bound_texts.data[template_index].tokens.len;
}

static hui_build_opts hui_normalize_build_opts(const hui_build_opts *opts) {
    hui_build_opts normalized;
    normalized.viewport_w = (opts && opts->viewport_w > 0.0f) ? opts->viewport_w : 800.0f;
    normalized.viewport_h = (opts && opts->viewport_h > 0.0f) ? opts->viewport_h : 600.0f;
    normalized.dpi = (opts && opts->dpi > 0.0f) ? opts->dpi : 96.0f;
    normalized.flags = opts ? opts->flags : 0u;
    return normalized;
}

static int hui_render_opts_equal(const hui_ctx *ctx, const hui_build_opts *opts) {
    if (!ctx || !opts) return 0;
    if (!ctx->render_cache.valid) return 0;
    return ctx->render_cache.viewport_w == opts->viewport_w &&
           ctx->render_cache.viewport_h == opts->viewport_h &&
           ctx->render_cache.dpi == opts->dpi &&
           ctx->render_cache.flags == opts->flags;
}

static void hui_render_cache_store(hui_ctx *ctx, const hui_build_opts *opts) {
    if (!ctx || !opts) return;
    ctx->render_cache.viewport_w = opts->viewport_w;
    ctx->render_cache.viewport_h = opts->viewport_h;
    ctx->render_cache.dpi = opts->dpi;
    ctx->render_cache.flags = opts->flags;
    ctx->render_cache.valid = 1;
}

static void hui_fonts_reset(hui_ctx *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->fonts.len; i++) {
        hui_font_entry *entry = &ctx->fonts.data[i];
        if (entry->resource.family) {
            ctx->free_fn((void *) entry->resource.family);
            entry->resource.family = NULL;
        }
        if (entry->resource.data) {
            ctx->free_fn((void *) entry->resource.data);
            entry->resource.data = NULL;
        }
        entry->resource.size = 0;
        if (entry->src_path) {
            ctx->free_fn(entry->src_path);
            entry->src_path = NULL;
        }
        entry->family_atom = 0;
        entry->id = HUI_FONT_ID_NONE;
    }
    ctx->fonts.len = 0;
}

static int hui_fonts_load_from_stylesheet(hui_ctx *ctx) {
    if (!ctx) return HUI_EINVAL;
    hui_fonts_reset(ctx);
    for (size_t i = 0; i < ctx->stylesheet.font_faces.len; i++) {
        const hui_css_font_face *face = &ctx->stylesheet.font_faces.data[i];
        if (!face->family_name || !face->src || face->src_len == 0)
            continue;
        FILE *f = hui_open_asset_stream(ctx->asset_base, face->src);
        if (!f) {
            fprintf(stderr, "[hui] warning: font file '%s' not found (skipping @font-face)\n", face->src);
            continue;
        }
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            hui_set_error(ctx, "failed to seek font file");
            hui_fonts_reset(ctx);
            return HUI_EPARSE;
        }
        long file_size = ftell(f);
        if (file_size <= 0) {
            fclose(f);
            continue;
        }
        if (fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            hui_set_error(ctx, "failed to rewind font file");
            hui_fonts_reset(ctx);
            return HUI_EPARSE;
        }
        size_t size = (size_t) file_size;
        uint8_t *buffer = (uint8_t *) ctx->alloc_fn(size);
        if (!buffer) {
            fclose(f);
            hui_set_error(ctx, "out of memory loading font");
            hui_fonts_reset(ctx);
            return HUI_ENOMEM;
        }
        size_t read_bytes = fread(buffer, 1, size, f);
        fclose(f);
        if (read_bytes != size) {
            ctx->free_fn(buffer);
            hui_set_error(ctx, "failed to read full font file");
            hui_fonts_reset(ctx);
            return HUI_EPARSE;
        }
        size_t family_len = strlen(face->family_name);
        char *family_copy = (char *) ctx->alloc_fn(family_len + 1);
        if (!family_copy) {
            ctx->free_fn(buffer);
            hui_fonts_reset(ctx);
            return HUI_ENOMEM;
        }
        memcpy(family_copy, face->family_name, family_len + 1);
        size_t src_len = face->src_len ? face->src_len : strlen(face->src);
        char *src_copy = (char *) ctx->alloc_fn(src_len + 1);
        if (!src_copy) {
            ctx->free_fn(buffer);
            ctx->free_fn(family_copy);
            hui_fonts_reset(ctx);
            return HUI_ENOMEM;
        }
        memcpy(src_copy, face->src, src_len);
        src_copy[src_len] = '\0';

        hui_font_entry entry;
        memset(&entry, 0, sizeof(entry));
        entry.id = (hui_font_id) ctx->fonts.len;
        entry.family_atom = face->family_atom;
        entry.src_path = src_copy;
        entry.resource.family = family_copy;
        entry.resource.weight = face->weight ? face->weight : 400;
        entry.resource.style = (face->style == 1) ? HUI_FONT_STYLE_ITALIC : HUI_FONT_STYLE_NORMAL;
        entry.resource.data = buffer;
        entry.resource.size = read_bytes;
        hui_vec_push(&ctx->fonts, entry);
    }
    return HUI_OK;
}

static hui_font_id hui_fonts_match(const hui_ctx *ctx, hui_atom family, uint32_t weight, uint32_t style) {
    if (!ctx || ctx->fonts.len == 0) return HUI_FONT_ID_NONE;
    uint32_t target_style = style;
    int prefer_family = family != 0;
    hui_font_id best_id = HUI_FONT_ID_NONE;
    uint32_t best_style_penalty = UINT32_MAX;
    uint32_t best_weight_diff = UINT32_MAX;
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < ctx->fonts.len; i++) {
            const hui_font_entry *entry = &ctx->fonts.data[i];
            if (prefer_family && entry->family_atom != family) continue;
            uint32_t entry_style = (uint32_t) entry->resource.style;
            uint32_t style_penalty = (entry_style == target_style) ? 0u : 1u;
            uint32_t entry_weight = entry->resource.weight ? entry->resource.weight : 400u;
            uint32_t weight_diff = (entry_weight > weight)
                                       ? (entry_weight - weight)
                                       : (weight - entry_weight);
            if (style_penalty < best_style_penalty ||
                (style_penalty == best_style_penalty && weight_diff < best_weight_diff)) {
                best_style_penalty = style_penalty;
                best_weight_diff = weight_diff;
                best_id = entry->id;
            }
        }
        if (best_id != HUI_FONT_ID_NONE) break;
        if (!prefer_family) break;
        prefer_family = 0;
        best_style_penalty = UINT32_MAX;
        best_weight_diff = UINT32_MAX;
    }
    return best_id;
}

static void hui_style_assign_fonts(hui_ctx *ctx) {
    if (!ctx) return;
    size_t count = ctx->styles.styles.len;
    for (size_t i = 0; i < count; i++) {
        hui_computed_style *cs = &ctx->styles.styles.data[i];
        cs->font_id = hui_fonts_match(ctx, cs->font_family, cs->font_weight, cs->font_style);
    }
}

hui_ctx *hui_create(void * (*alloc_fn)(size_t), void (*free_fn)(void *)) {
    void * (*afn)(size_t) = alloc_fn ? alloc_fn : malloc;
    void (*ffn)(void *) = free_fn ? free_fn : free;
    hui_ctx *ctx = (hui_ctx *) afn(sizeof(hui_ctx));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(*ctx));
    ctx->alloc_fn = afn;
    ctx->free_fn = ffn;
    hui_intern_init(&ctx->atoms);
    hui_dom_init(&ctx->dom);
    hui_css_init(&ctx->stylesheet);
    hui_style_store_init(&ctx->styles);
    hui_draw_list_init(&ctx->draw);
    hui_vec_init(&ctx->fonts);
    hui_vec_init(&ctx->bindings);
    hui_vec_init(&ctx->auto_text_fields);
    hui_vec_init(&ctx->auto_select_fields);
    hui_vec_init(&ctx->bound_texts);
    ctx->filter_spec.max_depth = -1;
    ctx->filter_spec.max_nodes = 0;
    ctx->filter_spec.flags = 0;
    ctx->filter_spec_enabled = 0;
    ctx->prop_mask = HUI_PROP_ALL;
    ctx->pointer_x = 0.0f;
    ctx->pointer_y = 0.0f;
    ctx->pointer_buttons = 0;
    ctx->pointer_buttons_prev = 0;
    ctx->pointer_pressed = 0;
    ctx->pointer_released = 0;
    ctx->hovered_node = 0xFFFFFFFFu;
    ctx->pointer_active = 0;
    ctx->key_modifiers = 0;
    ctx->focus_node = HUI_NODE_NULL;
    hui_vec_init(&ctx->input_events);
    hui_vec_init(&ctx->text_input);
    hui_vec_init(&ctx->key_pressed);
    hui_vec_init(&ctx->key_released);
    hui_vec_init(&ctx->key_held);
    memset(&ctx->input_state, 0, sizeof(ctx->input_state));
    ctx->input_state.hovered = HUI_NODE_NULL;
    ctx->input_state.focus = HUI_NODE_NULL;
    ctx->text_input_defaults.clipboard_set = 0;
    ctx->text_input_defaults.keymap_set = 0;
    ctx->text_input_defaults.backspace_initial_delay = 0.0f;
    ctx->text_input_defaults.backspace_repeat_delay = 0.0f;
    ctx->text_input_defaults.delays_set = 0;
    ctx->text_input_defaults.buffer_capacity = 256;
    ctx->asset_base[0] = '\0';
    ctx->dirty_flags = HUI_DIRTY_ALL;
    ctx->draw_valid = 0;
    ctx->draw_version = 0;
    ctx->render_cache.viewport_w = 0.0f;
    ctx->render_cache.viewport_h = 0.0f;
    ctx->render_cache.dpi = 0.0f;
    ctx->render_cache.flags = 0;
    ctx->render_cache.valid = 0;
    return ctx;
}

static void hui_auto_text_fields_reset(hui_ctx *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->auto_text_fields.len; i++) {
        hui_auto_text_field *auto_field = &ctx->auto_text_fields.data[i];
        if (auto_field->buffer) {
            ctx->free_fn(auto_field->buffer);
            auto_field->buffer = NULL;
        }
        if (auto_field->placeholder) {
            ctx->free_fn(auto_field->placeholder);
            auto_field->placeholder = NULL;
        }
        auto_field->buffer_capacity = 0;
        auto_field->dom_index = 0xFFFFFFFFu;
        auto_field->binding_index = 0xFFFFFFFFu;
        memset(&auto_field->field, 0, sizeof(auto_field->field));
    }
    ctx->auto_text_fields.len = 0;
}

static int hui_node_text_entry_info(hui_ctx *ctx, const hui_dom_node *node, int *out_multiline) {
    if (!ctx || !node) return 0;
    if (node->type != HUI_NODE_ELEM) return 0;

    hui_atom textarea_atom = hui_intern_put(&ctx->atoms, "textarea", strlen("textarea"));
    if (node->tag == textarea_atom) {
        if (out_multiline) *out_multiline = 1;
        return 1;
    }

    hui_atom input_atom = hui_intern_put(&ctx->atoms, "input", strlen("input"));
    if (node->tag != input_atom) return 0;

    if (out_multiline) *out_multiline = 0;
    if (node->attr_type == 0) return 1;

    hui_atom text_atom = hui_intern_put(&ctx->atoms, "text", strlen("text"));
    hui_atom email_atom = hui_intern_put(&ctx->atoms, "email", strlen("email"));
    hui_atom password_atom = hui_intern_put(&ctx->atoms, "password", strlen("password"));
    hui_atom search_atom = hui_intern_put(&ctx->atoms, "search", strlen("search"));
    hui_atom tel_atom = hui_intern_put(&ctx->atoms, "tel", strlen("tel"));
    hui_atom url_atom = hui_intern_put(&ctx->atoms, "url", strlen("url"));
    hui_atom number_atom = hui_intern_put(&ctx->atoms, "number", strlen("number"));
    return node->attr_type == text_atom ||
           node->attr_type == email_atom ||
           node->attr_type == password_atom ||
           node->attr_type == search_atom ||
           node->attr_type == tel_atom ||
           node->attr_type == url_atom ||
           node->attr_type == number_atom;
}

static int hui_auto_text_fields_rebuild(hui_ctx *ctx) {
    if (!ctx) return HUI_EINVAL;
    size_t old_len = ctx->auto_text_fields.len;
    size_t reuse_index = 0;
    size_t capacity_default = ctx->text_input_defaults.buffer_capacity;
    if (capacity_default < 16) capacity_default = 256;

    if (ctx->dom.nodes.len == 0) {
        for (size_t i = 0; i < old_len; i++) {
            hui_auto_text_field *field = &ctx->auto_text_fields.data[i];
            if (field->buffer) {
                ctx->free_fn(field->buffer);
                field->buffer = NULL;
            }
            if (field->placeholder) {
                ctx->free_fn(field->placeholder);
                field->placeholder = NULL;
            }
        }
        ctx->auto_text_fields.len = 0;
        return HUI_OK;
    }

    int status = HUI_OK;

    for (size_t i = 0; i < ctx->dom.nodes.len; i++) {
        hui_dom_node *node = &ctx->dom.nodes.data[i];
        int multiline = 0;
        if (!hui_node_text_entry_info(ctx, node, &multiline)) continue;

        uint32_t binding_index = node->binding_value_index;
        const char *initial_text_src = NULL;
        size_t initial_text_len = 0;
        if (binding_index != 0xFFFFFFFFu && binding_index < ctx->bindings.len) {
            hui_binding_entry *binding = &ctx->bindings.data[binding_index];
            if (binding->type == HUI_BIND_STRING && binding->string_value) {
                initial_text_src = binding->string_value;
                initial_text_len = binding->cached_length;
            }
        } else if (node->attr_value && node->attr_value_len > 0 && node->binding_value_atom == 0) {
            initial_text_src = node->attr_value;
            initial_text_len = node->attr_value_len;
        } else {
            hui_node_handle elem = (hui_node_handle){(uint32_t) i, node->gen};
            hui_node_handle child = hui_node_first_child(ctx, elem);
            while (!hui_node_is_null(child) && !hui_node_is_text(ctx, child))
                child = hui_node_next_sibling(ctx, child);
            if (!hui_node_is_null(child)) {
                hui_dom_node *text_node = &ctx->dom.nodes.data[child.index];
                if (text_node->text && text_node->text_len > 0) {
                    initial_text_src = text_node->text;
                    initial_text_len = text_node->text_len;
                }
            }
        }

        size_t required_capacity = capacity_default;
        if (initial_text_len + 1 > required_capacity)
            required_capacity = initial_text_len + 1;
        if (binding_index != 0xFFFFFFFFu && binding_index < ctx->bindings.len) {
            hui_binding_entry *binding = &ctx->bindings.data[binding_index];
            if (binding->type == HUI_BIND_STRING && binding->string_cap > required_capacity)
                required_capacity = binding->string_cap;
        }

        hui_auto_text_field *auto_field = NULL;
        if (reuse_index < old_len) {
            auto_field = &ctx->auto_text_fields.data[reuse_index];
        } else {
            hui_auto_text_field fresh;
            memset(&fresh, 0, sizeof(fresh));
            fresh.dom_index = 0xFFFFFFFFu;
            fresh.binding_index = 0xFFFFFFFFu;
            hui_vec_push(&ctx->auto_text_fields, fresh);
            auto_field = &ctx->auto_text_fields.data[reuse_index];
        }
        reuse_index++;

        if (!auto_field->buffer || auto_field->buffer_capacity < required_capacity) {
            char *new_buffer = (char *) ctx->alloc_fn(required_capacity);
            if (!new_buffer) {
                status = HUI_ENOMEM;
                goto rebuild_fail;
            }
            if (auto_field->buffer) ctx->free_fn(auto_field->buffer);
            auto_field->buffer = new_buffer;
            auto_field->buffer_capacity = required_capacity;
        }
        auto_field->buffer[0] = '\0';

        if (node->attr_placeholder && node->attr_placeholder_len > 0) {
            size_t existing_len = auto_field->placeholder ? strlen(auto_field->placeholder) : 0;
            if (existing_len != node->attr_placeholder_len ||
                (existing_len > 0 && memcmp(auto_field->placeholder,
                                            node->attr_placeholder,
                                            existing_len) != 0)) {
                char *new_placeholder = (char *) ctx->alloc_fn(node->attr_placeholder_len + 1);
                if (!new_placeholder) {
                    status = HUI_ENOMEM;
                    goto rebuild_fail;
                }
                memcpy(new_placeholder, node->attr_placeholder, node->attr_placeholder_len);
                new_placeholder[node->attr_placeholder_len] = '\0';
                if (auto_field->placeholder) ctx->free_fn(auto_field->placeholder);
                auto_field->placeholder = new_placeholder;
            }
        } else if (auto_field->placeholder) {
            ctx->free_fn(auto_field->placeholder);
            auto_field->placeholder = NULL;
        }

        memset(&auto_field->field, 0, sizeof(auto_field->field));
        hui_text_field_desc desc;
        memset(&desc, 0, sizeof(desc));
        desc.container = (hui_node_handle){(uint32_t) i, node->gen};
        desc.value = desc.container;
        desc.placeholder = auto_field->placeholder;
        desc.buffer = auto_field->buffer;
        desc.buffer_capacity = auto_field->buffer_capacity;
        desc.flags = multiline ? HUI_TEXT_FIELD_FLAG_MULTI_LINE : HUI_TEXT_FIELD_FLAG_NONE;
        if (ctx->text_input_defaults.clipboard_set)
            desc.clipboard = &ctx->text_input_defaults.clipboard;
        if (ctx->text_input_defaults.keymap_set)
            desc.keymap = &ctx->text_input_defaults.keymap;
        if (ctx->text_input_defaults.delays_set) {
            desc.backspace_initial_delay = ctx->text_input_defaults.backspace_initial_delay;
            desc.backspace_repeat_delay = ctx->text_input_defaults.backspace_repeat_delay;
        }

        if (hui_text_field_init(ctx, &auto_field->field, &desc) != HUI_OK) {
            status = HUI_EINVAL;
            goto rebuild_fail;
        }

        auto_field->dom_index = (uint32_t) i;
        auto_field->binding_index = binding_index;

        if (binding_index != 0xFFFFFFFFu && binding_index < ctx->bindings.len) {
            hui_binding_entry *entry = &ctx->bindings.data[binding_index];
            if (entry->string_value)
                hui_text_field_set_text(ctx, &auto_field->field, entry->string_value);
        } else if (initial_text_src && initial_text_len > 0) {
            if (initial_text_len + 1 > auto_field->buffer_capacity) {
                char *new_buffer = (char *) ctx->alloc_fn(initial_text_len + 1);
                if (!new_buffer) {
                    status = HUI_ENOMEM;
                    goto rebuild_fail;
                }
                ctx->free_fn(auto_field->buffer);
                auto_field->buffer = new_buffer;
                auto_field->buffer_capacity = initial_text_len + 1;
                desc.buffer = auto_field->buffer;
                desc.buffer_capacity = auto_field->buffer_capacity;
                memset(&auto_field->field, 0, sizeof(auto_field->field));
                if (hui_text_field_init(ctx, &auto_field->field, &desc) != HUI_OK) {
                    status = HUI_EINVAL;
                    goto rebuild_fail;
                }
            }
            memcpy(auto_field->buffer, initial_text_src, initial_text_len);
            auto_field->buffer[initial_text_len] = '\0';
            hui_text_field_set_text(ctx, &auto_field->field, auto_field->buffer);
        }
    }

    for (size_t i = reuse_index; i < old_len; i++) {
        hui_auto_text_field *field = &ctx->auto_text_fields.data[i];
        if (field->buffer) {
            ctx->free_fn(field->buffer);
            field->buffer = NULL;
        }
        if (field->placeholder) {
            ctx->free_fn(field->placeholder);
            field->placeholder = NULL;
        }
    }
    ctx->auto_text_fields.len = reuse_index;

    hui_ctx_accumulate_dirty(ctx, HUI_DIRTY_STYLE | HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
    return HUI_OK;

rebuild_fail:
    hui_auto_text_fields_reset(ctx);
    hui_auto_select_fields_reset(ctx);
    return status;
}

static void hui_auto_select_option_free(hui_ctx *ctx, hui_select_option *opt) {
    if (!ctx || !opt) return;
    if (opt->label) {
        ctx->free_fn(opt->label);
        opt->label = NULL;
    }
    opt->label_len = 0;
    opt->option = HUI_NODE_NULL;
    opt->value_atom = 0;
}

static void hui_auto_select_fields_reset(hui_ctx *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->auto_select_fields.len; i++) {
        hui_auto_select_field *field = &ctx->auto_select_fields.data[i];
        for (size_t j = 0; j < field->options.len; j++) {
            hui_auto_select_option_free(ctx, &field->options.data[j]);
        }
        hui_vec_free(&field->options);
        field->selected_index = -1;
        field->binding_index = 0xFFFFFFFFu;
        field->node = HUI_NODE_NULL;
        field->display_node = HUI_NODE_NULL;
        field->display_text = HUI_NODE_NULL;
        field->menu_node = HUI_NODE_NULL;
        field->open = 0;
        field->hovered_index = -1;
    }
    ctx->auto_select_fields.len = 0;
}

static int hui_auto_select_find_value_index(hui_ctx *ctx, const hui_auto_select_field *field,
                                            const char *value, size_t len) {
    if (!ctx || !field || !value) return -1;
    for (size_t i = 0; i < field->options.len; i++) {
        const hui_select_option *opt = &field->options.data[i];
        uint32_t opt_len = 0;
        const char *opt_str = hui_intern_str(&ctx->atoms, opt->value_atom, &opt_len);
        if (opt_len == len && (opt_len == 0 || memcmp(opt_str, value, len) == 0))
            return (int) i;
    }
    return -1;
}

static uint32_t hui_select_apply_visibility(hui_ctx *ctx, hui_auto_select_field *field) {
    if (!ctx || !field) return 0;
    size_t styles_len = ctx->styles.styles.len;
    if (styles_len == 0) return 0;
    uint32_t dirty = 0;
    uint32_t desired_menu = field->open ? 1u : 0u;
    if (!hui_node_is_null(field->menu_node) && field->menu_node.index < styles_len) {
        hui_computed_style *menu_style = &ctx->styles.styles.data[field->menu_node.index];
        if (menu_style->display != desired_menu) {
            menu_style->display = desired_menu;
            menu_style->present_mask |= HUI_STYLE_PRESENT_DISPLAY;
            dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
        }
    }
    return dirty;
}

static void hui_auto_select_refresh_visibility(hui_ctx *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->auto_select_fields.len; i++)
        hui_select_apply_visibility(ctx, &ctx->auto_select_fields.data[i]);
}

static void hui_select_update_hover(hui_ctx *ctx, hui_auto_select_field *field, int new_hover) {
    if (!ctx || !field) return;
    if (field->hovered_index == new_hover) return;
    if (field->hovered_index >= 0 && (size_t) field->hovered_index < field->options.len) {
        hui_select_option *opt = &field->options.data[field->hovered_index];
        hui_dom_remove_class(ctx, opt->option, "hui-select-option-hover");
    }
    field->hovered_index = new_hover;
    if (new_hover >= 0 && (size_t) new_hover < field->options.len) {
        hui_select_option *opt = &field->options.data[new_hover];
        hui_dom_add_class(ctx, opt->option, "hui-select-option-hover");
    }
}

static void hui_select_mark_selected(hui_ctx *ctx, hui_auto_select_field *field, int index) {
    for (size_t i = 0; i < field->options.len; i++) {
        hui_select_option *opt = &field->options.data[i];
        int selected = (index >= 0 && (int) i == index);
        if (selected)
            hui_dom_add_class(ctx, opt->option, "hui-select-option-selected");
        else
            hui_dom_remove_class(ctx, opt->option, "hui-select-option-selected");
        if (!hui_node_is_null(opt->option) && opt->option.index < ctx->dom.nodes.len)
            ctx->dom.nodes.data[opt->option.index].attr_selected = selected;
    }
}

static const char *hui_select_option_value(const hui_ctx *ctx, const hui_select_option *opt, size_t *len_out) {
    if (!ctx || !opt) {
        if (len_out) *len_out = 0;
        return "";
    }
    return hui_intern_str(&ctx->atoms, opt->value_atom, (uint32_t *) len_out);
}

static uint32_t hui_auto_select_apply_selection(hui_ctx *ctx, hui_auto_select_field *field,
                                                int new_index, int from_binding) {
    if (!ctx || !field) return 0;
    if (field->options.len == 0) return 0;
    if (new_index < 0 || (size_t) new_index >= field->options.len) return 0;
    if (field->selected_index == new_index && from_binding) return 0;

    hui_select_mark_selected(ctx, field, new_index);

    const hui_select_option *opt = &field->options.data[new_index];
    const char *label = opt->label ? opt->label : "";
    if (opt->label_len == 0) {
        size_t val_len = 0;
        const char *val = hui_select_option_value(ctx, opt, &val_len);
        label = val;
    }
    if (!hui_node_is_null(field->display_text))
        hui_dom_set_text(ctx, field->display_text, label);

    field->selected_index = new_index;
    if (!from_binding && field->binding_index != 0xFFFFFFFFu && field->binding_index < ctx->bindings.len) {
        size_t val_len = 0;
        const char *value = hui_select_option_value(ctx, opt, &val_len);
        int string_applied = 0;
        if (hui_binding_push_from_string(ctx, field->binding_index, value, &string_applied)) {
            hui_binding_entry *entry = &ctx->bindings.data[field->binding_index];
            hui_binding_apply_text_nodes(ctx, entry);
            hui_binding_apply_inputs_from_binding(ctx, field->binding_index, 0);
        }
    }
    if (!hui_node_is_null(field->node) && field->node.index < ctx->dom.nodes.len) {
        const hui_select_option *sel = &field->options.data[new_index];
        size_t val_len = 0;
        const char *value = hui_select_option_value(ctx, sel, &val_len);
        hui_dom_node *node = &ctx->dom.nodes.data[field->node.index];
        node->attr_value = value;
        node->attr_value_len = (uint32_t) val_len;
    }
    hui_mark_dirty(ctx, field->node, HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
    return HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
}

static int hui_auto_select_fields_rebuild(hui_ctx *ctx) {
    if (!ctx) return HUI_EINVAL;
    size_t old_len = ctx->auto_select_fields.len;
    size_t reuse_index = 0;
    if (ctx->dom.nodes.len == 0) {
        for (size_t i = 0; i < old_len; i++) {
            hui_auto_select_field *field = &ctx->auto_select_fields.data[i];
            for (size_t j = 0; j < field->options.len; j++)
                hui_auto_select_option_free(ctx, &field->options.data[j]);
            hui_vec_free(&field->options);
        }
        ctx->auto_select_fields.len = 0;
        return HUI_OK;
    }

    int status = HUI_OK;
    hui_atom select_atom = hui_intern_put(&ctx->atoms, "select", strlen("select"));
    hui_atom option_atom = hui_intern_put(&ctx->atoms, "option", strlen("option"));
    hui_atom display_class = hui_intern_put(&ctx->atoms, "hui-select-display", strlen("hui-select-display"));
    hui_atom menu_class = hui_intern_put(&ctx->atoms, "hui-select-menu", strlen("hui-select-menu"));

    for (size_t i = 0; i < ctx->dom.nodes.len; i++) {
        hui_dom_node *node = &ctx->dom.nodes.data[i];
        if (node->type != HUI_NODE_ELEM || node->tag != select_atom) continue;

        hui_auto_select_field field;
        memset(&field, 0, sizeof(field));
        field.node = (hui_node_handle){(uint32_t) i, node->gen};
        field.display_node = HUI_NODE_NULL;
        field.display_text = HUI_NODE_NULL;
        field.menu_node = HUI_NODE_NULL;
        field.binding_index = (node->binding_value_index < ctx->bindings.len)
                                  ? node->binding_value_index
                                  : 0xFFFFFFFFu;
        field.selected_index = -1;
        field.open = 0;
        field.hovered_index = -1;
        hui_vec_init(&field.options);

        hui_dom_add_class(ctx, field.node, "hui-select");

        uint32_t child_idx = node->first_child;
        while (child_idx != 0xFFFFFFFFu) {
            hui_dom_node *child = &ctx->dom.nodes.data[child_idx];
            int is_display = 0;
            if (child->type == HUI_NODE_ELEM) {
                for (size_t c = 0; c < child->classes.len; c++) {
                    if (child->classes.data[c] == display_class) {
                        is_display = 1;
                        break;
                    }
                }
            }
            if (is_display) {
                field.display_node = (hui_node_handle){child_idx, child->gen};
                break;
            }
            child_idx = child->next_sibling;
        }

        if (hui_node_is_null(field.display_node)) {
            uint32_t old_first = node->first_child;
            uint32_t elem_idx = hui_dom_add_node(&ctx->dom, HUI_NODE_ELEM);
            hui_dom_node *elem = &ctx->dom.nodes.data[elem_idx];
            elem->tag = hui_intern_put(&ctx->atoms, "div", strlen("div"));
            elem->parent = (uint32_t) i;
            elem->next_sibling = old_first;
            elem->first_child = 0xFFFFFFFFu;
            elem->last_child = 0xFFFFFFFFu;
            hui_vec_push(&elem->classes, display_class);
            node->first_child = elem_idx;
            if (old_first == 0xFFFFFFFFu)
                node->last_child = elem_idx;
            field.display_node = (hui_node_handle){elem_idx, elem->gen};
        }

        hui_dom_node *display_elem = &ctx->dom.nodes.data[field.display_node.index];
        if (display_elem->first_child == 0xFFFFFFFFu) {
            uint32_t text_idx = hui_dom_add_node(&ctx->dom, HUI_NODE_TEXT);
            hui_dom_node *text = &ctx->dom.nodes.data[text_idx];
            text->text = "";
            text->text_len = 0;
            hui_dom_invalidate_text_cache(text);
            hui_dom_link_child_tail(&ctx->dom, field.display_node.index, text_idx);
            field.display_text = (hui_node_handle){text_idx, text->gen};
        } else {
            hui_dom_node *text = &ctx->dom.nodes.data[display_elem->first_child];
            if (text->type != HUI_NODE_TEXT) {
                uint32_t text_idx = hui_dom_add_node(&ctx->dom, HUI_NODE_TEXT);
                hui_dom_node *tn = &ctx->dom.nodes.data[text_idx];
                tn->parent = field.display_node.index;
                tn->next_sibling = display_elem->first_child;
                tn->text = "";
                tn->text_len = 0;
                hui_dom_invalidate_text_cache(tn);
                display_elem->first_child = text_idx;
                if (display_elem->last_child == 0xFFFFFFFFu)
                    display_elem->last_child = text_idx;
                field.display_text = (hui_node_handle){text_idx, tn->gen};
            } else {
                field.display_text = (hui_node_handle){display_elem->first_child, text->gen};
            }
        }
        hui_dom_sync_last_child(&ctx->dom, field.display_node.index);

        uint32_t menu_idx = 0xFFFFFFFFu;
        child_idx = node->first_child;
        while (child_idx != 0xFFFFFFFFu) {
            hui_dom_node *child = &ctx->dom.nodes.data[child_idx];
            int is_menu = 0;
            if (child->type == HUI_NODE_ELEM) {
                for (size_t c = 0; c < child->classes.len; c++) {
                    if (child->classes.data[c] == menu_class) {
                        is_menu = 1;
                        break;
                    }
                }
            }
            if (is_menu) {
                menu_idx = child_idx;
                break;
            }
            child_idx = child->next_sibling;
        }

        if (menu_idx == 0xFFFFFFFFu) {
            uint32_t elem_idx = hui_dom_add_node(&ctx->dom, HUI_NODE_ELEM);
            hui_dom_node *menu_elem = &ctx->dom.nodes.data[elem_idx];
            menu_elem->tag = hui_intern_put(&ctx->atoms, "div", strlen("div"));
            menu_elem->parent = field.node.index;
            menu_elem->first_child = 0xFFFFFFFFu;
            menu_elem->last_child = 0xFFFFFFFFu;
            menu_elem->next_sibling = 0xFFFFFFFFu;
            hui_vec_push(&menu_elem->classes, menu_class);

            hui_dom_node *display_dom = &ctx->dom.nodes.data[field.display_node.index];
            uint32_t after_display = display_dom->next_sibling;
            menu_elem->next_sibling = after_display;
            display_dom->next_sibling = elem_idx;
            if (after_display == 0xFFFFFFFFu)
                node->last_child = elem_idx;
            menu_idx = elem_idx;
        }

        field.menu_node = (hui_node_handle){menu_idx, ctx->dom.nodes.data[menu_idx].gen};
        hui_dom_add_class(ctx, field.menu_node, "hui-select-menu");

        // Reparent option nodes under menu.
        hui_dom_node *menu_elem = &ctx->dom.nodes.data[menu_idx];
        hui_dom_sync_last_child(&ctx->dom, (uint32_t) i);
        hui_dom_sync_last_child(&ctx->dom, menu_idx);
        uint32_t menu_tail = menu_elem->last_child;

        uint32_t prev = 0xFFFFFFFFu;
        uint32_t current = node->first_child;
        while (current != 0xFFFFFFFFu) {
            uint32_t next = ctx->dom.nodes.data[current].next_sibling;
            if (current == field.display_node.index || current == menu_idx) {
                prev = current;
                current = next;
                continue;
            }
            hui_dom_node *child = &ctx->dom.nodes.data[current];
            if (child->type == HUI_NODE_ELEM && child->tag == option_atom) {
                if (prev == 0xFFFFFFFFu)
                    node->first_child = next;
                else
                    ctx->dom.nodes.data[prev].next_sibling = next;
                child->next_sibling = 0xFFFFFFFFu;
                hui_dom_link_child_tail(&ctx->dom, menu_idx, current);
                menu_tail = menu_elem->last_child;
            } else {
                prev = current;
            }
            current = next;
        }
        hui_dom_sync_last_child(&ctx->dom, (uint32_t) i);
        hui_dom_sync_last_child(&ctx->dom, menu_idx);

        uint32_t opt_idx = menu_elem->first_child;
        size_t option_counter = 0;
        while (opt_idx != 0xFFFFFFFFu) {
            hui_dom_node *child = &ctx->dom.nodes.data[opt_idx];
            uint32_t next = child->next_sibling;
            if (child->type == HUI_NODE_ELEM && child->tag == option_atom) {
                hui_dom_add_class(ctx, (hui_node_handle){opt_idx, child->gen}, "hui-select-option");
                hui_select_option opt;
                memset(&opt, 0, sizeof(opt));
                opt.option = (hui_node_handle){opt_idx, child->gen};

                uint32_t text_child = child->first_child;
                while (text_child != 0xFFFFFFFFu &&
                       ctx->dom.nodes.data[text_child].type != HUI_NODE_TEXT) {
                    text_child = ctx->dom.nodes.data[text_child].next_sibling;
                }
                if (text_child == 0xFFFFFFFFu) {
                    text_child = hui_dom_add_node(&ctx->dom, HUI_NODE_TEXT);
                    hui_dom_node *tn = &ctx->dom.nodes.data[text_child];
                    tn->parent = opt_idx;
                    tn->text = "";
                    tn->text_len = 0;
                    hui_dom_invalidate_text_cache(tn);
                    tn->next_sibling = child->first_child;
                    child->first_child = text_child;
                    if (child->last_child == 0xFFFFFFFFu)
                        child->last_child = text_child;
                }
                hui_dom_sync_last_child(&ctx->dom, opt_idx);
                hui_dom_node *text_node = &ctx->dom.nodes.data[text_child];
                const char *src = text_node->text ? text_node->text : "";
                size_t len = text_node->text_len;
                size_t start = 0;
                size_t end = len;
                while (start < end && isspace((unsigned char) src[start])) start++;
                while (end > start && isspace((unsigned char) src[end - 1])) end--;
                size_t trimmed = end - start;
                const char *label_src = src + start;
                size_t label_len = trimmed;
                if (label_len == 0 && child->attr_value && child->attr_value_len > 0) {
                    label_src = child->attr_value;
                    label_len = child->attr_value_len;
                }
                char *label_copy = (char *) ctx->alloc_fn(label_len + 1);
                if (!label_copy) {
                    for (size_t j = 0; j < field.options.len; j++)
                        hui_auto_select_option_free(ctx, &field.options.data[j]);
                    hui_vec_free(&field.options);
                    status = HUI_ENOMEM;
                    goto rebuild_fail_select;
                }
                if (label_len > 0) memcpy(label_copy, label_src, label_len);
                label_copy[label_len] = '\0';
                opt.label = label_copy;
                opt.label_len = label_len;

                if (child->attr_value && child->attr_value_len > 0) {
                    opt.value_atom = hui_intern_put(&ctx->atoms, child->attr_value, child->attr_value_len);
                } else if (label_len > 0) {
                    opt.value_atom = hui_intern_put(&ctx->atoms, label_src, label_len);
                } else {
                    opt.value_atom = hui_intern_put(&ctx->atoms, "", 0);
                }

                if (child->attr_selected)
                    field.selected_index = (int) option_counter;

                hui_vec_push(&field.options, opt);
                option_counter++;
            }
            opt_idx = next;
        }

        if (field.options.len == 0) {
            for (size_t j = 0; j < field.options.len; j++)
                hui_auto_select_option_free(ctx, &field.options.data[j]);
            hui_vec_free(&field.options);
            continue;
        }

        hui_auto_select_field *stored = NULL;
        if (reuse_index < old_len) {
            stored = &ctx->auto_select_fields.data[reuse_index];
            for (size_t j = 0; j < stored->options.len; j++)
                hui_auto_select_option_free(ctx, &stored->options.data[j]);
            hui_vec_free(&stored->options);
            *stored = field;
        } else {
            hui_vec_push(&ctx->auto_select_fields, field);
            stored = &ctx->auto_select_fields.data[ctx->auto_select_fields.len - 1];
        }
        field.options.data = NULL;
        field.options.len = 0;
        field.options.cap = 0;
        reuse_index++;

        int selected_index = stored->selected_index;
        int binding_matched = 0;
        if (stored->binding_index != 0xFFFFFFFFu) {
            hui_binding_entry *entry = &ctx->bindings.data[stored->binding_index];
            const char *binding_value = entry->string_value ? entry->string_value : "";
            size_t binding_len = entry->string_value ? entry->cached_length : 0;
            if (binding_len == 0 && binding_value)
                binding_len = strlen(binding_value);
            int idx = hui_auto_select_find_value_index(ctx, stored, binding_value, binding_len);
            if (idx >= 0) {
                selected_index = idx;
                binding_matched = 1;
            }
        }
        if (selected_index < 0 && node->attr_value && node->attr_value_len > 0) {
            int idx = hui_auto_select_find_value_index(ctx, stored, node->attr_value, node->attr_value_len);
            if (idx >= 0) selected_index = idx;
        }
        if (selected_index < 0) selected_index = 0;
        if (selected_index >= 0)
            hui_auto_select_apply_selection(ctx, stored, selected_index, binding_matched);
        else if (!hui_node_is_null(stored->display_text))
            hui_dom_set_text(ctx, stored->display_text, "");
        hui_select_apply_visibility(ctx, stored);
    }

    for (size_t i = reuse_index; i < old_len; i++) {
        hui_auto_select_field *left = &ctx->auto_select_fields.data[i];
        for (size_t j = 0; j < left->options.len; j++)
            hui_auto_select_option_free(ctx, &left->options.data[j]);
        hui_vec_free(&left->options);
    }
    ctx->auto_select_fields.len = reuse_index;

    return status;

rebuild_fail_select:
    hui_auto_select_fields_reset(ctx);
    return status;
}

static uint32_t hui_auto_select_fields_step(hui_ctx *ctx, float dt) {
    (void) dt;
    if (!ctx) return 0;
    const hui_input_state *state = hui_input_get_state(ctx);
    if (!state) return 0;
    float px = state->pointer_x;
    float py = state->pointer_y;
    uint32_t hit = hui_hit_test(ctx, px, py);
    uint32_t dirty = 0;
    uint32_t pressed = state->pointer_pressed & HUI_POINTER_BUTTON_PRIMARY;

    for (size_t i = 0; i < ctx->auto_select_fields.len; i++) {
        hui_auto_select_field *field = &ctx->auto_select_fields.data[i];
        if (field->options.len == 0) continue;

        int hit_option = -1;
        for (size_t oi = 0; oi < field->options.len; oi++) {
            if (field->options.data[oi].option.index == hit) {
                hit_option = (int) oi;
                break;
            }
        }

        if (field->open) {
            hui_select_update_hover(ctx, field, hit_option);
            if (pressed) {
                if (hit_option >= 0) {
                    dirty |= hui_auto_select_apply_selection(ctx, field, hit_option, 0);
                    field->open = 0;
                    hui_dom_remove_class(ctx, field->node, "hui-select-open");
                    dirty |= hui_select_apply_visibility(ctx, field);
                    hui_select_update_hover(ctx, field, -1);
                    hui_mark_dirty(ctx, field->node, HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
                    dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
                } else if (hit == field->display_node.index || hit == field->node.index) {
                    field->open = 0;
                    hui_dom_remove_class(ctx, field->node, "hui-select-open");
                    dirty |= hui_select_apply_visibility(ctx, field);
                    hui_select_update_hover(ctx, field, -1);
                    hui_mark_dirty(ctx, field->node, HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
                    dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
                } else if (!hui_node_is_null(field->menu_node) && hit == field->menu_node.index) {
                    // click inside menu background, keep open
                } else {
                    field->open = 0;
                    hui_dom_remove_class(ctx, field->node, "hui-select-open");
                    dirty |= hui_select_apply_visibility(ctx, field);
                    hui_select_update_hover(ctx, field, -1);
                    hui_mark_dirty(ctx, field->node, HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
                    dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
                }
            }
        } else {
            if (pressed && (hit == field->node.index || hit == field->display_node.index)) {
                field->open = 1;
                hui_dom_add_class(ctx, field->node, "hui-select-open");
                dirty |= hui_select_apply_visibility(ctx, field);
                hui_input_set_focus(ctx, field->node);
                hui_mark_dirty(ctx, field->node, HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
                dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
            }
        }
    }
    return dirty;
}

static size_t hui_strnlen(const char *text, size_t max_len) {
    if (!text) return 0;
    size_t len = 0;
    while (len < max_len && text[len] != '\0') len++;
    return len;
}

static void hui_binding_store_string_len(hui_binding_entry *entry, const char *value, size_t len) {
    if (!entry || !entry->string_value || entry->string_cap == 0) return;
    size_t copy_len = len;
    if (entry->string_cap > 0 && copy_len >= entry->string_cap) copy_len = entry->string_cap - 1;
    if (copy_len > 0 && value) {
        memcpy(entry->string_value, value, copy_len);
    }
    if (copy_len < entry->string_cap)
        entry->string_value[copy_len] = '\0';
    entry->cached_length = copy_len;
}

static void hui_binding_store_string(hui_binding_entry *entry, const char *value) {
    size_t len = value ? strlen(value) : 0;
    hui_binding_store_string_len(entry, value, len);
}

static hui_auto_text_field *hui_find_auto_field(hui_ctx *ctx, uint32_t dom_index) {
    if (!ctx) return NULL;
    for (size_t i = 0; i < ctx->auto_text_fields.len; i++) {
        hui_auto_text_field *field = &ctx->auto_text_fields.data[i];
        if (field->dom_index == dom_index) return field;
    }
    return NULL;
}

static hui_auto_select_field *hui_find_auto_select_field(hui_ctx *ctx, uint32_t dom_index) {
    if (!ctx) return NULL;
    for (size_t i = 0; i < ctx->auto_select_fields.len; i++) {
        hui_auto_select_field *field = &ctx->auto_select_fields.data[i];
        if (!hui_node_is_null(field->node) && field->node.index == dom_index)
            return field;
    }
    return NULL;
}

static int hui_binding_find_entry(const hui_ctx *ctx, hui_atom name) {
    if (!ctx || name == 0) return -1;
    for (size_t i = 0; i < ctx->bindings.len; i++) {
        if (ctx->bindings.data[i].name == name) return (int) i;
    }
    return -1;
}

static int hui_binding_pull_pointer(hui_binding_entry *entry) {
    if (!entry) return 0;
    int changed = 0;
    switch (entry->type) {
        case HUI_BIND_INT:
            if (entry->target.i32) {
                int32_t value = *entry->target.i32;
                if (value != entry->cached.i32 || entry->cached_length == 0) {
                    char buffer[64];
                    snprintf(buffer, sizeof(buffer), "%d", value);
                    hui_binding_store_string(entry, buffer);
                    entry->cached.i32 = value;
                    changed = 1;
                }
            }
            break;
        case HUI_BIND_FLOAT:
            if (entry->target.f32) {
                float value = *entry->target.f32;
                if (value != entry->cached.f32 || entry->cached_length == 0) {
                    char buffer[64];
                    snprintf(buffer, sizeof(buffer), "%.6g", value);
                    hui_binding_store_string(entry, buffer);
                    entry->cached.f32 = value;
                    changed = 1;
                }
            }
            break;
        case HUI_BIND_STRING:
            if (entry->target.str.ptr && entry->target.str.capacity > 0) {
                size_t capacity = entry->target.str.capacity;
                size_t len = hui_strnlen(entry->target.str.ptr, capacity > 0 ? capacity - 1 : 0);
                if (!entry->string_value ||
                    entry->cached_length != len ||
                    (len > 0 && memcmp(entry->string_value, entry->target.str.ptr, len) != 0) ||
                    (entry->string_value && entry->string_value[len] != '\0')) {
                    hui_binding_store_string_len(entry, entry->target.str.ptr, len);
                    changed = 1;
                }
            }
            break;
        default:
            break;
    }
    return changed;
}

static uint32_t hui_binding_apply_text_nodes(hui_ctx *ctx, hui_binding_entry *entry) {
    if (!ctx || !entry) return 0;
    uint32_t dirty = 0;
    uint32_t length = entry->string_value ? (uint32_t) entry->cached_length : 0;
    for (size_t i = 0; i < entry->text_nodes.len; i++) {
        uint32_t idx = entry->text_nodes.data[i];
        if (idx >= ctx->dom.nodes.len) continue;
        hui_dom_node *node = &ctx->dom.nodes.data[idx];
        if (node->binding_template_index != 0xFFFFFFFFu) {
            dirty |= hui_bound_text_apply_index(ctx, node->binding_template_index);
        } else {
            if (entry->string_value) {
                node->text = entry->string_value;
                node->text_len = length;
            } else {
                node->text = "";
                node->text_len = 0;
            }
            hui_dom_invalidate_text_cache(node);
            dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
        }
    }
    return dirty;
}

static uint32_t hui_binding_apply_inputs_from_binding(hui_ctx *ctx, size_t entry_index, int force) {
    if (!ctx || entry_index >= ctx->bindings.len) return 0;
    hui_binding_entry *entry = &ctx->bindings.data[entry_index];
    if (!entry->string_value) return 0;
    uint32_t dirty = 0;
    for (size_t i = 0; i < entry->input_nodes.len; i++) {
        hui_auto_text_field *field = hui_find_auto_field(ctx, entry->input_nodes.data[i]);
        if (field) {
            field->binding_index = (uint32_t) entry_index;
            if (!force && field->field.focused) continue;
            dirty |= hui_text_field_set_text(ctx, &field->field, entry->string_value);
            continue;
        }
        hui_auto_select_field *select_field = hui_find_auto_select_field(ctx, entry->input_nodes.data[i]);
        if (!select_field) continue;
        select_field->binding_index = (uint32_t) entry_index;
        size_t binding_len = entry->cached_length;
        if (binding_len == 0 && entry->string_value)
            binding_len = strlen(entry->string_value);
        int idx = hui_auto_select_find_value_index(ctx, select_field, entry->string_value, binding_len);
        if (idx >= 0)
            dirty |= hui_auto_select_apply_selection(ctx, select_field, idx, 1);
        else {
            hui_select_mark_selected(ctx, select_field, -1);
            select_field->selected_index = -1;
            if (!hui_node_is_null(select_field->display_text))
                hui_dom_set_text(ctx, select_field->display_text, entry->string_value ? entry->string_value : "");
            if (!hui_node_is_null(select_field->node) && select_field->node.index < ctx->dom.nodes.len) {
                hui_dom_node *sel_node = &ctx->dom.nodes.data[select_field->node.index];
                sel_node->attr_value = entry->string_value ? entry->string_value : "";
                sel_node->attr_value_len = (uint32_t) binding_len;
            }
            hui_mark_dirty(ctx, select_field->node, HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
            dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
        }
        dirty |= hui_select_apply_visibility(ctx, select_field);
    }
    return dirty;
}

static hui_atom hui_binding_atom_from_mustache(hui_ctx *ctx, const char *text, size_t len) {
    if (!ctx || !text || len < 4) return 0;
    size_t start = 0;
    size_t end = len;
    while (start < end && isspace((unsigned char) text[start])) start++;
    while (end > start && isspace((unsigned char) text[end - 1])) end--;
    if (end - start < 4) return 0;
    if (!(text[start] == '{' && text[start + 1] == '{' &&
          text[end - 2] == '}' && text[end - 1] == '}'))
        return 0;
    start += 2;
    end -= 2;
    while (start < end && isspace((unsigned char) text[start])) start++;
    while (end > start && isspace((unsigned char) text[end - 1])) end--;
    if (end <= start) return 0;
    for (size_t i = start; i < end; i++) {
        char c = text[i];
        if (!(isalnum((unsigned char) c) || c == '_' || c == '-'))
            return 0;
    }
    return hui_intern_put(&ctx->atoms, text + start, end - start);
}

static hui_atom hui_binding_atom_from_attr(hui_ctx *ctx, const char *text, size_t len) {
    if (!ctx || !text || len == 0) return 0;
    size_t start = 0;
    size_t end = len;
    while (start < end && isspace((unsigned char) text[start])) start++;
    while (end > start && isspace((unsigned char) text[end - 1])) end--;
    if (end <= start) return 0;
    hui_atom moustache = hui_binding_atom_from_mustache(ctx, text + start, end - start);
    if (moustache) return moustache;
    for (size_t i = start; i < end; i++) {
        char c = text[i];
        if (!(isalnum((unsigned char) c) || c == '_' || c == '-'))
            return 0;
    }
    return hui_intern_put(&ctx->atoms, text + start, end - start);
}

static void hui_binding_relink_entry(hui_ctx *ctx, size_t entry_index) {
    if (!ctx || entry_index >= ctx->bindings.len) return;
    hui_binding_entry *entry = &ctx->bindings.data[entry_index];
    entry->text_nodes.len = 0;
    entry->input_nodes.len = 0;
    for (size_t i = 0; i < ctx->dom.nodes.len; i++) {
        hui_dom_node *node = &ctx->dom.nodes.data[i];
        if (node->binding_text_atom == entry->name) {
            node->binding_text_index = (uint32_t) entry_index;
            hui_binding_push_node_unique(&entry->text_nodes, (uint32_t) i);
        }
        if (node->binding_value_atom == entry->name) {
            node->binding_value_index = (uint32_t) entry_index;
            hui_vec_push(&entry->input_nodes, (uint32_t) i);
        }
    }
    for (size_t i = 0; i < ctx->bound_texts.len; i++) {
        const hui_bound_text_template *templ = &ctx->bound_texts.data[i];
        for (size_t t = 0; t < templ->tokens.len; t++) {
            if (templ->tokens.data[t].atom == entry->name) {
                hui_binding_push_node_unique(&entry->text_nodes, templ->node_index);
                break;
            }
        }
    }
}

static void hui_binding_prepare_dom(hui_ctx *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->bindings.len; i++) {
        ctx->bindings.data[i].text_nodes.len = 0;
        ctx->bindings.data[i].input_nodes.len = 0;
    }
    hui_bound_texts_reset(ctx);
    for (size_t i = 0; i < ctx->dom.nodes.len; i++) {
        hui_dom_node *node = &ctx->dom.nodes.data[i];
        node->binding_text_index = 0xFFFFFFFFu;
        node->binding_value_index = 0xFFFFFFFFu;
        node->binding_text_atom = 0;
        node->binding_value_atom = 0;
        node->binding_template_index = 0xFFFFFFFFu;
        node->tf_flags = HUI_NODE_TF_NONE;
        node->tf_caret = 0;
        node->tf_sel_start = 0;
        node->tf_sel_end = 0;
        if (node->type == HUI_NODE_TEXT) {
            hui_bound_text_prepare_node(ctx, (uint32_t) i, node);
        } else if (node->type == HUI_NODE_ELEM) {
            if (node->attr_value && node->attr_value_len > 0) {
                hui_atom atom = hui_binding_atom_from_attr(ctx, node->attr_value, node->attr_value_len);
                if (atom) node->binding_value_atom = atom;
            }
            if (node->binding_value_atom == 0) {
                int text_entry = hui_node_text_entry_info(ctx, node, NULL);
                if (text_entry) {
                    uint32_t child_idx = node->first_child;
                    while (child_idx != 0xFFFFFFFFu) {
                        hui_dom_node *child = &ctx->dom.nodes.data[child_idx];
                        if (child->type == HUI_NODE_TEXT && child->binding_text_atom != 0) {
                            node->binding_value_atom = child->binding_text_atom;
                            child->binding_text_atom = 0;
                            child->binding_text_index = 0xFFFFFFFFu;
                            child->binding_template_index = 0xFFFFFFFFu;
                            break;
                        }
                        child_idx = child->next_sibling;
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < ctx->bindings.len; i++) {
        hui_binding_relink_entry(ctx, i);
    }
}

static int hui_binding_push_from_string(hui_ctx *ctx, size_t entry_index, const char *text, int *string_applied) {
    (void) ctx;
    if (string_applied) *string_applied = 0;
    if (!ctx || entry_index >= ctx->bindings.len) return 0;
    hui_binding_entry *entry = &ctx->bindings.data[entry_index];
    const char *source = text ? text : "";
    switch (entry->type) {
        case HUI_BIND_INT: {
            if (!entry->target.i32) return 0;
            char *end = NULL;
            long value = strtol(source, &end, 10);
            while (end && *end && isspace((unsigned char) *end)) end++;
            if (source == end || (end && *end != '\0')) return 0;
            if (value < INT32_MIN || value > INT32_MAX) return 0;
            int32_t new_val = (int32_t) value;
            int pointer_changed = (*entry->target.i32 != new_val);
            *entry->target.i32 = new_val;
            entry->cached.i32 = new_val;
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%d", new_val);
            int string_changed = (!entry->string_value || strcmp(entry->string_value, buffer) != 0);
            hui_binding_store_string(entry, buffer);
            if (string_applied) *string_applied = string_changed;
            return pointer_changed || string_changed;
        }
        case HUI_BIND_FLOAT: {
            if (!entry->target.f32) return 0;
            char *end = NULL;
            float value = strtof(source, &end);
            while (end && *end && isspace((unsigned char) *end)) end++;
            if (source == end || (end && *end != '\0')) return 0;
            float new_val = value;
            int pointer_changed = (*entry->target.f32 != new_val);
            *entry->target.f32 = new_val;
            entry->cached.f32 = new_val;
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%.6g", new_val);
            int string_changed = (!entry->string_value || strcmp(entry->string_value, buffer) != 0);
            hui_binding_store_string(entry, buffer);
            if (string_applied) *string_applied = string_changed;
            return pointer_changed || string_changed;
        }
        case HUI_BIND_STRING: {
            if (!entry->target.str.ptr || entry->target.str.capacity == 0) return 0;
            size_t capacity = entry->target.str.capacity;
            size_t len = strlen(source);
            if (len >= capacity) len = capacity - 1;
            int pointer_changed = strncmp(entry->target.str.ptr, source, capacity) != 0;
            memcpy(entry->target.str.ptr, source, len);
            entry->target.str.ptr[len] = '\0';
            int string_changed = (!entry->string_value || strcmp(entry->string_value, entry->target.str.ptr) != 0);
            hui_binding_store_string_len(entry, entry->target.str.ptr, len);
            if (string_applied) *string_applied = string_changed;
            return pointer_changed || string_changed;
        }
        default:
            break;
    }
    return 0;
}

static uint32_t hui_binding_sync_ctx(hui_ctx *ctx) {
    if (!ctx) return 0;
    uint32_t dirty = 0;
    for (size_t i = 0; i < ctx->bindings.len; i++) {
        hui_binding_entry *entry = &ctx->bindings.data[i];
        if (hui_binding_pull_pointer(entry)) {
            dirty |= hui_binding_apply_text_nodes(ctx, entry);
            dirty |= hui_binding_apply_inputs_from_binding(ctx, i, 1);
        }
    }
    for (size_t i = 0; i < ctx->auto_text_fields.len; i++) {
        hui_auto_text_field *field = &ctx->auto_text_fields.data[i];
        if (field->binding_index == 0xFFFFFFFFu || field->binding_index >= ctx->bindings.len)
            continue;
        hui_binding_entry *entry = &ctx->bindings.data[field->binding_index];
        if (!entry->string_value) continue;
        if (strcmp(field->buffer, entry->string_value) != 0) {
            int string_applied = 0;
            if (hui_binding_push_from_string(ctx, field->binding_index, field->buffer, &string_applied)) {
                dirty |= hui_binding_apply_text_nodes(ctx, entry);
                if (string_applied) {
                    dirty |= hui_text_field_set_text(ctx, &field->field, entry->string_value);
                }
                dirty |= hui_binding_apply_inputs_from_binding(ctx, field->binding_index, 0);
            }
        }
    }
    for (size_t i = 0; i < ctx->auto_select_fields.len; i++) {
        hui_auto_select_field *field = &ctx->auto_select_fields.data[i];
        if (field->binding_index == 0xFFFFFFFFu || field->binding_index >= ctx->bindings.len)
            continue;
        hui_binding_entry *entry = &ctx->bindings.data[field->binding_index];
        if (!entry->string_value) continue;
        const char *binding_value = entry->string_value;
        size_t binding_len = entry->cached_length;
        if (binding_len == 0 && binding_value)
            binding_len = strlen(binding_value);
        int idx = hui_auto_select_find_value_index(ctx, field, binding_value, binding_len);
        if (idx >= 0 && idx != field->selected_index) {
            dirty |= hui_auto_select_apply_selection(ctx, field, idx, 1);
        } else if (idx < 0) {
            hui_select_mark_selected(ctx, field, -1);
            field->selected_index = -1;
            if (!hui_node_is_null(field->display_text))
                hui_dom_set_text(ctx, field->display_text, binding_value);
            if (!hui_node_is_null(field->node) && field->node.index < ctx->dom.nodes.len) {
                hui_dom_node *sel_node = &ctx->dom.nodes.data[field->node.index];
                sel_node->attr_value = binding_value;
                sel_node->attr_value_len = (uint32_t) binding_len;
            }
            hui_mark_dirty(ctx, field->node, HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
            dirty |= HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
        }
        dirty |= hui_select_apply_visibility(ctx, field);
    }
    return dirty;
}

static uint32_t hui_auto_text_fields_step(hui_ctx *ctx, float dt) {
    if (!ctx) return 0;
    uint32_t dirty = 0;
    const hui_input_state *state = hui_input_get_state(ctx);
    int pointer_event = 0;
    if (state) {
        pointer_event = (state->pointer_pressed | state->pointer_released) != 0;
    }
    for (size_t i = 0; i < ctx->auto_text_fields.len; i++) {
        hui_text_field *field = &ctx->auto_text_fields.data[i].field;
        int active = pointer_event ||
                     field->focused || field->selecting || field->nav_active_key != 0;
        if (!active) continue;
        dirty |= hui_text_field_step(ctx, field, dt);
    }
    return dirty;
}

void hui_destroy(hui_ctx *ctx) {
    if (!ctx) return;
    free(ctx->html);
    free(ctx->css);
    hui_intern_reset(&ctx->atoms);
    hui_dom_reset(&ctx->dom);
    hui_css_reset(&ctx->stylesheet);
    hui_style_store_release(&ctx->styles);
    hui_draw_list_release(&ctx->draw);
    hui_bound_texts_reset(ctx);
    hui_vec_free(&ctx->bound_texts);
    hui_vec_free(&ctx->input_events);
    hui_vec_free(&ctx->text_input);
    hui_vec_free(&ctx->key_pressed);
    hui_vec_free(&ctx->key_released);
    hui_vec_free(&ctx->key_held);
    hui_auto_text_fields_reset(ctx);
    hui_auto_select_fields_reset(ctx);
    hui_vec_free(&ctx->auto_text_fields);
    hui_vec_free(&ctx->auto_select_fields);
    hui_fonts_reset(ctx);
    hui_vec_free(&ctx->fonts);
    for (size_t i = 0; i < ctx->bindings.len; i++) {
        hui_binding_entry *entry = &ctx->bindings.data[i];
        hui_vec_free(&entry->text_nodes);
        hui_vec_free(&entry->input_nodes);
        if (entry->string_value)
            ctx->free_fn(entry->string_value);
    }
    hui_vec_free(&ctx->bindings);
    ctx->free_fn(ctx);
}

void hui_set_dom_filter(hui_ctx *ctx, hui_dom_filter_fn fn, void *user) {
    ctx->filter_fn = fn;
    ctx->filter_user = user;
}

void hui_set_filter_spec(hui_ctx *ctx, const hui_filter_spec *spec) {
    if (spec) {
        ctx->filter_spec = *spec;
        if (ctx->filter_spec.max_depth < 0) ctx->filter_spec.max_depth = -1;
        ctx->filter_spec_enabled = 1;
    } else {
        memset(&ctx->filter_spec, 0, sizeof(ctx->filter_spec));
        ctx->filter_spec.max_depth = -1;
        ctx->filter_spec_enabled = 0;
    }
}

void hui_set_style_property_mask(hui_ctx *ctx, uint32_t mask) {
    ctx->prop_mask = mask;
}

static int hui_append_buffer(char **buf, size_t *len, size_t *cap, const uint8_t *data, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t new_cap = (*cap ? *cap * 2 : 4096);
        while (new_cap < *len + n + 1) new_cap *= 2;
        char *new_buf = (char *) realloc(*buf, new_cap);
        if (!new_buf) return HUI_ENOMEM;
        *buf = new_buf;
        *cap = new_cap;
    }
    memcpy(*buf + *len, data, n);
    *len += n;
    (*buf)[*len] = '\0';
    return HUI_OK;
}

int hui_feed_html(hui_ctx *ctx, hui_bytes chunk, int is_last) {
    if (ctx->html_finished) return HUI_ESTATE;
    int r = hui_append_buffer(&ctx->html, &ctx->html_len, &ctx->html_cap, chunk.ptr, chunk.len);
    if (r != HUI_OK) return r;
    if (is_last) ctx->html_finished = 1;
    return HUI_OK;
}

int hui_feed_css(hui_ctx *ctx, hui_bytes chunk, int is_last) {
    if (ctx->css_finished) return HUI_ESTATE;
    int r = hui_append_buffer(&ctx->css, &ctx->css_len, &ctx->css_cap, chunk.ptr, chunk.len);
    if (r != HUI_OK) return r;
    if (is_last) ctx->css_finished = 1;
    return HUI_OK;
}

int hui_parse(hui_ctx *ctx) {
    if (!ctx->html) {
        hui_set_error(ctx, "no HTML");
        return HUI_EPARSE;
    }
    hui_auto_text_fields_reset(ctx);
    hui_dom_reset(&ctx->dom);
    hui_dom_init(&ctx->dom);
    hui_builder builder;
    memset(&builder, 0, sizeof(builder));
    builder.dom = &ctx->dom;
    builder.atoms = &ctx->atoms;
    builder.filter_fn = ctx->filter_fn;
    builder.filter_user = ctx->filter_user;
    builder.filter_spec = ctx->filter_spec_enabled ? &ctx->filter_spec : NULL;
    int r = hui_build_from_html(&builder, ctx->html, ctx->html_len);
    if (r != HUI_OK) {
        hui_set_error(ctx, "html parse failed");
        return r;
    }
    hui_css_reset(&ctx->stylesheet);
    hui_css_init(&ctx->stylesheet);
    if (ctx->css && ctx->css_len) {
        if (hui_css_parse(&ctx->stylesheet, &ctx->atoms, ctx->css, ctx->css_len) != 0) {
            hui_set_error(ctx, "css parse failed");
            return HUI_EPARSE;
        }
    }
    int font_status = hui_fonts_load_from_stylesheet(ctx);
    if (font_status != HUI_OK) return font_status;
    hui_binding_prepare_dom(ctx);
    for (size_t bi = 0; bi < ctx->bindings.len; bi++) {
        hui_binding_entry *entry = &ctx->bindings.data[bi];
        hui_binding_pull_pointer(entry);
        uint32_t binding_dirty = hui_binding_apply_text_nodes(ctx, entry);
        hui_ctx_accumulate_dirty(ctx, binding_dirty);
    }
    int auto_r = hui_auto_text_fields_rebuild(ctx);
    if (auto_r != HUI_OK) return auto_r;
    auto_r = hui_auto_select_fields_rebuild(ctx);
    if (auto_r != HUI_OK) return auto_r;
    for (size_t bi = 0; bi < ctx->bindings.len; bi++) {
        uint32_t input_dirty = hui_binding_apply_inputs_from_binding(ctx, bi, 1);
        hui_ctx_accumulate_dirty(ctx, input_dirty);
    }
    ctx->render_cache.valid = 0;
    hui_ctx_accumulate_dirty(ctx, HUI_DIRTY_ALL);
    return HUI_OK;
}

int hui_style(hui_ctx *ctx) {
    hui_style_store_reset(&ctx->styles);
    hui_style_store_init(&ctx->styles);
    hui_apply_styles(&ctx->styles, &ctx->dom, &ctx->atoms, &ctx->stylesheet, ctx->prop_mask);
    hui_style_assign_fonts(ctx);
    hui_auto_select_refresh_visibility(ctx);
    return HUI_OK;
}

int hui_layout(hui_ctx *ctx, const hui_build_opts *opts) {
    hui_build_opts normalized = hui_normalize_build_opts(opts);
    hui_render_cache_store(ctx, &normalized);
    hui_layout_opts layout_opts;
    layout_opts.viewport_w = normalized.viewport_w;
    layout_opts.viewport_h = normalized.viewport_h;
    layout_opts.dpi = normalized.dpi;
    hui_layout_run(&ctx->dom, &ctx->styles, &layout_opts);
    return HUI_OK;
}

int hui_paint(hui_ctx *ctx) {
    hui_draw_list_reset(&ctx->draw);
    hui_draw_list_init(&ctx->draw);
    float viewport_w = ctx->render_cache.valid ? ctx->render_cache.viewport_w : 800.0f;
    float viewport_h = ctx->render_cache.valid ? ctx->render_cache.viewport_h : 600.0f;
    hui_paint_build(&ctx->draw, &ctx->dom, &ctx->styles, viewport_w, viewport_h);
    return HUI_OK;
}

int hui_has_dirty(hui_ctx *ctx) {
    if (!ctx) return 0;
    return (ctx->dirty_flags & HUI_DIRTY_ALL) != 0;
}

int hui_build_ir(hui_ctx *ctx, const hui_build_opts *opts) {
    return hui_render(ctx, opts, NULL);
}

int hui_render(hui_ctx *ctx, const hui_build_opts *opts, hui_render_output *out) {
    if (!ctx) return HUI_EINVAL;
    hui_render_output local_out;
    memset(&local_out, 0, sizeof(local_out));
    uint32_t pending_dirty = ctx->dirty_flags & HUI_DIRTY_ALL;
    hui_build_opts normalized = hui_normalize_build_opts(opts);
    int opts_changed = !hui_render_opts_equal(ctx, &normalized);
    int needs_style = opts_changed || !ctx->draw_valid || ((pending_dirty & HUI_DIRTY_STYLE) != 0);
    int needs_layout = opts_changed || !ctx->draw_valid || ((pending_dirty & HUI_DIRTY_LAYOUT) != 0) || needs_style;
    int needs_paint = opts_changed || !ctx->draw_valid || ((pending_dirty & HUI_DIRTY_PAINT) != 0) || needs_layout;
    int status = HUI_OK;

    if (!needs_style && !needs_layout && !needs_paint) {
        local_out.changed = 0;
        local_out.dirty_flags = pending_dirty;
        local_out.draw.items = ctx->draw.cmds.data;
        local_out.draw.count = ctx->draw.cmds.len;
        if (out) *out = local_out;
        return status;
    }

    if (!needs_style && !needs_layout && !needs_paint) {
        local_out.changed = 0;
        local_out.dirty_flags = pending_dirty;
        local_out.draw.items = ctx->draw.cmds.data;
        local_out.draw.count = ctx->draw.cmds.len;
        if (out) *out = local_out;
        return status;
    }

    if (needs_style) {
        status = hui_style(ctx);
        if (status != HUI_OK) goto render_end;
    }
    if (needs_layout) {
        status = hui_layout(ctx, &normalized);
        if (status != HUI_OK) goto render_end;
    }
    if (needs_paint) {
        status = hui_paint(ctx);
        if (status != HUI_OK) goto render_end;
        ctx->draw_valid = 1;
        ctx->draw_version++;
        hui_render_cache_store(ctx, &normalized);
        ctx->dirty_flags &= ~pending_dirty;
        local_out.changed = 1;
    } else {
        local_out.changed = 0;
    }

render_end:
    local_out.dirty_flags = pending_dirty;
    local_out.draw.items = ctx->draw.cmds.data;
    local_out.draw.count = ctx->draw.cmds.len;
    if (out) *out = local_out;
    return status;
}

static uint32_t hui_hit_test_siblings(const hui_ctx *ctx, uint32_t idx, float x, float y) {
    if (!ctx) return 0xFFFFFFFFu;
    if (idx == 0xFFFFFFFFu) return 0xFFFFFFFFu;
    if (idx >= ctx->dom.nodes.len) return 0xFFFFFFFFu;
    const hui_dom_node *node = &ctx->dom.nodes.data[idx];
    uint32_t hit = hui_hit_test_siblings(ctx, node->next_sibling, x, y);
    if (hit != 0xFFFFFFFFu) return hit;
    return hui_hit_test_subtree(ctx, idx, x, y);
}

static uint32_t hui_hit_test_subtree(const hui_ctx *ctx, uint32_t idx, float x, float y) {
    if (!ctx) return 0xFFFFFFFFu;
    if (idx >= ctx->dom.nodes.len) return 0xFFFFFFFFu;
    const hui_dom_node *node = &ctx->dom.nodes.data[idx];
    const hui_computed_style *cs = (idx < ctx->styles.styles.len)
                                       ? &ctx->styles.styles.data[idx]
                                       : NULL;
    if (node->type == HUI_NODE_ELEM) {
        if (cs && cs->display == 0) return 0xFFFFFFFFu;
        uint32_t child = node->first_child;
        if (child != 0xFFFFFFFFu) {
            uint32_t hit = hui_hit_test_siblings(ctx, child, x, y);
            if (hit != 0xFFFFFFFFu) return hit;
        }
        float left = node->x;
        float top = node->y;
        float right = left + node->w;
        float bottom = top + node->h;
        if (node->w <= 0.0f && node->h <= 0.0f) return 0xFFFFFFFFu;
        if (x >= left && x <= right && y >= top && y <= bottom)
            return idx;
    }
    return 0xFFFFFFFFu;
}

static uint32_t hui_hit_test(const hui_ctx *ctx, float x, float y) {
    if (!ctx) return 0xFFFFFFFFu;
    if (ctx->dom.nodes.len == 0) return 0xFFFFFFFFu;
    if (ctx->dom.root != 0xFFFFFFFFu)
        return hui_hit_test_siblings(ctx, ctx->dom.root, x, y);
    for (size_t idx = ctx->dom.nodes.len; idx-- > 0;) {
        const hui_dom_node *node = &ctx->dom.nodes.data[idx];
        if (node->parent != 0xFFFFFFFFu) continue;
        uint32_t hit = hui_hit_test_subtree(ctx, (uint32_t) idx, x, y);
        if (hit != 0xFFFFFFFFu) return hit;
    }
    return 0xFFFFFFFFu;
}

static uint32_t hui_update_hover_target(hui_ctx *ctx, uint32_t hit) {
    uint32_t dirty = 0;
    if (hit == ctx->hovered_node) return 0;
    if (ctx->hovered_node != 0xFFFFFFFFu && ctx->hovered_node < ctx->dom.nodes.len) {
        ctx->dom.nodes.data[ctx->hovered_node].flags &= ~HUI_NODE_FLAG_HOVER;
        dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
    }
    ctx->hovered_node = 0xFFFFFFFFu;
    if (hit != 0xFFFFFFFFu && hit < ctx->dom.nodes.len) {
        ctx->dom.nodes.data[hit].flags |= HUI_NODE_FLAG_HOVER;
        ctx->hovered_node = hit;
        dirty |= HUI_DIRTY_STYLE | HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT;
    }
    return dirty;
}

static uint32_t hui_apply_pointer_position(hui_ctx *ctx, float x, float y) {
    ctx->pointer_active = 1;
    ctx->pointer_x = x;
    ctx->pointer_y = y;
    uint32_t hit = hui_hit_test(ctx, x, y);
    return hui_update_hover_target(ctx, hit);
}

static void hui_remove_key_held(hui_ctx *ctx, uint32_t keycode) {
    for (size_t i = 0; i < ctx->key_held.len; i++) {
        if (ctx->key_held.data[i] == keycode) {
            if (i + 1 < ctx->key_held.len) {
                memmove(&ctx->key_held.data[i], &ctx->key_held.data[i + 1],
                        (ctx->key_held.len - i - 1) * sizeof(uint32_t));
            }
            ctx->key_held.len--;
            return;
        }
    }
}

static int hui_key_is_held(const hui_ctx *ctx, uint32_t keycode) {
    for (size_t i = 0; i < ctx->key_held.len; i++) {
        if (ctx->key_held.data[i] == keycode) return 1;
    }
    return 0;
}

int hui_push_input(hui_ctx *ctx, const hui_input_event *event) {
    if (!ctx || !event) return HUI_EINVAL;
    hui_vec_push(&ctx->input_events, *event);
    return HUI_OK;
}

uint32_t hui_process_input(hui_ctx *ctx) {
    if (!ctx) return 0u;
    uint32_t dirty = 0u;
    ctx->pointer_pressed = 0;
    ctx->pointer_released = 0;
    ctx->text_input.len = 0;
    ctx->key_pressed.len = 0;
    ctx->key_released.len = 0;
    for (size_t i = 0; i < ctx->input_events.len; i++) {
        const hui_input_event *ev = &ctx->input_events.data[i];
        switch (ev->type) {
            case HUI_INPUT_EVENT_POINTER_MOVE:
                dirty |= hui_apply_pointer_position(ctx, ev->data.pointer_move.x, ev->data.pointer_move.y);
                break;
            case HUI_INPUT_EVENT_POINTER_BUTTON:
                ctx->pointer_buttons_prev = ctx->pointer_buttons;
                ctx->pointer_buttons = ev->data.pointer_button.buttons;
                ctx->pointer_pressed |= ctx->pointer_buttons & ~ctx->pointer_buttons_prev;
                ctx->pointer_released |= ctx->pointer_buttons_prev & ~ctx->pointer_buttons;
                dirty |= hui_apply_pointer_position(ctx, ev->data.pointer_button.x, ev->data.pointer_button.y);
                break;
            case HUI_INPUT_EVENT_POINTER_LEAVE:
                if (ctx->pointer_active || ctx->hovered_node != 0xFFFFFFFFu) {
                    ctx->pointer_active = 0;
                    ctx->pointer_buttons = 0;
                    ctx->pointer_pressed = 0;
                    ctx->pointer_released = 0;
                    dirty |= hui_update_hover_target(ctx, 0xFFFFFFFFu);
                }
                break;
            case HUI_INPUT_EVENT_KEY_DOWN:
                ctx->key_modifiers = ev->data.key.modifiers;
                if (!hui_key_is_held(ctx, ev->data.key.keycode)) {
                    hui_vec_push(&ctx->key_held, ev->data.key.keycode);
                }
                hui_vec_push(&ctx->key_pressed, ev->data.key.keycode);
                break;
            case HUI_INPUT_EVENT_KEY_UP:
                ctx->key_modifiers = ev->data.key.modifiers;
                hui_remove_key_held(ctx, ev->data.key.keycode);
                hui_vec_push(&ctx->key_released, ev->data.key.keycode);
                break;
            case HUI_INPUT_EVENT_TEXT_INPUT:
                hui_vec_push(&ctx->text_input, ev->data.text.codepoint);
                break;
            case HUI_INPUT_EVENT_NONE:
            default:
                break;
        }
    }
    ctx->input_events.len = 0;
    ctx->input_state.pointer_x = ctx->pointer_x;
    ctx->input_state.pointer_y = ctx->pointer_y;
    ctx->input_state.pointer_buttons = ctx->pointer_buttons;
    ctx->input_state.pointer_pressed = ctx->pointer_pressed;
    ctx->input_state.pointer_released = ctx->pointer_released;
    ctx->input_state.pointer_inside = ctx->pointer_active;
    ctx->input_state.key_modifiers = ctx->key_modifiers;
    if (ctx->hovered_node != 0xFFFFFFFFu && ctx->hovered_node < ctx->dom.nodes.len) {
        hui_dom_node *hover_node = &ctx->dom.nodes.data[ctx->hovered_node];
        ctx->input_state.hovered = (hui_node_handle){ctx->hovered_node, hover_node->gen};
    } else {
        ctx->input_state.hovered = HUI_NODE_NULL;
    }
    if (!hui_node_is_null(ctx->focus_node) && ctx->focus_node.index < ctx->dom.nodes.len) {
        hui_dom_node *focus_node = &ctx->dom.nodes.data[ctx->focus_node.index];
        if (focus_node->gen == ctx->focus_node.gen) {
            ctx->input_state.focus = ctx->focus_node;
        } else {
            ctx->focus_node = HUI_NODE_NULL;
            ctx->input_state.focus = HUI_NODE_NULL;
        }
    } else {
        ctx->focus_node = HUI_NODE_NULL;
        ctx->input_state.focus = HUI_NODE_NULL;
    }
    ctx->input_state.text_input.data = ctx->text_input.data;
    ctx->input_state.text_input.count = ctx->text_input.len;
    ctx->input_state.keys_pressed.data = ctx->key_pressed.data;
    ctx->input_state.keys_pressed.count = ctx->key_pressed.len;
    ctx->input_state.keys_released.data = ctx->key_released.data;
    ctx->input_state.keys_released.count = ctx->key_released.len;
    hui_ctx_accumulate_dirty(ctx, dirty);
    return dirty;
}

uint32_t hui_step(hui_ctx *ctx, float dt) {
    if (!ctx) return 0u;
    uint32_t dirty = hui_process_input(ctx);
    dirty |= hui_auto_text_fields_step(ctx, dt);
    dirty |= hui_auto_select_fields_step(ctx, dt);
    dirty |= hui_binding_sync_ctx(ctx);
    hui_ctx_accumulate_dirty(ctx, dirty);
    return dirty;
}

int hui_bind_variable(hui_ctx *ctx, const char *name, const hui_binding *binding) {
    if (!ctx || !name || !binding || !binding->ptr) return HUI_EINVAL;
    size_t name_len = strlen(name);
    if (name_len == 0) return HUI_EINVAL;
    hui_atom atom = hui_intern_put(&ctx->atoms, name, name_len);
    int idx = hui_binding_find_entry(ctx, atom);
    hui_binding_entry *entry = NULL;
    if (idx >= 0) {
        entry = &ctx->bindings.data[idx];
    } else {
        hui_binding_entry fresh;
        memset(&fresh, 0, sizeof(fresh));
        fresh.name = atom;
        hui_vec_init(&fresh.text_nodes);
        hui_vec_init(&fresh.input_nodes);
        hui_vec_push(&ctx->bindings, fresh);
        idx = (int) (ctx->bindings.len - 1);
        entry = &ctx->bindings.data[idx];
    }

    if (entry->type != binding->type && entry->string_value) {
        ctx->free_fn(entry->string_value);
        entry->string_value = NULL;
        entry->string_cap = 0;
    }

    entry->type = binding->type;
    switch (binding->type) {
        case HUI_BIND_INT: {
            entry->target.i32 = (int32_t *) binding->ptr;
            entry->cached.i32 = entry->target.i32 ? *entry->target.i32 : 0;
            if (entry->string_cap < 64 || entry->string_value == NULL) {
                if (entry->string_value) ctx->free_fn(entry->string_value);
                entry->string_cap = 64;
                entry->string_value = (char *) ctx->alloc_fn(entry->string_cap);
                if (!entry->string_value) return HUI_ENOMEM;
            }
            break;
        }
        case HUI_BIND_FLOAT: {
            entry->target.f32 = (float *) binding->ptr;
            entry->cached.f32 = entry->target.f32 ? *entry->target.f32 : 0.0f;
            if (entry->string_cap < 64 || entry->string_value == NULL) {
                if (entry->string_value) ctx->free_fn(entry->string_value);
                entry->string_cap = 64;
                entry->string_value = (char *) ctx->alloc_fn(entry->string_cap);
                if (!entry->string_value) return HUI_ENOMEM;
            }
            break;
        }
        case HUI_BIND_STRING: {
            entry->target.str.ptr = (char *) binding->ptr;
            entry->target.str.capacity = binding->string_capacity ? binding->string_capacity : 1;
            if (entry->target.str.capacity < 2) entry->target.str.capacity = 2;
            if (entry->string_value == NULL || entry->string_cap < entry->target.str.capacity) {
                if (entry->string_value) ctx->free_fn(entry->string_value);
                entry->string_cap = entry->target.str.capacity;
                entry->string_value = (char *) ctx->alloc_fn(entry->string_cap);
                if (!entry->string_value) return HUI_ENOMEM;
            } else {
                entry->string_cap = entry->target.str.capacity;
            }
            break;
        }
        default:
            return HUI_EINVAL;
    }

    if (entry->string_value) {
        entry->string_value[0] = '\0';
        entry->cached_length = 0;
    }
    hui_binding_pull_pointer(entry);
    if (ctx->dom.nodes.len > 0) {
        hui_binding_relink_entry(ctx, (size_t) idx);
        uint32_t text_dirty = hui_binding_apply_text_nodes(ctx, entry);
        hui_ctx_accumulate_dirty(ctx, text_dirty);
        uint32_t input_dirty = hui_binding_apply_inputs_from_binding(ctx, (size_t) idx, 1);
        hui_ctx_accumulate_dirty(ctx, input_dirty);
    }
    return HUI_OK;
}

int hui_unbind_variable(hui_ctx *ctx, const char *name) {
    if (!ctx || !name) return HUI_EINVAL;
    size_t name_len = strlen(name);
    if (name_len == 0) return HUI_EINVAL;
    hui_atom atom = hui_intern_put(&ctx->atoms, name, name_len);
    int idx = hui_binding_find_entry(ctx, atom);
    if (idx < 0) return HUI_EINVAL;

    hui_binding_entry *entry = &ctx->bindings.data[idx];
    for (size_t i = 0; i < entry->text_nodes.len; i++) {
        uint32_t node_index = entry->text_nodes.data[i];
        if (node_index < ctx->dom.nodes.len) {
            hui_dom_node *node = &ctx->dom.nodes.data[node_index];
            node->binding_text_index = 0xFFFFFFFFu;
        }
    }
    for (size_t i = 0; i < entry->input_nodes.len; i++) {
        uint32_t node_index = entry->input_nodes.data[i];
        if (node_index < ctx->dom.nodes.len) {
            hui_dom_node *node = &ctx->dom.nodes.data[node_index];
            node->binding_value_index = 0xFFFFFFFFu;
        }
    }
    if (entry->string_value) ctx->free_fn(entry->string_value);
    hui_vec_free(&entry->text_nodes);
    hui_vec_free(&entry->input_nodes);

    for (size_t j = (size_t) idx + 1; j < ctx->bindings.len; j++) {
        ctx->bindings.data[j - 1] = ctx->bindings.data[j];
        hui_binding_entry *moved = &ctx->bindings.data[j - 1];
        for (size_t k = 0; k < moved->text_nodes.len; k++) {
            uint32_t node_index = moved->text_nodes.data[k];
            if (node_index < ctx->dom.nodes.len)
                ctx->dom.nodes.data[node_index].binding_text_index = (uint32_t) (j - 1);
        }
        for (size_t k = 0; k < moved->input_nodes.len; k++) {
            uint32_t node_index = moved->input_nodes.data[k];
            if (node_index < ctx->dom.nodes.len)
                ctx->dom.nodes.data[node_index].binding_value_index = (uint32_t) (j - 1);
        }
    }

    ctx->bindings.len--;
    uint32_t templ_dirty = 0;
    for (size_t i = 0; i < ctx->bound_texts.len; i++) {
        templ_dirty |= hui_bound_text_apply_index(ctx, (uint32_t) i);
    }
    hui_ctx_accumulate_dirty(ctx, templ_dirty | HUI_DIRTY_STYLE | HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
    return HUI_OK;
}

void hui_set_text_input_defaults(hui_ctx *ctx, const hui_clipboard_iface *clipboard,
                                 const hui_text_field_keymap *keymap, size_t buffer_capacity) {
    if (!ctx) return;
    if (clipboard) {
        ctx->text_input_defaults.clipboard = *clipboard;
        ctx->text_input_defaults.clipboard_set =
                clipboard->get_text != NULL || clipboard->set_text != NULL;
    } else {
        memset(&ctx->text_input_defaults.clipboard, 0, sizeof(ctx->text_input_defaults.clipboard));
        ctx->text_input_defaults.clipboard_set = 0;
    }
    if (keymap) {
        ctx->text_input_defaults.keymap = *keymap;
        ctx->text_input_defaults.keymap_set = 1;
    } else {
        memset(&ctx->text_input_defaults.keymap, 0, sizeof(ctx->text_input_defaults.keymap));
        ctx->text_input_defaults.keymap_set = 0;
    }
    if (buffer_capacity >= 16)
        ctx->text_input_defaults.buffer_capacity = buffer_capacity;
    if (ctx->dom.nodes.len > 0)
        hui_auto_text_fields_rebuild(ctx);
}

void hui_set_text_input_repeat(hui_ctx *ctx, float initial_delay, float repeat_delay) {
    if (!ctx) return;
    if (initial_delay > 0.0f && repeat_delay > 0.0f) {
        ctx->text_input_defaults.backspace_initial_delay = initial_delay;
        ctx->text_input_defaults.backspace_repeat_delay = repeat_delay;
        ctx->text_input_defaults.delays_set = 1;
    } else {
        ctx->text_input_defaults.backspace_initial_delay = 0.0f;
        ctx->text_input_defaults.backspace_repeat_delay = 0.0f;
        ctx->text_input_defaults.delays_set = 0;
    }
    if (ctx->dom.nodes.len > 0)
        hui_auto_text_fields_rebuild(ctx);
}

hui_ir_view hui_get_ir(hui_ctx *ctx) {
    (void) ctx;
    hui_ir_view view = {NULL, 0};
    return view;
}

int hui_write_ir_file(hui_ctx *ctx, const char *path) {
    return hui_ir_write_draw_only(&ctx->draw, path);
}

int hui_dump_text_ir(hui_ctx *ctx, const char *path) {
    return hui_ir_dump_text_draw_only(&ctx->draw, path);
}

hui_draw_list_view hui_get_draw_list(hui_ctx *ctx) {
    hui_draw_list_view view = {NULL, 0};
    if (!ctx) return view;
    view.items = ctx->draw.cmds.data;
    view.count = ctx->draw.cmds.len;
    return view;
}

const char *hui_draw_text_utf8(hui_ctx *ctx, const hui_draw *cmd, size_t *len) {
    if (!ctx || !cmd || cmd->op != HUI_DRAW_OP_GLYPH_RUN) return NULL;
    if (cmd->u1 >= ctx->dom.nodes.len) return NULL;
    const hui_dom_node *node = &ctx->dom.nodes.data[cmd->u1];
    if (node->type != HUI_NODE_TEXT) return NULL;
    if (len) *len = node->text_len;
    return node->text;
}

const hui_font_resource *hui_draw_font(hui_ctx *ctx, const hui_draw *cmd) {
    if (!ctx || !cmd || cmd->op != HUI_DRAW_OP_GLYPH_RUN) return NULL;
    if (cmd->u2 == HUI_FONT_ID_NONE) return NULL;
    size_t idx = (size_t) cmd->u2;
    if (idx >= ctx->fonts.len) return NULL;
    return &ctx->fonts.data[idx].resource;
}

void hui_set_asset_base(hui_ctx *ctx, const char *path_utf8) {
    if (!ctx) return;
    if (!path_utf8 || !path_utf8[0]) {
        ctx->asset_base[0] = '\0';
        return;
    }
    size_t len = strlen(path_utf8);
    if (len >= sizeof(ctx->asset_base)) len = sizeof(ctx->asset_base) - 1;
    memcpy(ctx->asset_base, path_utf8, len);
    ctx->asset_base[len] = '\0';
}

hui_node_handle hui_dom_root(hui_ctx *ctx) {
    if (ctx->dom.root == 0xFFFFFFFFu || ctx->dom.nodes.len == 0)
        return HUI_NODE_NULL;
    hui_dom_node *node = &ctx->dom.nodes.data[ctx->dom.root];
    return (hui_node_handle){ctx->dom.root, node->gen};
}

static int hui_valid_handle(hui_ctx *ctx, hui_node_handle h) {
    return h.index < ctx->dom.nodes.len && ctx->dom.nodes.data[h.index].gen == h.gen;
}

hui_node_handle hui_dom_query_id(hui_ctx *ctx, const char *id_utf8) {
    if (!id_utf8) return HUI_NODE_NULL;
    for (size_t i = 0; i < ctx->dom.id_keys.len; i++) {
        hui_atom atom = (hui_atom) ctx->dom.id_keys.data[i];
        uint32_t len = 0;
        const char *str = hui_intern_str(&ctx->atoms, atom, &len);
        if (strlen(id_utf8) == len && memcmp(id_utf8, str, len) == 0) {
            uint32_t idx = ctx->dom.id_vals.data[i];
            hui_dom_node *node = &ctx->dom.nodes.data[idx];
            return (hui_node_handle){idx, node->gen};
        }
    }
    return HUI_NODE_NULL;
}

size_t hui_dom_query_class(hui_ctx *ctx, const char *class_utf8, hui_node_handle *out, size_t max_out) {
    if (!class_utf8) return 0;
    size_t count = 0;
    for (size_t i = 0; i < ctx->dom.class_keys.len; i++) {
        hui_atom atom = (hui_atom) ctx->dom.class_keys.data[i];
        uint32_t len = 0;
        const char *str = hui_intern_str(&ctx->atoms, atom, &len);
        if (strlen(class_utf8) == len && memcmp(class_utf8, str, len) == 0) {
            uint32_t offset = ctx->dom.class_offsets.data[i];
            uint32_t items = ctx->dom.class_counts.data[i];
            for (uint32_t j = 0; j < items; j++) {
                uint32_t idx = ctx->dom.class_items.data[offset + j];
                if (count < max_out)
                    out[count] = (hui_node_handle){idx, ctx->dom.nodes.data[idx].gen};
                count++;
            }
            break;
        }
    }
    return count;
}

size_t hui_dom_query_tag(hui_ctx *ctx, const char *tag_utf8, hui_node_handle *out, size_t max_out) {
    size_t count = 0;
    size_t tag_len = strlen(tag_utf8);
    for (size_t i = 0; i < ctx->dom.nodes.len; i++) {
        hui_dom_node *node = &ctx->dom.nodes.data[i];
        if (node->type != HUI_NODE_ELEM) continue;
        uint32_t len = 0;
        const char *str = hui_intern_str(&ctx->atoms, node->tag, &len);
        if (len == tag_len && memcmp(str, tag_utf8, len) == 0) {
            if (count < max_out)
                out[count] = (hui_node_handle){(uint32_t) i, node->gen};
            count++;
        }
    }
    return count;
}

int hui_node_is_null(hui_node_handle h) {
    return h.index == 0xFFFFFFFFu;
}

int hui_node_is_element(hui_ctx *ctx, hui_node_handle h) {
    return hui_valid_handle(ctx, h) && ctx->dom.nodes.data[h.index].type == HUI_NODE_ELEM;
}

int hui_node_is_text(hui_ctx *ctx, hui_node_handle h) {
    return hui_valid_handle(ctx, h) && ctx->dom.nodes.data[h.index].type == HUI_NODE_TEXT;
}

hui_node_handle hui_node_parent(hui_ctx *ctx, hui_node_handle h) {
    if (!hui_valid_handle(ctx, h)) return HUI_NODE_NULL;
    uint32_t parent = ctx->dom.nodes.data[h.index].parent;
    if (parent == 0xFFFFFFFFu) return HUI_NODE_NULL;
    return (hui_node_handle){parent, ctx->dom.nodes.data[parent].gen};
}

hui_node_handle hui_node_first_child(hui_ctx *ctx, hui_node_handle h) {
    if (!hui_valid_handle(ctx, h)) return HUI_NODE_NULL;
    uint32_t child = ctx->dom.nodes.data[h.index].first_child;
    if (child == 0xFFFFFFFFu) return HUI_NODE_NULL;
    return (hui_node_handle){child, ctx->dom.nodes.data[child].gen};
}

hui_node_handle hui_node_next_sibling(hui_ctx *ctx, hui_node_handle h) {
    if (!hui_valid_handle(ctx, h)) return HUI_NODE_NULL;
    uint32_t sib = ctx->dom.nodes.data[h.index].next_sibling;
    if (sib == 0xFFFFFFFFu) return HUI_NODE_NULL;
    return (hui_node_handle){sib, ctx->dom.nodes.data[sib].gen};
}

int hui_dom_set_attr(hui_ctx *ctx, hui_node_handle h, const char *name, const char *value) {
    if (!hui_valid_handle(ctx, h) || !name) return HUI_EINVAL;
    hui_dom_node *node = &ctx->dom.nodes.data[h.index];
    if (node->type != HUI_NODE_ELEM) return HUI_EINVAL;
    size_t name_len = strlen(name);
    if (name_len == 2 && memcmp(name, "id", 2) == 0) {
        node->id = value ? hui_intern_put(&ctx->atoms, value, strlen(value)) : 0;
        hui_mark_dirty(ctx, h, HUI_DIRTY_STYLE | HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
        return HUI_OK;
    }
    if (name_len == 5 && memcmp(name, "class", 5) == 0) {
        node->classes.len = 0;
        if (value) {
            const char *p = value;
            size_t n = strlen(value);
            size_t i = 0;
            while (i < n) {
                while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) i++;
                size_t start = i;
                while (i < n && !(p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) i++;
                size_t clen = i - start;
                if (clen > 0) {
                    hui_atom atom = hui_intern_put(&ctx->atoms, p + start, clen);
                    hui_vec_push(&node->classes, atom);
                }
            }
        }
        hui_mark_dirty(ctx, h, HUI_DIRTY_STYLE | HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
        return HUI_OK;
    }
    return HUI_OK;
}

int hui_dom_add_class(hui_ctx *ctx, hui_node_handle h, const char *class_name) {
    if (!hui_valid_handle(ctx, h) || !class_name) return HUI_EINVAL;
    hui_dom_node *node = &ctx->dom.nodes.data[h.index];
    if (node->type != HUI_NODE_ELEM) return HUI_EINVAL;
    hui_atom atom = hui_intern_put(&ctx->atoms, class_name, strlen(class_name));
    for (size_t i = 0; i < node->classes.len; i++) if (node->classes.data[i] == atom) return HUI_OK;
    hui_vec_push(&node->classes, atom);
    hui_mark_dirty(ctx, h, HUI_DIRTY_STYLE | HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
    return HUI_OK;
}

int hui_dom_remove_class(hui_ctx *ctx, hui_node_handle h, const char *class_name) {
    if (!hui_valid_handle(ctx, h) || !class_name) return HUI_EINVAL;
    hui_dom_node *node = &ctx->dom.nodes.data[h.index];
    if (node->type != HUI_NODE_ELEM) return HUI_EINVAL;
    hui_atom atom = hui_intern_put(&ctx->atoms, class_name, strlen(class_name));
    size_t write = 0;
    for (size_t i = 0; i < node->classes.len; i++) {
        if (node->classes.data[i] != atom)
            node->classes.data[write++] = node->classes.data[i];
    }
    node->classes.len = write;
    hui_mark_dirty(ctx, h, HUI_DIRTY_STYLE | HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
    return HUI_OK;
}

int hui_dom_set_text(hui_ctx *ctx, hui_node_handle h, const char *text_utf8) {
    if (!hui_valid_handle(ctx, h)) return HUI_EINVAL;
    hui_dom_node *node = &ctx->dom.nodes.data[h.index];
    if (node->type != HUI_NODE_TEXT) return HUI_EINVAL;
    node->text = text_utf8;
    node->text_len = (uint32_t) (text_utf8 ? strlen(text_utf8) : 0);
    hui_dom_invalidate_text_cache(node);
    hui_mark_dirty(ctx, h, HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
    return HUI_OK;
}

hui_node_handle hui_dom_create_element(hui_ctx *ctx, const char *tag_utf8) {
    uint32_t idx = hui_dom_add_node(&ctx->dom, HUI_NODE_ELEM);
    hui_dom_node *node = &ctx->dom.nodes.data[idx];
    node->tag = hui_intern_put(&ctx->atoms, tag_utf8, strlen(tag_utf8));
    return (hui_node_handle){idx, node->gen};
}

hui_node_handle hui_dom_create_text(hui_ctx *ctx, const char *text_utf8) {
    uint32_t idx = hui_dom_add_node(&ctx->dom, HUI_NODE_TEXT);
    hui_dom_node *node = &ctx->dom.nodes.data[idx];
    node->text = text_utf8;
    node->text_len = (uint32_t) (text_utf8 ? strlen(text_utf8) : 0);
    hui_dom_invalidate_text_cache(node);
    return (hui_node_handle){idx, node->gen};
}

int hui_dom_append_child(hui_ctx *ctx, hui_node_handle parent, hui_node_handle child) {
    if (!hui_valid_handle(ctx, parent) || !hui_valid_handle(ctx, child)) return HUI_EINVAL;
    hui_dom_node *p = &ctx->dom.nodes.data[parent.index];
    hui_dom_node *c = &ctx->dom.nodes.data[child.index];
    if (c->parent != 0xFFFFFFFFu && c->parent < ctx->dom.nodes.len) {
        hui_dom_node *old = &ctx->dom.nodes.data[c->parent];
        uint32_t prev = 0xFFFFFFFFu;
        uint32_t curr = old->first_child;
        while (curr != 0xFFFFFFFFu) {
            uint32_t next = ctx->dom.nodes.data[curr].next_sibling;
            if (curr == child.index) {
                if (prev == 0xFFFFFFFFu) {
                    old->first_child = next;
                } else {
                    ctx->dom.nodes.data[prev].next_sibling = next;
                }
                if (old->last_child == child.index) {
                    old->last_child = prev;
                }
                break;
            }
            prev = curr;
            curr = next;
        }
    }
    hui_dom_link_child_tail(&ctx->dom, parent.index, child.index);
    hui_mark_dirty(ctx, parent, HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
    return HUI_OK;
}

void hui_dom_set_text_field_state(hui_ctx *ctx, hui_node_handle h, uint32_t flags,
                                  uint32_t caret, uint32_t sel_start, uint32_t sel_end,
                                  float scroll_x, float scroll_y) {
    if (!ctx || hui_node_is_null(h)) return;
    if (h.index >= ctx->dom.nodes.len) return;
    hui_dom_node *node = &ctx->dom.nodes.data[h.index];
    if (node->gen != h.gen) return;
    node->tf_flags = flags;
    node->tf_caret = caret;
    node->tf_sel_start = sel_start;
    node->tf_sel_end = sel_end;
    node->tf_scroll_x = scroll_x;
    node->tf_scroll_y = scroll_y;
}

void hui_mark_dirty(hui_ctx *ctx, hui_node_handle h, uint32_t flags) {
    if (!ctx) return;
    hui_ctx_accumulate_dirty(ctx, flags);
    if (hui_node_is_null(h)) return;
    if (h.index >= ctx->dom.nodes.len) return;
    hui_dom_node *node = &ctx->dom.nodes.data[h.index];
    if (node->gen != h.gen) return;
    (void) node;
}

int hui_restyle_and_relayout(hui_ctx *ctx, const hui_build_opts *opts) {
    return hui_build_ir(ctx, opts);
}

const char *hui_last_error(hui_ctx *ctx) {
    if (!ctx) return NULL;
    return ctx->last_error;
}

const hui_input_state *hui_input_get_state(hui_ctx *ctx) {
    if (!ctx) return NULL;
    return &ctx->input_state;
}

int hui_input_set_focus(hui_ctx *ctx, hui_node_handle node) {
    if (!ctx) return HUI_EINVAL;
    if (!hui_node_is_null(node)) {
        if (node.index >= ctx->dom.nodes.len) return HUI_EINVAL;
        if (ctx->dom.nodes.data[node.index].gen != node.gen) return HUI_EINVAL;
    }
    ctx->focus_node = node;
    ctx->input_state.focus = node;
    return HUI_OK;
}

hui_node_handle hui_input_get_focus(hui_ctx *ctx) {
    if (!ctx) return HUI_NODE_NULL;
    return ctx->input_state.focus;
}

int hui_input_key_down(hui_ctx *ctx, uint32_t keycode) {
    if (!ctx) return 0;
    return hui_key_is_held(ctx, keycode);
}

int hui_node_get_layout(hui_ctx *ctx, hui_node_handle h, hui_rect *out) {
    if (!ctx || !out) return HUI_EINVAL;
    if (hui_node_is_null(h)) return HUI_EINVAL;
    if (h.index >= ctx->dom.nodes.len) return HUI_EINVAL;
    hui_dom_node *node = &ctx->dom.nodes.data[h.index];
    if (node->gen != h.gen) return HUI_EINVAL;
    out->x = node->x;
    out->y = node->y;
    out->w = node->w;
    out->h = node->h;
    return HUI_OK;
}

static const hui_computed_style *hui_node_style(const hui_ctx *ctx, hui_node_handle h) {
    if (!ctx) return NULL;
    if (hui_node_is_null(h)) return NULL;
    if (h.index >= ctx->dom.nodes.len) return NULL;
    const hui_dom_node *node = &ctx->dom.nodes.data[h.index];
    if (node->gen != h.gen) return NULL;
    uint32_t style_index = node->style_id;
    if (style_index >= ctx->styles.styles.len || (style_index == 0 && h.index < ctx->styles.styles.len && h.index != 0)) {
        style_index = h.index;
    }
    if (style_index >= ctx->styles.styles.len) return NULL;
    return &ctx->styles.styles.data[style_index];
}

float hui_node_font_size(hui_ctx *ctx, hui_node_handle h) {
    if (!ctx) return 0.0f;
    const hui_computed_style *cs = hui_node_style(ctx, h);
    if (!cs) return 0.0f;
    float font_size = cs->font_size;
    if (font_size <= 0.0f) font_size = 16.0f;
    return font_size;
}

float hui_node_line_height(hui_ctx *ctx, hui_node_handle h) {
    if (!ctx) return 0.0f;
    const hui_computed_style *cs = hui_node_style(ctx, h);
    if (!cs) return 0.0f;
    float font_size = cs->font_size;
    if (font_size <= 0.0f) font_size = 16.0f;
    float line_height = cs->line_height;
    if (line_height > 0.0f) {
        if (line_height <= 4.0f)
            line_height = line_height * font_size;
        return line_height;
    }
    return font_size * HUI_TEXT_APPROX_LINE_HEIGHT;
}

