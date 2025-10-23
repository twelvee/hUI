#include "hui_style.h"

#include <string.h>
#include <stdlib.h>
#include "../../include/hui/hui.h"

static int hui_node_has_class(const hui_dom_node *node, hui_atom atom) {
    for (size_t i = 0; i < node->classes.len; i++) {
        if (node->classes.data[i] == atom) return 1;
    }
    return 0;
}

void hui_style_store_init(hui_style_store *store) {
    hui_vec_init(&store->styles);
}

void hui_style_store_reset(hui_style_store *store) {
    if (!store) return;
    store->styles.len = 0;
}

void hui_style_store_release(hui_style_store *store) {
    if (!store) return;
    free(store->styles.data);
    store->styles.data = NULL;
    store->styles.len = store->styles.cap = 0;
}

typedef struct {
    hui_atom atom;
    HUI_VEC(uint32_t) nodes;
} hui_tag_bucket;

static int hui_u32_vec_copy(HUI_VEC(uint32_t) *dst, const HUI_VEC(uint32_t) *src) {
    if (hui_vec_reserve(dst, src->len) != 0) {
        dst->len = 0;
        return -1;
    }
    if (src->len > 0 && dst->data && src->data) {
        memcpy(dst->data, src->data, src->len * sizeof(uint32_t));
    }
    dst->len = src->len;
    return 0;
}

static void hui_collect_nodes_by_class(const hui_dom *dom, hui_atom atom, HUI_VEC(uint32_t) *out) {
    out->len = 0;
    if (!atom) return;
    for (size_t i = 0; i < dom->class_keys.len; i++) {
        if ((hui_atom) dom->class_keys.data[i] != atom) continue;
        uint32_t offset = dom->class_offsets.data[i];
        uint32_t count = dom->class_counts.data[i];
        if (count == 0) return;
        if (hui_vec_reserve(out, out->len + count) != 0) {
            out->len = 0;
            return;
        }
        for (uint32_t j = 0; j < count; j++) {
            out->data[out->len++] = dom->class_items.data[offset + j];
        }
        return;
    }
}

static void hui_collect_nodes_by_id(const hui_dom *dom, hui_atom atom, HUI_VEC(uint32_t) *out) {
    out->len = 0;
    if (!atom) return;
    for (size_t i = 0; i < dom->id_keys.len; i++) {
        if ((hui_atom) dom->id_keys.data[i] == atom) {
            if (hui_vec_reserve(out, out->len + 1) != 0) {
                out->len = 0;
                return;
            }
            out->data[out->len++] = dom->id_vals.data[i];
            return;
        }
    }
}

static void hui_collect_nodes_by_tag(const hui_dom *dom, hui_atom atom, HUI_VEC(uint32_t) *out,
                                     HUI_VEC(hui_tag_bucket) *cache) {
    out->len = 0;
    if (!atom) return;
    for (size_t i = 0; i < cache->len; i++) {
        const hui_tag_bucket *bucket = &cache->data[i];
        if (bucket->atom == atom) {
            hui_u32_vec_copy(out, &bucket->nodes);
            return;
        }
    }
    hui_tag_bucket bucket;
    bucket.atom = atom;
    hui_vec_init(&bucket.nodes);
    for (size_t i = 0; i < dom->nodes.len; i++) {
        const hui_dom_node *node = &dom->nodes.data[i];
        if (node->type == HUI_NODE_ELEM && node->tag == atom) {
            hui_vec_push(&bucket.nodes, (uint32_t) i);
        }
    }
    hui_u32_vec_copy(out, &bucket.nodes);
    hui_vec_push(cache, bucket);
}

