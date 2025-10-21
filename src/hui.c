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

typedef struct {
    hui_text_field field;
    char *buffer;
    size_t buffer_capacity;
    char *placeholder;
} hui_auto_text_field;

static void hui_auto_text_fields_reset(hui_ctx *ctx);
static int hui_auto_text_fields_rebuild(hui_ctx *ctx);
static uint32_t hui_auto_text_fields_step(hui_ctx *ctx, float dt);
static int hui_node_is_text_input(hui_ctx *ctx, const hui_dom_node *node);

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
    HUI_VEC(hui_auto_text_field) auto_text_fields;
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
    char last_error[256];
};

static void hui_set_error(hui_ctx *ctx, const char *msg) {
    snprintf(ctx->last_error, sizeof(ctx->last_error), "%s", msg);
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
    hui_vec_init(&ctx->auto_text_fields);
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
        memset(&auto_field->field, 0, sizeof(auto_field->field));
    }
    ctx->auto_text_fields.len = 0;
}

static int hui_node_is_text_input(hui_ctx *ctx, const hui_dom_node *node) {
    if (!ctx || !node) return 0;
    if (node->type != HUI_NODE_ELEM) return 0;
    hui_atom input_atom = hui_intern_put(&ctx->atoms, "input", strlen("input"));
    if (node->tag != input_atom) return 0;
    if (node->attr_type == 0) return 1;
    hui_atom text_atom = hui_intern_put(&ctx->atoms, "text", strlen("text"));
    hui_atom email_atom = hui_intern_put(&ctx->atoms, "email", strlen("email"));
    hui_atom password_atom = hui_intern_put(&ctx->atoms, "password", strlen("password"));
    return node->attr_type == text_atom ||
           node->attr_type == email_atom ||
           node->attr_type == password_atom;
}

static int hui_auto_text_fields_rebuild(hui_ctx *ctx) {
    if (!ctx) return HUI_EINVAL;
    hui_auto_text_fields_reset(ctx);
    if (ctx->dom.nodes.len == 0) return HUI_OK;

    size_t capacity_default = ctx->text_input_defaults.buffer_capacity;
    if (capacity_default < 16) capacity_default = 256;

    for (size_t i = 0; i < ctx->dom.nodes.len; i++) {
        hui_dom_node *node = &ctx->dom.nodes.data[i];
        if (!hui_node_is_text_input(ctx, node)) continue;

        hui_auto_text_field auto_field;
        memset(&auto_field, 0, sizeof(auto_field));
        auto_field.buffer_capacity = capacity_default;
        auto_field.buffer = (char *) ctx->alloc_fn(auto_field.buffer_capacity);
        if (!auto_field.buffer) {
            hui_auto_text_fields_reset(ctx);
            return HUI_ENOMEM;
        }
        auto_field.buffer[0] = '\0';

        if (node->attr_placeholder && node->attr_placeholder_len > 0) {
            auto_field.placeholder = (char *) ctx->alloc_fn(node->attr_placeholder_len + 1);
            if (!auto_field.placeholder) {
                ctx->free_fn(auto_field.buffer);
                hui_auto_text_fields_reset(ctx);
                return HUI_ENOMEM;
            }
            memcpy(auto_field.placeholder, node->attr_placeholder, node->attr_placeholder_len);
            auto_field.placeholder[node->attr_placeholder_len] = '\0';
        }

        hui_text_field_desc desc;
        memset(&desc, 0, sizeof(desc));
        desc.container = (hui_node_handle){(uint32_t) i, node->gen};
        desc.value = desc.container;
        desc.placeholder = auto_field.placeholder;
        desc.buffer = auto_field.buffer;
        desc.buffer_capacity = auto_field.buffer_capacity;
        if (ctx->text_input_defaults.clipboard_set)
            desc.clipboard = &ctx->text_input_defaults.clipboard;
        if (ctx->text_input_defaults.keymap_set)
            desc.keymap = &ctx->text_input_defaults.keymap;
        if (ctx->text_input_defaults.delays_set) {
            desc.backspace_initial_delay = ctx->text_input_defaults.backspace_initial_delay;
            desc.backspace_repeat_delay = ctx->text_input_defaults.backspace_repeat_delay;
        }

        if (hui_text_field_init(ctx, &auto_field.field, &desc) != HUI_OK) {
            ctx->free_fn(auto_field.buffer);
            if (auto_field.placeholder) ctx->free_fn(auto_field.placeholder);
            hui_auto_text_fields_reset(ctx);
            return HUI_EINVAL;
        }

        if (node->attr_value && node->attr_value_len > 0) {
            char *initial_text = (char *) ctx->alloc_fn(node->attr_value_len + 1);
            if (!initial_text) {
                if (auto_field.placeholder) ctx->free_fn(auto_field.placeholder);
                ctx->free_fn(auto_field.buffer);
                hui_auto_text_fields_reset(ctx);
                return HUI_ENOMEM;
            }
            memcpy(initial_text, node->attr_value, node->attr_value_len);
            initial_text[node->attr_value_len] = '\0';
            hui_text_field_set_text(ctx, &auto_field.field, initial_text);
            ctx->free_fn(initial_text);
        }

        hui_vec_push(&ctx->auto_text_fields, auto_field);
    }

    return HUI_OK;
}

