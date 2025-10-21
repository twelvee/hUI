#include "hui_css_parser.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static float parse_float_token(const char *s, size_t n) {
    if (!s || n == 0) return 0.0f;
    size_t start = 0;
    while (start < n && isspace((unsigned char) s[start])) start++;
    while (n > start && isspace((unsigned char) s[n - 1])) n--;
    if (n <= start) return 0.0f;
    size_t len = n - start;
    if (len >= 63) len = 63;
    char buf[64];
    memcpy(buf, s + start, len);
    buf[len] = '\0';
    return (float) atof(buf);
}

static void skip_ws(const char *s, size_t n, size_t *i) {
    while (*i < n) {
        if (isspace((unsigned char) s[*i])) {
            (*i)++;
            continue;
        }
        if (*i + 1 < n && s[*i] == '/' && s[*i + 1] == '*') {
            *i += 2;
            while (*i + 1 < n && !(s[*i] == '*' && s[*i + 1] == '/')) (*i)++;
            if (*i + 1 < n) *i += 2;
            continue;
        }
        break;
    }
}

static int is_name_char(char c) {
    return isalnum((unsigned char) c) || c == '-' || c == '_';
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static uint32_t parse_hex_color(const char *s, size_t n) {
    if (n == 7 && s[0] == '#') {
        int r1 = hexval(s[1]), r2 = hexval(s[2]);
        int g1 = hexval(s[3]), g2 = hexval(s[4]);
        int b1 = hexval(s[5]), b2 = hexval(s[6]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) return 0xFF000000u;
        uint32_t r = (uint32_t) (r1 * 16 + r2);
        uint32_t g = (uint32_t) (g1 * 16 + g2);
        uint32_t b = (uint32_t) (b1 * 16 + b2);
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    return 0xFF000000u;
}

static hui_selector parse_selector(hui_intern *atoms, const char *css, size_t n, size_t *i) {
    hui_selector sel;
    hui_vec_init(&sel.steps);
    sel.specificity = 0;
    typedef struct {
        hui_sel_step step;
    } Temp;
    HUI_VEC(Temp) temp;
    hui_vec_init(&temp);
    skip_ws(css, n, i);
    while (*i < n) {
        if (css[*i] == ',' || css[*i] == '{') break;
        hui_sel_step step;
        step.comb = HUI_COMB_DESC;
        step.simple.type = 0;
        step.simple.atom = 0;
        if (css[*i] == '.') {
            (*i)++;
            size_t start = *i;
            while (*i < n && is_name_char(css[*i])) (*i)++;
            step.simple.type = HUI_SEL_CLASS;
            step.simple.atom = hui_intern_put(atoms, css + start, *i - start);
            sel.specificity += 10;
        } else if (css[*i] == '#') {
            (*i)++;
            size_t start = *i;
            while (*i < n && is_name_char(css[*i])) (*i)++;
            step.simple.type = HUI_SEL_ID;
            step.simple.atom = hui_intern_put(atoms, css + start, *i - start);
            sel.specificity += 100;
        } else if (is_name_char(css[*i])) {
            size_t start = *i;
            while (*i < n && is_name_char(css[*i])) (*i)++;
            step.simple.type = HUI_SEL_TAG;
            step.simple.atom = hui_intern_put(atoms, css + start, *i - start);
            sel.specificity += 1;
        } else if (css[*i] == '>') {
            (*i)++;
            skip_ws(css, n, i);
            if (temp.len > 0)
                temp.data[temp.len - 1].step.comb = HUI_COMB_CHILD;
            continue;
        } else if (isspace((unsigned char) css[*i])) {
            skip_ws(css, n, i);
            if (temp.len > 0)
                temp.data[temp.len - 1].step.comb = HUI_COMB_DESC;
            continue;
        } else {
            (*i)++;
            continue;
        }
        Temp t;
        t.step = step;
        hui_vec_push(&temp, t);
        skip_ws(css, n, i);
    }
    for (size_t k = 0; k < temp.len; k++) {
        hui_sel_step step = temp.data[temp.len - 1 - k].step;
        hui_vec_push(&sel.steps, step);
    }
    hui_vec_free(&temp);
    return sel;
}

void hui_css_init(hui_stylesheet *sheet) {
    hui_vec_init(&sheet->rules);
}

void hui_css_reset(hui_stylesheet *sheet) {
    for (size_t i = 0; i < sheet->rules.len; i++) {
        hui_rule *rule = &sheet->rules.data[i];
        for (size_t j = 0; j < rule->selectors.len; j++) {
            hui_vec_free(&rule->selectors.data[j].steps);
        }
        hui_vec_free(&rule->selectors);
        hui_vec_free(&rule->decls);
    }
    hui_vec_free(&sheet->rules);
}

int hui_css_parse(hui_stylesheet *sheet, hui_intern *atoms, const char *css, size_t len) {
    size_t i = 0;
    while (1) {
        skip_ws(css, len, &i);
        if (i >= len) break;
        hui_rule rule;
        hui_vec_init(&rule.selectors);
        hui_vec_init(&rule.decls);
        while (1) {
            hui_selector sel = parse_selector(atoms, css, len, &i);
            hui_vec_push(&rule.selectors, sel);
            skip_ws(css, len, &i);
            if (i < len && css[i] == ',') {
                i++;
                continue;
            }
            if (i < len && css[i] == '{') {
                i++;
                break;
            }
            break;
        }
        while (i < len) {
            skip_ws(css, len, &i);
            if (i < len && css[i] == '}') {
                i++;
                break;
            }
            size_t name_start = i;
            while (i < len && is_name_char(css[i])) i++;
            size_t name_len = i - name_start;
            skip_ws(css, len, &i);
            if (i < len && css[i] == ':') i++;
            skip_ws(css, len, &i);
            size_t value_start = i;
            while (i < len && css[i] != ';' && css[i] != '}') i++;
            size_t value_len = i - value_start;

            hui_decl decl;
            memset(&decl, 0, sizeof(decl));
            if (name_len == 7 && strncmp(css + name_start, "display", 7) == 0) {
                decl.id = HUI_DECL_DISPLAY;
                decl.val.kind = HUI_VAL_ENUM;
                if (value_len == 5 && strncmp(css + value_start, "block", 5) == 0) decl.val.u32 = 1;
                else if (value_len == 6 && strncmp(css + value_start, "inline", 6) == 0) decl.val.u32 = 2;
                else decl.val.u32 = 1;
            } else if (name_len == 5 && strncmp(css + name_start, "color", 5) == 0) {
                decl.id = HUI_DECL_COLOR;
                decl.val.kind = HUI_VAL_COLOR;
                decl.val.u32 = parse_hex_color(css + value_start, value_len);
            } else if (name_len == 16 && strncmp(css + name_start, "background-color", 16) == 0) {
                decl.id = HUI_DECL_BG_COLOR;
                decl.val.kind = HUI_VAL_COLOR;
                decl.val.u32 = parse_hex_color(css + value_start, value_len);
            } else if (name_len == 5 && strncmp(css + name_start, "width", 5) == 0) {
                decl.id = HUI_DECL_WIDTH;
                decl.val.kind = HUI_VAL_AUTO;
                if (value_len > 2 && strncmp(css + value_start + value_len - 2, "px", 2) == 0) {
                    decl.val.kind = HUI_VAL_PX;
                    decl.val.num = parse_float_token(css + value_start, value_len - 2);
                }
            } else if (name_len == 6 && strncmp(css + name_start, "height", 6) == 0) {
                decl.id = HUI_DECL_HEIGHT;
                decl.val.kind = HUI_VAL_AUTO;
                if (value_len > 2 && strncmp(css + value_start + value_len - 2, "px", 2) == 0) {
                    decl.val.kind = HUI_VAL_PX;
                    decl.val.num = parse_float_token(css + value_start, value_len - 2);
                }
            } else if (name_len >= 6 && strncmp(css + name_start, "margin", 6) == 0) {
                if (value_len > 2 && strncmp(css + value_start + value_len - 2, "px", 2) == 0) {
                    float px = parse_float_token(css + value_start, value_len - 2);
                    decl.id = HUI_DECL_MARGIN_TOP;
                    decl.val.kind = HUI_VAL_PX;
                    decl.val.num = px;
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_MARGIN_RIGHT;
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_MARGIN_BOTTOM;
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_MARGIN_LEFT;
                    hui_vec_push(&rule.decls, decl);
                    goto after_push;
                }
            } else if (name_len >= 7 && strncmp(css + name_start, "padding", 7) == 0) {
                if (value_len > 2 && strncmp(css + value_start + value_len - 2, "px", 2) == 0) {
                    float px = parse_float_token(css + value_start, value_len - 2);
                    decl.id = HUI_DECL_PADDING_TOP;
                    decl.val.kind = HUI_VAL_PX;
                    decl.val.num = px;
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_PADDING_RIGHT;
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_PADDING_BOTTOM;
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_PADDING_LEFT;
                    hui_vec_push(&rule.decls, decl);
                    goto after_push;
                }
            } else if (name_len == 9 && strncmp(css + name_start, "font-size", 9) == 0) {
                decl.id = HUI_DECL_FONT_SIZE;
                decl.val.kind = HUI_VAL_PX;
                if (value_len > 2 && strncmp(css + value_start + value_len - 2, "px", 2) == 0)
                    decl.val.num = parse_float_token(css + value_start, value_len - 2);
                else
                    decl.val.num = 16.0f;
            } else {
                decl.id = 0;
            }

            if (decl.id != 0)
                hui_vec_push(&rule.decls, decl);
        after_push:
            if (i < len && css[i] == ';') i++;
            if (i < len && css[i] == '}') {
                i++;
                break;
            }
        }
        hui_vec_push(&sheet->rules, rule);
    }
    return 0;
}