static void hui_collect_selector_candidates(const hui_dom *dom, const hui_selector *sel,
                                            HUI_VEC(uint32_t) *out,
                                            const HUI_VEC(uint32_t) *all_elements,
                                            HUI_VEC(hui_tag_bucket) *tag_cache) {
    out->len = 0;
    if (!sel || sel->steps.len == 0) {
        hui_u32_vec_copy(out, all_elements);
        return;
    }
    const hui_sel_step *first = &sel->steps.data[0];
    if (first->simple.type == HUI_SEL_ID) {
        hui_collect_nodes_by_id(dom, first->simple.atom, out);
    } else if (first->simple.type == HUI_SEL_CLASS) {
        hui_collect_nodes_by_class(dom, first->simple.atom, out);
    } else if (first->simple.type == HUI_SEL_TAG) {
        hui_collect_nodes_by_tag(dom, first->simple.atom, out, tag_cache);
    }
    if (out->len == 0) {
        hui_u32_vec_copy(out, all_elements);
    }
}

static int hui_match_selector(const hui_selector *sel, const hui_dom *dom, uint32_t idx) {
    const hui_dom_node *node = &dom->nodes.data[idx];
    size_t step = 0;
    uint32_t current = idx;
    while (1) {
        if (step >= sel->steps.len) return 1;
        hui_sel_step s = sel->steps.data[step];
        if (node->type != HUI_NODE_ELEM) return 0;
        int ok = 1;
        if (s.simple.type == HUI_SEL_TAG) {
            if (s.simple.atom != 0) ok = (node->tag == s.simple.atom);
        } else if (s.simple.type == HUI_SEL_ID) {
            ok = (node->id == s.simple.atom);
        } else if (s.simple.type == HUI_SEL_CLASS) {
            ok = hui_node_has_class(node, s.simple.atom);
        }
        if (!ok) return 0;
        if ((s.pseudo_mask & HUI_SEL_PSEUDO_HOVER) && !(node->flags & HUI_NODE_FLAG_HOVER))
            return 0;
        if ((s.pseudo_mask & HUI_SEL_PSEUDO_ROOT) && current != dom->root)
            return 0;
        if (step + 1 >= sel->steps.len) return 1;
        hui_combinator comb = sel->steps.data[step].comb;
        step++;
        if (comb == HUI_COMB_CHILD) {
            if (node->parent == 0xFFFFFFFFu) return 0;
            current = node->parent;
            node = &dom->nodes.data[current];
        } else {
            uint32_t parent = node->parent;
            int found = 0;
            while (parent != 0xFFFFFFFFu) {
                const hui_dom_node *parent_node = &dom->nodes.data[parent];
                const hui_sel_step *next_step = &sel->steps.data[step];
                int match = 0;
                if (parent_node->type == HUI_NODE_ELEM) {
                    if (next_step->simple.type == HUI_SEL_TAG) match = (parent_node->tag == next_step->simple.atom);
                    else if (next_step->simple.type == HUI_SEL_ID) match = (parent_node->id == next_step->simple.atom);
                    else if (next_step->simple.type == HUI_SEL_CLASS) match = hui_node_has_class(
                                                                          parent_node, next_step->simple.atom);
                }
                if (match) {
                    current = parent;
                    node = parent_node;
                    found = 1;
                    break;
                }
                parent = parent_node->parent;
            }
            if (!found) return 0;
            step++;
            if (step >= sel->steps.len) return 1;
        }
    }
}