static uint32_t hui_auto_text_fields_step(hui_ctx *ctx, float dt) {
    if (!ctx) return 0;
    uint32_t dirty = 0;
    for (size_t i = 0; i < ctx->auto_text_fields.len; i++) {
        dirty |= hui_text_field_step(ctx, &ctx->auto_text_fields.data[i].field, dt);
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
    hui_style_store_reset(&ctx->styles);
    hui_draw_list_reset(&ctx->draw);
    hui_vec_free(&ctx->input_events);
    hui_vec_free(&ctx->text_input);
    hui_vec_free(&ctx->key_pressed);
    hui_vec_free(&ctx->key_released);
    hui_vec_free(&ctx->key_held);
    hui_auto_text_fields_reset(ctx);
    hui_vec_free(&ctx->auto_text_fields);
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
    int auto_r = hui_auto_text_fields_rebuild(ctx);
    if (auto_r != HUI_OK) return auto_r;
    return HUI_OK;
}

int hui_style(hui_ctx *ctx) {
    hui_style_store_reset(&ctx->styles);
    hui_style_store_init(&ctx->styles);
    hui_apply_styles(&ctx->styles, &ctx->dom, &ctx->atoms, &ctx->stylesheet, ctx->prop_mask);
    return HUI_OK;
}

int hui_layout(hui_ctx *ctx, const hui_build_opts *opts) {
    hui_layout_opts layout_opts;
    layout_opts.viewport_w = opts ? opts->viewport_w : 800.0f;
    layout_opts.viewport_h = opts ? opts->viewport_h : 600.0f;
    layout_opts.dpi = opts ? opts->dpi : 96.0f;
    hui_layout_run(&ctx->dom, &ctx->styles, &layout_opts);
    return HUI_OK;
}

int hui_paint(hui_ctx *ctx) {
    hui_draw_list_reset(&ctx->draw);
    hui_draw_list_init(&ctx->draw);
    hui_paint_build(&ctx->draw, &ctx->dom, &ctx->styles);
    return HUI_OK;
}

int hui_build_ir(hui_ctx *ctx, const hui_build_opts *opts) {
    int r = hui_style(ctx);
    if (r != HUI_OK) return r;
    r = hui_layout(ctx, opts);
    if (r != HUI_OK) return r;
    return hui_paint(ctx);
}

static uint32_t hui_hit_test(const hui_ctx *ctx, float x, float y) {
    if (!ctx) return 0xFFFFFFFFu;
    const hui_dom *dom = &ctx->dom;
    const hui_style_store *styles = &ctx->styles;
    if (dom->nodes.len == 0) return 0xFFFFFFFFu;
    if (styles->styles.len < dom->nodes.len) return 0xFFFFFFFFu;
    for (size_t idx = dom->nodes.len; idx-- > 0;) {
        const hui_dom_node *node = &dom->nodes.data[idx];
        if (node->type != HUI_NODE_ELEM) continue;
        const hui_computed_style *cs = &styles->styles.data[idx];
        if (cs->display == 0) continue;
        float left = node->x;
        float top = node->y;
        float right = left + node->w;
        float bottom = top + node->h;
        if (x >= left && x <= right && y >= top && y <= bottom)
            return (uint32_t) idx;
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
    return dirty;
}

uint32_t hui_step(hui_ctx *ctx, float dt) {
    if (!ctx) return 0u;
    uint32_t dirty = hui_process_input(ctx);
    dirty |= hui_auto_text_fields_step(ctx, dt);
    return dirty;
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
    return (hui_node_handle){idx, node->gen};
}

int hui_dom_append_child(hui_ctx *ctx, hui_node_handle parent, hui_node_handle child) {
    if (!hui_valid_handle(ctx, parent) || !hui_valid_handle(ctx, child)) return HUI_EINVAL;
    hui_dom_node *p = &ctx->dom.nodes.data[parent.index];
    hui_dom_node *c = &ctx->dom.nodes.data[child.index];
    c->parent = parent.index;
    if (p->first_child == 0xFFFFFFFFu) {
        p->first_child = child.index;
    } else {
        uint32_t sibling = p->first_child;
        while (ctx->dom.nodes.data[sibling].next_sibling != 0xFFFFFFFFu)
            sibling = ctx->dom.nodes.data[sibling].next_sibling;
        ctx->dom.nodes.data[sibling].next_sibling = child.index;
    }
    hui_mark_dirty(ctx, parent, HUI_DIRTY_LAYOUT | HUI_DIRTY_PAINT);
    return HUI_OK;
}

void hui_mark_dirty(hui_ctx *ctx, hui_node_handle h, uint32_t flags) {
    (void) ctx;
    (void) h;
    (void) flags;
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
