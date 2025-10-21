#include "hui_style.h"

#include <string.h>
#include <stdlib.h>

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
    free(store->styles.data);
    store->styles.data = NULL;
    store->styles.len = store->styles.cap = 0;
}

static int hui_match_selector(const hui_selector *sel, const hui_dom *dom, uint32_t idx) {
    const hui_dom_node *node = &dom->nodes.data[idx];
    size_t step = 0;
    uint32_t current = idx;
    while (1) {
        if (step >= sel->steps.len) return 1;
        hui_sel_step s = sel->steps.data[step];
        if (node->type != HUI_NODE_ELEM) return 0;
        int ok = 0;
        if (s.simple.type == HUI_SEL_TAG) ok = (node->tag == s.simple.atom);
        else if (s.simple.type == HUI_SEL_ID) ok = (node->id == s.simple.atom);
        else if (s.simple.type == HUI_SEL_CLASS) ok = hui_node_has_class(node, s.simple.atom);
        if (!ok) return 0;
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
    if (store->styles.cap < dom->nodes.len) hui_vec_reserve(&store->styles, dom->nodes.len);
    store->styles.len = dom->nodes.len;
    for (size_t i = 0; i < dom->nodes.len; i++) {
        hui_computed_style *cs = &store->styles.data[i];
        memset(cs, 0, sizeof(*cs));
        cs->display = 1;
        cs->width = -1.0f;
        cs->height = -1.0f;
        cs->font_size = 16.0f;
        cs->font_weight = 400;
        cs->color = 0xFF000000u;
        cs->bg_color = 0x00000000u;
    }

    for (size_t ri = 0; ri < sheet->rules.len; ri++) {
        const hui_rule *rule = &sheet->rules.data[ri];
        for (size_t ni = 0; ni < dom->nodes.len; ni++) {
            const hui_dom_node *node = &dom->nodes.data[ni];
            if (node->type != HUI_NODE_ELEM) continue;
            int matched = 0;
            for (size_t si = 0; si < rule->selectors.len; si++) {
                if (hui_match_selector(&rule->selectors.data[si], dom, (uint32_t) ni)) {
                    matched = 1;
                    break;
                }
            }
            if (!matched) continue;
            hui_computed_style *cs = &store->styles.data[ni];
            for (size_t di = 0; di < rule->decls.len; di++) {
                const hui_decl *decl = &rule->decls.data[di];
                switch (decl->id) {
                    case HUI_DECL_DISPLAY: cs->display = decl->val.u32;
                        cs->present_mask |= HUI_STYLE_PRESENT_DISPLAY;
                        break;
                    case HUI_DECL_WIDTH: cs->width = (decl->val.kind == HUI_VAL_PX) ? decl->val.num : -1.0f;
                        cs->present_mask |= HUI_STYLE_PRESENT_WIDTH;
                        break;
                    case HUI_DECL_HEIGHT: cs->height = (decl->val.kind == HUI_VAL_PX) ? decl->val.num : -1.0f;
                        cs->present_mask |= HUI_STYLE_PRESENT_HEIGHT;
                        break;
                    case HUI_DECL_MARGIN_TOP: cs->margin[0] = decl->val.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_MARGIN;
                        break;
                    case HUI_DECL_MARGIN_RIGHT: cs->margin[1] = decl->val.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_MARGIN;
                        break;
                    case HUI_DECL_MARGIN_BOTTOM: cs->margin[2] = decl->val.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_MARGIN;
                        break;
                    case HUI_DECL_MARGIN_LEFT: cs->margin[3] = decl->val.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_MARGIN;
                        break;
                    case HUI_DECL_PADDING_TOP: cs->padding[0] = decl->val.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_PADDING;
                        break;
                    case HUI_DECL_PADDING_RIGHT: cs->padding[1] = decl->val.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_PADDING;
                        break;
                    case HUI_DECL_PADDING_BOTTOM: cs->padding[2] = decl->val.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_PADDING;
                        break;
                    case HUI_DECL_PADDING_LEFT: cs->padding[3] = decl->val.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_PADDING;
                        break;
                    case HUI_DECL_BG_COLOR: cs->bg_color = decl->val.u32;
                        cs->present_mask |= HUI_STYLE_PRESENT_BG_COLOR;
                        break;
                    case HUI_DECL_COLOR: cs->color = decl->val.u32;
                        cs->present_mask |= HUI_STYLE_PRESENT_COLOR;
                        break;
                    case HUI_DECL_FONT_SIZE: cs->font_size = decl->val.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_FONT_SIZE;
                        break;
                    case HUI_DECL_FONT_WEIGHT: cs->font_weight = (uint32_t) decl->val.num;
                        cs->present_mask |= HUI_STYLE_PRESENT_FONT_WEIGHT;
                        break;
                    default: break;
                }
            }
        }
    }

    for (size_t i = 0; i < dom->nodes.len; i++) {
        const hui_dom_node *node = &dom->nodes.data[i];
        if (node->parent == 0xFFFFFFFFu) continue;
        hui_computed_style *cs = &store->styles.data[i];
        const hui_computed_style *parent = &store->styles.data[node->parent];
        if (!(cs->present_mask & HUI_STYLE_PRESENT_COLOR)) cs->color = parent->color;
        if (!(cs->present_mask & HUI_STYLE_PRESENT_FONT_SIZE)) cs->font_size = parent->font_size;
        if (!(cs->present_mask & HUI_STYLE_PRESENT_FONT_WEIGHT)) cs->font_weight = parent->font_weight;
    }
}
