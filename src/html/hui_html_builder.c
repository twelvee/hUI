#include "hui_html_builder.h"
#include "hui_html_lexer.h"
#include "hui_err.h"

#include <string.h>

static int hui_atom_in_class_list(const char *raw, size_t len, hui_intern *atoms, hui_atom atom) {
    if (!raw || len == 0 || atom == 0) return 0;
    uint32_t alen = 0;
    const char *astr = hui_intern_str(atoms, atom, &alen);
    const char *p = raw;
    size_t i = 0;
    while (i < len) {
        while (i < len && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) i++;
        size_t start = i;
        while (i < len && !(p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) i++;
        size_t clen = i - start;
        if (clen == alen && (clen == 0 || memcmp(p + start, astr, clen) == 0)) return 1;
    }
    return 0;
}

void hui_dom_init(hui_dom *dom) {
    hui_arena_init(&dom->arena, 64 * 1024);
    hui_vec_init(&dom->nodes);
    dom->root = 0xFFFFFFFFu;
    hui_vec_init(&dom->id_keys);
    hui_vec_init(&dom->id_vals);
    hui_vec_init(&dom->class_keys);
    hui_vec_init(&dom->class_offsets);
    hui_vec_init(&dom->class_counts);
    hui_vec_init(&dom->class_items);
}

void hui_dom_reset(hui_dom *dom) {
    hui_arena_reset(&dom->arena);
    hui_vec_free(&dom->nodes);
    hui_vec_free(&dom->id_keys);
    hui_vec_free(&dom->id_vals);
    hui_vec_free(&dom->class_keys);
    hui_vec_free(&dom->class_offsets);
    hui_vec_free(&dom->class_counts);
    hui_vec_free(&dom->class_items);
    dom->root = 0xFFFFFFFFu;
}

uint32_t hui_dom_add_node(hui_dom *dom, hui_node_type type) {
    hui_dom_node node;
    memset(&node, 0, sizeof(node));
    node.type = type;
    node.parent = 0xFFFFFFFFu;
    node.first_child = 0xFFFFFFFFu;
    node.next_sibling = 0xFFFFFFFFu;
    node.gen = 1;
    hui_vec_init(&node.classes);
    node.binding_text_atom = 0;
    node.binding_value_atom = 0;
    node.binding_text_index = 0xFFFFFFFFu;
    node.binding_value_index = 0xFFFFFFFFu;
    uint32_t index = (uint32_t) dom->nodes.len;
    hui_vec_push(&dom->nodes, node);
    return index;
}

static void hui_dom_ensure_class_bucket(hui_builder *builder, hui_atom atom) {
    for (size_t i = 0; i < builder->dom->class_keys.len; i++) {
        if ((hui_atom) builder->dom->class_keys.data[i] == atom) return;
    }
    hui_vec_push(&builder->dom->class_keys, (int32_t)atom);
    hui_vec_push(&builder->dom->class_offsets, (uint32_t)builder->dom->class_items.len);
    hui_vec_push(&builder->dom->class_counts, 0u);
}

void hui_dom_add_index_id(hui_builder *builder, hui_atom atom, uint32_t node_index) {
    if (!atom) return;
    hui_vec_push(&builder->dom->id_keys, (int32_t)atom);
    hui_vec_push(&builder->dom->id_vals, node_index);
}

void hui_dom_add_index_class(hui_builder *builder, hui_atom atom, uint32_t node_index) {
    if (!atom) return;
    hui_dom_ensure_class_bucket(builder, atom);
    for (size_t i = 0; i < builder->dom->class_keys.len; i++) {
        if ((hui_atom) builder->dom->class_keys.data[i] == atom) {
            uint32_t offset = builder->dom->class_offsets.data[i];
            (void) offset;
            uint32_t count = builder->dom->class_counts.data[i];
            hui_vec_push(&builder->dom->class_items, node_index);
            builder->dom->class_counts.data[i] = count + 1;
            return;
        }
    }
}

static int hui_str_in_set(const char *str, size_t len, const char **set, size_t count) {
    if (!set || count == 0 || !str) return 0;
    for (size_t i = 0; i < count; i++) {
        const char *val = set[i];
        size_t vlen = strlen(val);
        if (vlen == len && (vlen == 0 || memcmp(str, val, vlen) == 0)) return 1;
    }
    return 0;
}

static hui_filter_decision hui_apply_filter_spec(const hui_filter_spec *spec, const hui_tag_probe *probe) {
    if (!spec) return HUI_FILTER_TAKE;
    if (spec->max_depth >= 0 && probe->depth > spec->max_depth) return HUI_FILTER_PRUNE_SUBTREE;
    int ok = (spec->allow_tags_count == 0 && spec->allow_ids_count == 0 && spec->allow_classes_count == 0);
    if (hui_str_in_set(probe->tag, probe->tag_len, spec->allow_tags, spec->allow_tags_count)) ok = 1;
    if (probe->id && hui_str_in_set(probe->id, probe->id_len, spec->allow_ids, spec->allow_ids_count)) ok = 1;
    if (probe->class_list && spec->allow_classes_count > 0) {
        for (size_t i = 0; i < spec->allow_classes_count; i++) {
            const char *cls = spec->allow_classes[i];
            size_t cls_len = strlen(cls);
            const char *p = probe->class_list;
            size_t n = probe->class_list_len;
            size_t j = 0;
            while (j < n) {
                while (j < n && (p[j] == ' ' || p[j] == '\t' || p[j] == '\n' || p[j] == '\r')) j++;
                size_t start = j;
                while (j < n && !(p[j] == ' ' || p[j] == '\t' || p[j] == '\n' || p[j] == '\r')) j++;
                size_t clen = j - start;
                if (clen == cls_len && (clen == 0 || memcmp(p + start, cls, clen) == 0)) {
                    ok = 1;
                    break;
                }
            }
            if (ok) break;
        }
    }
    return ok ? HUI_FILTER_TAKE : HUI_FILTER_SKIP_DESCEND;
}

static uint32_t hui_find_kept_parent(const uint32_t *stack_data, size_t stack_len) {
    for (size_t i = stack_len; i > 0; i--) {
        uint32_t idx = stack_data[i - 1];
        if (idx != 0xFFFFFFFFu) return idx;
    }
    return 0xFFFFFFFFu;
}

int hui_build_from_html(hui_builder *builder, const char *html, size_t html_len) {
    hui_html_lexer lexer;
    hui_html_lexer_init(&lexer, html, html_len);
    HUI_VEC(uint32_t) stack;
    hui_vec_init(&stack);
    size_t kept = 0;
    int depth = 0;
    uint32_t last_root = 0xFFFFFFFFu;

    hui_token token;
    while (hui_html_next(&lexer, &token)) {
        if (token.kind == HUI_TK_EOF) break;
        switch (token.kind) {
            case HUI_TK_TEXT: {
                if (stack.len == 0) break;
                uint32_t parent = hui_find_kept_parent(stack.data, stack.len);
                if (parent == 0xFFFFFFFFu) break;
                int all_ws = 1;
                for (size_t i = 0; i < token.text.n; i++) {
                    char c = token.text.p[i];
                    if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
                        all_ws = 0;
                        break;
                    }
                }
                if (all_ws) break;
                uint32_t idx = hui_dom_add_node(builder->dom, HUI_NODE_TEXT);
                hui_dom_node *node = &builder->dom->nodes.data[idx];
                node->parent = parent;
                node->text = token.text.p;
                node->text_len = (uint32_t) token.text.n;
                if (builder->dom->nodes.data[parent].first_child == 0xFFFFFFFFu) {
                    builder->dom->nodes.data[parent].first_child = idx;
                } else {
                    uint32_t child = builder->dom->nodes.data[parent].first_child;
                    while (builder->dom->nodes.data[child].next_sibling != 0xFFFFFFFFu)
                        child = builder->dom->nodes.data[child].next_sibling;
                    builder->dom->nodes.data[child].next_sibling = idx;
                }
                kept++;
            }
            break;

            case HUI_TK_OPEN:
            case HUI_TK_SELF_CLOSE: {
                hui_tag_probe probe;
                probe.tag = token.tag.p;
                probe.tag_len = token.tag.n;
                probe.id = token.id.n ? token.id.p : NULL;
                probe.id_len = token.id.n;
                probe.class_list = token.class_attr.n ? token.class_attr.p : NULL;
                probe.class_list_len = token.class_attr.n;
                probe.depth = depth;

                hui_filter_decision dec = HUI_FILTER_TAKE;
                if (builder->filter_fn)
                    dec = builder->filter_fn(&probe, builder->filter_user);
                if (dec == HUI_FILTER_TAKE && builder->filter_spec)
                    dec = hui_apply_filter_spec(builder->filter_spec, &probe);

                if (dec == HUI_FILTER_PRUNE_SUBTREE) {
                    int skip_depth = 1;
                    hui_token skip_token;
                    while (hui_html_next(&lexer, &skip_token)) {
                        if (skip_token.kind == HUI_TK_OPEN || skip_token.kind == HUI_TK_SELF_CLOSE) skip_depth++;
                        else if (skip_token.kind == HUI_TK_CLOSE) {
                            skip_depth--;
                            if (skip_depth <= 0) break;
                        } else if (skip_token.kind == HUI_TK_EOF) break;
                    }
                    break;
                }

                uint32_t idx = 0xFFFFFFFFu;
                if (dec == HUI_FILTER_TAKE) {
                    idx = hui_dom_add_node(builder->dom, HUI_NODE_ELEM);
                    hui_dom_node *node = &builder->dom->nodes.data[idx];
                    node->tag = hui_intern_put(builder->atoms, probe.tag, probe.tag_len);
                    if (probe.id) node->id = hui_intern_put(builder->atoms, probe.id, probe.id_len);
                    if (token.type_attr.n)
                        node->attr_type = hui_intern_put(builder->atoms, token.type_attr.p, token.type_attr.n);
                    if (token.placeholder_attr.n) {
                        node->attr_placeholder = token.placeholder_attr.p;
                        node->attr_placeholder_len = (uint32_t) token.placeholder_attr.n;
                    }
                    if (token.value_attr.n) {
                        node->attr_value = token.value_attr.p;
                        node->attr_value_len = (uint32_t) token.value_attr.n;
                    }
                    if (probe.class_list) {
                        const char *p = probe.class_list;
                        size_t n = probe.class_list_len;
                        size_t i = 0;
                        while (i < n) {
                            while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) i++;
                            size_t start = i;
                            while (i < n && !(p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) i++;
                            size_t clen = i - start;
                            if (clen > 0) {
                                hui_atom atom = hui_intern_put(builder->atoms, p + start, clen);
                                hui_vec_push(&node->classes, atom);
                                hui_dom_add_index_class(builder, atom, idx);
                            }
                        }
                    }
                    if (node->id) hui_dom_add_index_id(builder, node->id, idx);
                    uint32_t parent = hui_find_kept_parent(stack.data, stack.len);
                    if (parent != 0xFFFFFFFFu) {
                        node->parent = parent;
                        if (builder->dom->nodes.data[parent].first_child == 0xFFFFFFFFu) {
                            builder->dom->nodes.data[parent].first_child = idx;
                        } else {
                            uint32_t child = builder->dom->nodes.data[parent].first_child;
                            while (builder->dom->nodes.data[child].next_sibling != 0xFFFFFFFFu)
                                child = builder->dom->nodes.data[child].next_sibling;
                            builder->dom->nodes.data[child].next_sibling = idx;
                        }
                    } else {
                        if (last_root != 0xFFFFFFFFu)
                            builder->dom->nodes.data[last_root].next_sibling = idx;
                        else
                            builder->dom->root = idx;
                        last_root = idx;
                    }
                    kept++;
                }

                uint32_t push_idx = (dec == HUI_FILTER_TAKE) ? idx : 0xFFFFFFFFu;
                hui_vec_push(&stack, push_idx);
                depth++;
                if (token.kind == HUI_TK_SELF_CLOSE) {
                    if (stack.len > 0) stack.len--;
                    depth--;
                }
            }
            break;

            case HUI_TK_CLOSE:
                if (depth > 0 && stack.len > 0) {
                    stack.len--;
                    depth--;
                }
                break;

            default:
                break;
        }
    }

    stack.len = 0;
    builder->node_count_after_filter = kept;
    return HUI_OK;
}