void hui_apply_styles(hui_style_store *store, hui_dom *dom, hui_intern *atoms, const hui_stylesheet *sheet,
                      uint32_t property_mask) {
    (void) atoms;
    (void) property_mask;
    size_t node_count = dom->nodes.len;
    if (store->styles.cap < node_count) hui_vec_reserve(&store->styles, node_count);
    store->styles.len = node_count;
    for (size_t i = 0; i < node_count; i++) {
        hui_computed_style *cs = &store->styles.data[i];
        memset(cs, 0, sizeof(*cs));
        cs->display = 1;
        cs->width = -1.0f;
        cs->height = -1.0f;
        cs->min_height = 0.0f;
        cs->font_size = 16.0f;
        cs->font_weight = 400;
        cs->font_style = HUI_FONT_STYLE_NORMAL;
        cs->line_height = 0.0f;
        cs->font_family = 0;
        cs->font_id = HUI_FONT_ID_NONE;
        cs->color = 0xFF000000u;
        cs->bg_color = 0x00000000u;
    }

    HUI_VEC(uint32_t) all_elements;
    hui_vec_init(&all_elements);
    for (size_t i = 0; i < node_count; i++) {
        const hui_dom_node *node = &dom->nodes.data[i];
        if (node->type == HUI_NODE_ELEM) {
            hui_vec_push(&all_elements, (uint32_t) i);
        }
    }

    HUI_VEC(hui_tag_bucket) tag_cache;
    hui_vec_init(&tag_cache);

    HUI_VEC(uint32_t) selector_candidates;
    HUI_VEC(uint32_t) rule_candidates;
    hui_vec_init(&selector_candidates);
    hui_vec_init(&rule_candidates);

    uint8_t *rule_marker = (node_count > 0) ? (uint8_t *) malloc(node_count) : NULL;

    for (size_t ri = 0; ri < sheet->rules.len; ri++) {
        const hui_rule *rule = &sheet->rules.data[ri];
        if (rule_marker) memset(rule_marker, 0, node_count);
        rule_candidates.len = 0;

        for (size_t si = 0; si < rule->selectors.len; si++) {
            const hui_selector *sel = &rule->selectors.data[si];
            selector_candidates.len = 0;
            hui_collect_selector_candidates(dom, sel, &selector_candidates, &all_elements, &tag_cache);
            for (size_t ci = 0; ci < selector_candidates.len; ci++) {
                uint32_t idx = selector_candidates.data[ci];
                if (idx >= node_count) continue;
                if (rule_marker && rule_marker[idx]) continue;
                if (rule_marker) rule_marker[idx] = 1;
                hui_vec_push(&rule_candidates, idx);
            }
        }

        for (size_t ci = 0; ci < rule_candidates.len; ci++) {
            uint32_t idx = rule_candidates.data[ci];
            if (idx >= node_count) continue;
            const hui_dom_node *node = &dom->nodes.data[idx];
            if (node->type != HUI_NODE_ELEM) continue;
            int matched = 0;
            for (size_t si = 0; si < rule->selectors.len; si++) {
                if (hui_match_selector(&rule->selectors.data[si], dom, idx)) {
                    matched = 1;
                    break;
                }
            }
            if (!matched) continue;
            hui_computed_style *cs = &store->styles.data[idx];
            for (size_t di = 0; di < rule->decls.len; di++) {
                const hui_decl *decl = &rule->decls.data[di];
                switch (decl->id) {
                    case HUI_DECL_DISPLAY: cs->display = decl->val.data.u32;
                        cs->present_mask |= HUI_STYLE_PRESENT_DISPLAY;
                        break;
                    case HUI_DECL_WIDTH: cs->width =
                            (decl->val.kind == HUI_VAL_PX || decl->val.kind == HUI_VAL_NUMBER)
                                    ? decl->val.data.num : -1.0f;
                        cs->present_mask |= HUI_STYLE_PRESENT_WIDTH;
                        break;
                    case HUI_DECL_HEIGHT: cs->height =
                            (decl->val.kind == HUI_VAL_PX || decl->val.kind == HUI_VAL_NUMBER)
                                    ? decl->val.data.num : -1.0f;
                        cs->present_mask |= HUI_STYLE_PRESENT_HEIGHT;
                        break;
                    case HUI_DECL_MIN_HEIGHT:
                        if (decl->val.kind == HUI_VAL_PX || decl->val.kind == HUI_VAL_NUMBER) {
                            cs->min_height = decl->val.data.num;
                            if (cs->min_height < 0.0f) cs->min_height = 0.0f;
                            cs->present_mask |= HUI_STYLE_PRESENT_MIN_HEIGHT;
                        }
                        break;
                    case HUI_DECL_MARGIN_TOP: cs->margin[0] = decl->val.data.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_MARGIN;
                        break;
                    case HUI_DECL_MARGIN_RIGHT: cs->margin[1] = decl->val.data.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_MARGIN;
                        break;
                    case HUI_DECL_MARGIN_BOTTOM: cs->margin[2] = decl->val.data.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_MARGIN;
                        break;
                    case HUI_DECL_MARGIN_LEFT: cs->margin[3] = decl->val.data.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_MARGIN;
                        break;
                    case HUI_DECL_PADDING_TOP: cs->padding[0] = decl->val.data.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_PADDING;
                        break;
                    case HUI_DECL_PADDING_RIGHT: cs->padding[1] = decl->val.data.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_PADDING;
                        break;
                    case HUI_DECL_PADDING_BOTTOM: cs->padding[2] = decl->val.data.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_PADDING;
                        break;
                    case HUI_DECL_PADDING_LEFT: cs->padding[3] = decl->val.data.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_PADDING;
                        break;
                    case HUI_DECL_BG_COLOR: cs->bg_color = decl->val.data.u32;
                        cs->present_mask |= HUI_STYLE_PRESENT_BG_COLOR;
                        break;
                    case HUI_DECL_COLOR: cs->color = decl->val.data.u32;
                        cs->present_mask |= HUI_STYLE_PRESENT_COLOR;
                        break;
                    case HUI_DECL_FONT_SIZE: cs->font_size = decl->val.data.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_FONT_SIZE;
                        break;
                    case HUI_DECL_FONT_WEIGHT: {
                        uint32_t weight = 400;
                        if (decl->val.kind == HUI_VAL_NUMBER || decl->val.kind == HUI_VAL_PX)
                            weight = (uint32_t) decl->val.data.num;
                        else if (decl->val.kind == HUI_VAL_ENUM)
                            weight = decl->val.data.u32;
                        if (weight < 50) weight = 50;
                        if (weight > 1000) weight = 1000;
                        cs->font_weight = weight;
                        cs->present_mask |= HUI_STYLE_PRESENT_FONT_WEIGHT;
                        break;
                    }
                    case HUI_DECL_FONT_STYLE:
                        cs->font_style = decl->val.data.u32;
                        cs->present_mask |= HUI_STYLE_PRESENT_FONT_STYLE;
                        break;
                    case HUI_DECL_FONT_FAMILY:
                        cs->font_family = decl->val.data.atom;
                        cs->present_mask |= HUI_STYLE_PRESENT_FONT_FAMILY;
                        break;
                    case HUI_DECL_LINE_HEIGHT:
                        if (decl->val.kind == HUI_VAL_ENUM) {
                            cs->line_height = 0.0f;
                        } else {
                            cs->line_height = decl->val.data.num;
                        }
                        cs->present_mask |= HUI_STYLE_PRESENT_LINE_HEIGHT;
                        break;
                    default: break;
                }
            }
        }
    }

    if (rule_marker) free(rule_marker);
    hui_vec_free(&rule_candidates);
    hui_vec_free(&selector_candidates);
    for (size_t i = 0; i < tag_cache.len; i++) {
        hui_vec_free(&tag_cache.data[i].nodes);
    }
    hui_vec_free(&tag_cache);
    hui_vec_free(&all_elements);

    for (size_t i = 0; i < dom->nodes.len; i++) {
        const hui_dom_node *node = &dom->nodes.data[i];
        if (node->parent == 0xFFFFFFFFu) continue;
        hui_computed_style *cs = &store->styles.data[i];
        const hui_computed_style *parent = &store->styles.data[node->parent];
        if (!(cs->present_mask & HUI_STYLE_PRESENT_COLOR)) cs->color = parent->color;
        if (!(cs->present_mask & HUI_STYLE_PRESENT_FONT_SIZE)) cs->font_size = parent->font_size;
        if (!(cs->present_mask & HUI_STYLE_PRESENT_FONT_WEIGHT)) cs->font_weight = parent->font_weight;
        if (!(cs->present_mask & HUI_STYLE_PRESENT_FONT_STYLE)) cs->font_style = parent->font_style;
        if (!(cs->present_mask & HUI_STYLE_PRESENT_FONT_FAMILY)) cs->font_family = parent->font_family;
        if (!(cs->present_mask & HUI_STYLE_PRESENT_LINE_HEIGHT)) cs->line_height = parent->line_height;
    }
}
