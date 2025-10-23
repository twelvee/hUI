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

static int parse_px_shorthand(const char *s, size_t n, float out[4]) {
    float vals[4] = {0};
    size_t count = 0;
    size_t i = 0;
    while (i < n) {
        while (i < n && isspace((unsigned char) s[i])) i++;
        if (i >= n) break;
        size_t start = i;
        while (i < n && !isspace((unsigned char) s[i])) i++;
        size_t tok_len = i - start;
        if (tok_len == 0 || count >= 4) return -1;
        size_t num_len = tok_len;
        if (tok_len >= 2 && strncmp(s + start + tok_len - 2, "px", 2) == 0) {
            num_len -= 2;
        } else {
            int all_zero = 1;
            for (size_t k = 0; k < tok_len; k++) {
                char c = s[start + k];
                if (!(c == '0' || c == '.' || c == '+' || c == '-')) {
                    all_zero = 0;
                    break;
                }
            }
            if (!all_zero) return -1;
        }
        vals[count++] = parse_float_token(s + start, num_len);
    }
    if (count == 0) return -1;
    float top = vals[0];
    float right = (count >= 2) ? vals[1] : top;
    float bottom = (count >= 3) ? vals[2] : top;
    float left = (count == 4) ? vals[3] : right;
    out[0] = top;
    out[1] = right;
    out[2] = bottom;
    out[3] = left;
    return 0;
}

static char *hui_css_strndup(const char *s, size_t len) {
    if (!s) return NULL;
    char *out = (char *) malloc(len + 1);
    if (!out) return NULL;
    if (len > 0) memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static uint32_t parse_font_weight_value(const char *s, size_t len) {
    while (len > 0 && isspace((unsigned char) s[0])) {
        s++;
        len--;
    }
    while (len > 0 && isspace((unsigned char) s[len - 1])) len--;
    if (len == 0) return 400;
    if ((len == 6 || len == 7) && strncmp(s, "normal", len) == 0) return 400;
    if ((len == 4 || len == 5) && strncmp(s, "bold", len) == 0) return 700;
    float weight = parse_float_token(s, len);
    if (weight < 50.0f) weight = 50.0f;
    if (weight > 1000.0f) weight = 1000.0f;
    return (uint32_t) weight;
}

static uint32_t parse_font_style_value(const char *s, size_t len) {
    while (len > 0 && isspace((unsigned char) s[0])) {
        s++;
        len--;
    }
    while (len > 0 && isspace((unsigned char) s[len - 1])) len--;
    if (len == 0) return 0;
    if ((len == 6 || len == 7) && strncmp(s, "normal", len) == 0) return 0;
    if ((len == 6 || len == 7) && strncmp(s, "italic", len) == 0) return 1;
    return 0;
}

static char *parse_font_family_token(const char *s, size_t len) {
    size_t start = 0;
    while (start < len && isspace((unsigned char) s[start])) start++;
    if (start >= len) return NULL;
    size_t end = len;
    while (end > start && isspace((unsigned char) s[end - 1])) end--;
    if (end <= start) return NULL;
    if (s[start] == '\'' || s[start] == '\"') {
        char quote = s[start];
        start++;
        size_t q = start;
        while (q < end && s[q] != quote) q++;
        if (q <= start) return NULL;
        return hui_css_strndup(s + start, q - start);
    }
    size_t pos = start;
    while (pos < end) {
        if (s[pos] == ',') break;
        pos++;
    }
    size_t token_end = pos;
    while (token_end > start && isspace((unsigned char) s[token_end - 1])) token_end--;
    if (token_end <= start) return NULL;
    return hui_css_strndup(s + start, token_end - start);
}

static char *parse_font_face_src(const char *s, size_t len, size_t *out_len) {
    size_t pos = 0;
    int found = 0;
    while (pos + 4 <= len) {
        char c0 = s[pos];
        char c1 = s[pos + 1];
        char c2 = s[pos + 2];
        char c3 = s[pos + 3];
        if ((c0 == 'u' || c0 == 'U') &&
            (c1 == 'r' || c1 == 'R') &&
            (c2 == 'l' || c2 == 'L') &&
            c3 == '(') {
            pos += 4;
            found = 1;
            break;
        }
        pos++;
    }
    if (!found) return NULL;
    while (pos < len && isspace((unsigned char) s[pos])) pos++;
    if (pos >= len) return NULL;
    char quote = 0;
    if (s[pos] == '\'' || s[pos] == '\"') {
        quote = s[pos];
        pos++;
    }
    size_t start = pos;
    while (pos < len) {
        if (quote) {
            if (s[pos] == quote) break;
        } else if (s[pos] == ')') {
            break;
        }
        pos++;
    }
    if (pos > len) return NULL;
    size_t end = pos;
    if (quote) {
        while (pos < len && s[pos] != ')') pos++;
    }
    if (pos >= len || s[pos] != ')') return NULL;
    size_t trimmed_end = end;
    while (trimmed_end > start && isspace((unsigned char) s[trimmed_end - 1])) trimmed_end--;
    if (trimmed_end <= start) return NULL;
    if (out_len) *out_len = trimmed_end - start;
    return hui_css_strndup(s + start, trimmed_end - start);
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

static void trim_ws(const char **ptr, size_t *len) {
    if (!ptr || !len || !*ptr) return;
    const char *s = *ptr;
    size_t n = *len;
    while (n > 0 && isspace((unsigned char) s[0])) {
        s++;
        n--;
    }
    while (n > 0 && isspace((unsigned char) s[n - 1])) {
        n--;
    }
    *ptr = s;
    *len = n;
}

static const hui_css_custom_prop *hui_css_find_custom_prop(const hui_stylesheet *sheet, hui_atom name) {
    if (!sheet) return NULL;
    for (size_t i = 0; i < sheet->custom_props.len; i++) {
        const hui_css_custom_prop *prop = &sheet->custom_props.data[i];
        if (prop->name == name) return prop;
    }
    return NULL;
}

static int hui_css_set_custom_prop(hui_stylesheet *sheet, hui_atom name, const char *value, size_t value_len) {
    if (!sheet) return -1;
    char *copy = hui_css_strndup(value, value_len);
    if (!copy) return -1;
    for (size_t i = 0; i < sheet->custom_props.len; i++) {
        hui_css_custom_prop *prop = &sheet->custom_props.data[i];
        if (prop->name == name) {
            if (prop->value) free(prop->value);
            prop->value = copy;
            prop->value_len = value_len;
            return 0;
        }
    }
    hui_css_custom_prop prop;
    prop.name = name;
    prop.value = copy;
    prop.value_len = value_len;
    hui_vec_push(&sheet->custom_props, prop);
    return 0;
}

#define HUI_CSS_MAX_VAR_DEPTH 16

static int hui_css_try_resolve_var(const hui_stylesheet *sheet, hui_intern *atoms,
                                   const char **value_ptr, size_t *value_len, int depth) {
    if (!value_ptr || !value_len || !*value_ptr) return -1;
    if (depth > HUI_CSS_MAX_VAR_DEPTH) return -1;
    trim_ws(value_ptr, value_len);
    const char *s = *value_ptr;
    size_t n = *value_len;
    if (n < 4 || strncmp(s, "var(", 4) != 0) return 0;
    size_t pos = 4;
    int paren_depth = 1;
    while (pos < n) {
        char c = s[pos];
        if (c == '(') paren_depth++;
        else if (c == ')') {
            paren_depth--;
            if (paren_depth == 0) break;
        }
        pos++;
    }
    if (paren_depth != 0) return -1;
    size_t inner_len = (pos >= 4) ? (pos - 4) : 0;
    const char *inner = s + 4;
    size_t tail_pos = pos + 1;
    const char *tail_ptr = s + tail_pos;
    size_t tail_len = (tail_pos <= n) ? (n - tail_pos) : 0;
    trim_ws(&tail_ptr, &tail_len);
    if (tail_len != 0) return -1;
    size_t name_start = 0;
    while (name_start < inner_len && isspace((unsigned char) inner[name_start])) name_start++;
    size_t name_end = name_start;
    while (name_end < inner_len && inner[name_end] != ',') name_end++;
    size_t name_len = (name_end > name_start) ? (name_end - name_start) : 0;
    while (name_len > 0 && isspace((unsigned char) inner[name_start + name_len - 1])) name_len--;
    if (name_len < 2 || inner[name_start] != '-' || inner[name_start + 1] != '-') return -1;
    hui_atom var_name = hui_intern_put(atoms, inner + name_start, name_len);
    const hui_css_custom_prop *prop = hui_css_find_custom_prop(sheet, var_name);
    const char *fallback_ptr = NULL;
    size_t fallback_len = 0;
    if (name_end < inner_len && inner[name_end] == ',') {
        size_t fb_start = name_end + 1;
        fallback_ptr = inner + fb_start;
        fallback_len = inner_len - fb_start;
        trim_ws(&fallback_ptr, &fallback_len);
    }
    if (prop) {
        const char *resolved_ptr = prop->value ? prop->value : "";
        size_t resolved_len = prop->value_len;
        int status = hui_css_try_resolve_var(sheet, atoms, &resolved_ptr, &resolved_len, depth + 1);
        if (status < 0) return -1;
        *value_ptr = resolved_ptr;
        *value_len = resolved_len;
        return 1;
    }
    if (fallback_ptr && fallback_len > 0) {
        const char *resolved_ptr = fallback_ptr;
        size_t resolved_len = fallback_len;
        int status = hui_css_try_resolve_var(sheet, atoms, &resolved_ptr, &resolved_len, depth + 1);
        if (status < 0) return -1;
        *value_ptr = resolved_ptr;
        *value_len = resolved_len;
        return 1;
    }
    return -1;
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

static int parse_hex_color(const char *s, size_t n, uint32_t *out_color) {
    if (!s || n < 4 || s[0] != '#') return -1;
    if (n == 7) {
        int r1 = hexval(s[1]), r2 = hexval(s[2]);
        int g1 = hexval(s[3]), g2 = hexval(s[4]);
        int b1 = hexval(s[5]), b2 = hexval(s[6]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) return -1;
        uint32_t r = (uint32_t) (r1 * 16 + r2);
        uint32_t g = (uint32_t) (g1 * 16 + g2);
        uint32_t b = (uint32_t) (b1 * 16 + b2);
        if (out_color) *out_color = 0xFF000000u | (r << 16) | (g << 8) | b;
        return 0;
    }
    if (n == 4) {
        int r = hexval(s[1]);
        int g = hexval(s[2]);
        int b = hexval(s[3]);
        if (r < 0 || g < 0 || b < 0) return -1;
        uint32_t rr = (uint32_t) (r * 16 + r);
        uint32_t gg = (uint32_t) (g * 16 + g);
        uint32_t bb = (uint32_t) (b * 16 + b);
        if (out_color) *out_color = 0xFF000000u | (rr << 16) | (gg << 8) | bb;
        return 0;
    }
    return -1;
}

static int parse_named_color(const char *s, size_t n, uint32_t *out_color) {
    if (!s || n == 0) return -1;
    char buf[32];
    if (n >= sizeof(buf)) return -1;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        buf[i] = c;
    }
    buf[n] = '\0';
    struct color_kw { const char *name; uint32_t value; };
    static const struct color_kw keywords[] = {
        {"black", 0xFF000000u},
        {"silver", 0xFFC0C0C0u},
        {"gray", 0xFF808080u},
        {"white", 0xFFFFFFFFu},
        {"maroon", 0xFF800000u},
        {"red", 0xFFFF0000u},
        {"purple", 0xFF800080u},
        {"fuchsia", 0xFFFF00FFu},
        {"green", 0xFF008000u},
        {"lime", 0xFF00FF00u},
        {"olive", 0xFF808000u},
        {"yellow", 0xFFFFFF00u},
        {"navy", 0xFF000080u},
        {"blue", 0xFF0000FFu},
        {"teal", 0xFF008080u},
        {"aqua", 0xFF00FFFFu},
        {"orange", 0xFFFFA500u},
        {"transparent", 0x00000000u}
    };
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (strcmp(buf, keywords[i].name) == 0) {
            if (out_color) *out_color = keywords[i].value;
            return 0;
        }
    }
    return -1;
}

static uint32_t parse_color_value(const char *s, size_t n, int *ok) {
    const char *ptr = s;
    size_t len = n;
    trim_ws(&ptr, &len);
    uint32_t color = 0;
    if (parse_hex_color(ptr, len, &color) == 0) {
        if (ok) *ok = 1;
        return color;
    }
    if (parse_named_color(ptr, len, &color) == 0) {
        if (ok) *ok = 1;
        return color;
    }
    if (ok) *ok = 0;
    return 0;
}

static int parse_flex_direction_value(const char *s, size_t n, uint32_t *out) {
    const char *ptr = s;
    size_t len = n;
    trim_ws(&ptr, &len);
    if (len == 3 && strncmp(ptr, "row", 3) == 0) {
        if (out) *out = HUI_FLEX_DIRECTION_ROW;
        return 0;
    }
    if (len == 6 && strncmp(ptr, "column", 6) == 0) {
        if (out) *out = HUI_FLEX_DIRECTION_COLUMN;
        return 0;
    }
    return -1;
}

static int parse_justify_content_value(const char *s, size_t n, uint32_t *out) {
    const char *ptr = s;
    size_t len = n;
    trim_ws(&ptr, &len);
    if (len == 10 && strncmp(ptr, "flex-start", 10) == 0) {
        if (out) *out = HUI_FLEX_JUSTIFY_FLEX_START;
        return 0;
    }
    if (len == 8 && strncmp(ptr, "flex-end", 8) == 0) {
        if (out) *out = HUI_FLEX_JUSTIFY_FLEX_END;
        return 0;
    }
    if (len == 6 && strncmp(ptr, "center", 6) == 0) {
        if (out) *out = HUI_FLEX_JUSTIFY_CENTER;
        return 0;
    }
    if (len == 13 && strncmp(ptr, "space-between", 13) == 0) {
        if (out) *out = HUI_FLEX_JUSTIFY_SPACE_BETWEEN;
        return 0;
    }
    if (len == 12 && strncmp(ptr, "space-around", 12) == 0) {
        if (out) *out = HUI_FLEX_JUSTIFY_SPACE_AROUND;
        return 0;
    }
    if (len == 12 && strncmp(ptr, "space-evenly", 12) == 0) {
        if (out) *out = HUI_FLEX_JUSTIFY_SPACE_EVENLY;
        return 0;
    }
    return -1;
}

static int parse_align_value(const char *s, size_t n, uint32_t *out, int allow_auto) {
    const char *ptr = s;
    size_t len = n;
    trim_ws(&ptr, &len);
    if (allow_auto && len == 4 && strncmp(ptr, "auto", 4) == 0) {
        if (out) *out = HUI_FLEX_ALIGN_AUTO;
        return 0;
    }
    if (len == 10 && strncmp(ptr, "flex-start", 10) == 0) {
        if (out) *out = HUI_FLEX_ALIGN_FLEX_START;
        return 0;
    }
    if (len == 8 && strncmp(ptr, "flex-end", 8) == 0) {
        if (out) *out = HUI_FLEX_ALIGN_FLEX_END;
        return 0;
    }
    if (len == 6 && strncmp(ptr, "center", 6) == 0) {
        if (out) *out = HUI_FLEX_ALIGN_CENTER;
        return 0;
    }
    if (len == 7 && strncmp(ptr, "stretch", 7) == 0) {
        if (out) *out = HUI_FLEX_ALIGN_STRETCH;
        return 0;
    }
    return -1;
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
        step.pseudo_mask = HUI_SEL_PSEUDO_NONE;
        int simple_defined = 0;
        if (css[*i] == '.') {
            (*i)++;
            size_t start = *i;
            while (*i < n && is_name_char(css[*i])) (*i)++;
            step.simple.type = HUI_SEL_CLASS;
            step.simple.atom = hui_intern_put(atoms, css + start, *i - start);
            sel.specificity += 10;
            simple_defined = 1;
        } else if (css[*i] == '#') {
            (*i)++;
            size_t start = *i;
            while (*i < n && is_name_char(css[*i])) (*i)++;
            step.simple.type = HUI_SEL_ID;
            step.simple.atom = hui_intern_put(atoms, css + start, *i - start);
            sel.specificity += 100;
            simple_defined = 1;
        } else if (is_name_char(css[*i])) {
            size_t start = *i;
            while (*i < n && is_name_char(css[*i])) (*i)++;
            step.simple.type = HUI_SEL_TAG;
            step.simple.atom = hui_intern_put(atoms, css + start, *i - start);
            sel.specificity += 1;
            simple_defined = 1;
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
        } else if (css[*i] == ':') {
            /* Pseudo selector without preceding simple selector */
        } else {
            (*i)++;
            continue;
        }
        while (*i < n && css[*i] == ':') {
            (*i)++;
            size_t start = *i;
            while (*i < n && is_name_char(css[*i])) (*i)++;
            size_t plen = *i - start;
            if (plen == 5 && strncmp(css + start, "hover", 5) == 0) {
                step.pseudo_mask |= HUI_SEL_PSEUDO_HOVER;
                sel.specificity += 10;
            } else if (plen == 4 && strncmp(css + start, "root", 4) == 0) {
                step.pseudo_mask |= HUI_SEL_PSEUDO_ROOT;
                sel.specificity += 10;
            }
        }
        if (!simple_defined && step.pseudo_mask == HUI_SEL_PSEUDO_NONE) continue;
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
    hui_vec_init(&sheet->font_faces);
    hui_vec_init(&sheet->custom_props);
}

void hui_css_reset(hui_stylesheet *sheet) {
    for (size_t i = 0; i < sheet->rules.len; i++) {
        hui_rule *rule = &sheet->rules.data[i];
        for (size_t j = 0; j < rule->selectors.len; j++) {
            hui_vec_free(&rule->selectors.data[j].steps);
        }
        hui_vec_free(&rule->selectors);
        for (size_t j = 0; j < rule->decls.len; j++) {
            hui_decl *decl = &rule->decls.data[j];
            if (decl->val.kind == HUI_VAL_STRING && decl->val.data.str.ptr) {
                free(decl->val.data.str.ptr);
                decl->val.data.str.ptr = NULL;
                decl->val.data.str.len = 0;
            }
        }
        hui_vec_free(&rule->decls);
    }
    hui_vec_free(&sheet->rules);
    for (size_t i = 0; i < sheet->font_faces.len; i++) {
        hui_css_font_face *face = &sheet->font_faces.data[i];
        if (face->family_name) {
            free(face->family_name);
            face->family_name = NULL;
        }
        if (face->src) {
            free(face->src);
            face->src = NULL;
            face->src_len = 0;
        }
    }
    hui_vec_free(&sheet->font_faces);
    for (size_t i = 0; i < sheet->custom_props.len; i++) {
        hui_css_custom_prop *prop = &sheet->custom_props.data[i];
        if (prop->value) {
            free(prop->value);
            prop->value = NULL;
        }
        prop->value_len = 0;
    }
    hui_vec_free(&sheet->custom_props);
}

int hui_css_parse(hui_stylesheet *sheet, hui_intern *atoms, const char *css, size_t len) {
    for (size_t i = 0; i < sheet->custom_props.len; i++) {
        hui_css_custom_prop *prop = &sheet->custom_props.data[i];
        if (prop->value) {
            free(prop->value);
            prop->value = NULL;
        }
        prop->value_len = 0;
    }
    sheet->custom_props.len = 0;
    size_t i = 0;
    while (1) {
        skip_ws(css, len, &i);
        if (i >= len) break;
        if (css[i] == '@') {
            i++;
            size_t at_start = i;
            while (i < len && is_name_char(css[i])) i++;
            size_t at_len = i - at_start;
            if (at_len == 9 && strncmp(css + at_start, "font-face", 9) == 0) {
                skip_ws(css, len, &i);
                if (i < len && css[i] == '{') i++;
                hui_css_font_face face;
                memset(&face, 0, sizeof(face));
                face.weight = 400;
                face.style = 0;
                while (i < len) {
                    skip_ws(css, len, &i);
                    if (i < len && css[i] == '}') {
                        i++;
                        break;
                    }
                    size_t prop_start = i;
                    while (i < len && is_name_char(css[i])) i++;
                    size_t prop_len = i - prop_start;
                    skip_ws(css, len, &i);
                    if (i < len && css[i] == ':') i++;
                    skip_ws(css, len, &i);
                    size_t prop_val_start = i;
                    while (i < len && css[i] != ';' && css[i] != '}') i++;
                    size_t prop_val_len = i - prop_val_start;
                    const char *prop_ptr = css + prop_start;
                    const char *prop_val_ptr = css + prop_val_start;
                    if (prop_len == 11 && strncmp(prop_ptr, "font-family", 11) == 0) {
                        char *family = parse_font_family_token(prop_val_ptr, prop_val_len);
                        if (family) {
                            if (face.family_name) free(face.family_name);
                            face.family_name = family;
                            face.family_atom = hui_intern_put(atoms, family, strlen(family));
                        }
                    } else if (prop_len == 11 && strncmp(prop_ptr, "font-weight", 11) == 0) {
                        face.weight = parse_font_weight_value(prop_val_ptr, prop_val_len);
                    } else if (prop_len == 10 && strncmp(prop_ptr, "font-style", 10) == 0) {
                        face.style = parse_font_style_value(prop_val_ptr, prop_val_len);
                    } else if (prop_len == 3 && strncmp(prop_ptr, "src", 3) == 0) {
                        size_t src_len = 0;
                        char *src = parse_font_face_src(prop_val_ptr, prop_val_len, &src_len);
                        if (src) {
                            if (face.src) free(face.src);
                            face.src = src;
                            face.src_len = src_len;
                        }
                    }
                    if (i < len && css[i] == ';') i++;
                }
                if (face.family_name && face.src) {
                    hui_vec_push(&sheet->font_faces, face);
                } else {
                    if (face.family_name) free(face.family_name);
                    if (face.src) free(face.src);
                }
                continue;
            } else {
                while (i < len && css[i] != '{') i++;
                if (i < len && css[i] == '{') {
                    int depth = 1;
                    i++;
                    while (i < len && depth > 0) {
                        if (css[i] == '{') depth++;
                        else if (css[i] == '}') depth--;
                        i++;
                    }
                }
                continue;
            }
        }
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
        int rule_is_root_scope = 1;
        if (rule.selectors.len == 0) rule_is_root_scope = 0;
        for (size_t si = 0; si < rule.selectors.len && rule_is_root_scope; si++) {
            const hui_selector *sel = &rule.selectors.data[si];
            int selector_has_root = 0;
            for (size_t sj = 0; sj < sel->steps.len; sj++) {
                if (sel->steps.data[sj].pseudo_mask & HUI_SEL_PSEUDO_ROOT) {
                    selector_has_root = 1;
                    break;
                }
            }
            if (!selector_has_root) rule_is_root_scope = 0;
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
            const char *name_ptr = css + name_start;
            const char *value_ptr = css + value_start;
            trim_ws(&value_ptr, &value_len);
            if (name_len >= 2 && name_ptr[0] == '-' && name_ptr[1] == '-') {
                if (rule_is_root_scope) {
                    hui_atom var_name = hui_intern_put(atoms, name_ptr, name_len);
                    hui_css_set_custom_prop(sheet, var_name, value_ptr, value_len);
                }
                goto after_push;
            }
            const char *resolved_ptr = value_ptr;
            size_t resolved_len = value_len;
            int var_status = hui_css_try_resolve_var(sheet, atoms, &resolved_ptr, &resolved_len, 0);
            if (var_status < 0) goto after_push;
            value_ptr = resolved_ptr;
            value_len = resolved_len;

            hui_decl decl;
            memset(&decl, 0, sizeof(decl));
            if (name_len == 7 && strncmp(name_ptr, "display", 7) == 0) {
                decl.id = HUI_DECL_DISPLAY;
                decl.val.kind = HUI_VAL_ENUM;
                if (value_len == 4 && strncmp(value_ptr, "flex", 4) == 0) decl.val.data.u32 = HUI_DISPLAY_FLEX;
                else if (value_len == 4 && strncmp(value_ptr, "none", 4) == 0) decl.val.data.u32 = HUI_DISPLAY_NONE;
                else if (value_len == 6 && strncmp(value_ptr, "inline", 6) == 0) decl.val.data.u32 = HUI_DISPLAY_INLINE;
                else decl.val.data.u32 = HUI_DISPLAY_BLOCK;
            } else if (name_len == 5 && strncmp(name_ptr, "color", 5) == 0) {
                decl.id = HUI_DECL_COLOR;
                int color_ok = 0;
                uint32_t color = parse_color_value(value_ptr, value_len, &color_ok);
                if (!color_ok) goto after_push;
                decl.val.kind = HUI_VAL_COLOR;
                decl.val.data.u32 = color;
            } else if (name_len == 16 && strncmp(name_ptr, "background-color", 16) == 0) {
                decl.id = HUI_DECL_BG_COLOR;
                int color_ok = 0;
                uint32_t color = parse_color_value(value_ptr, value_len, &color_ok);
                if (!color_ok) goto after_push;
                decl.val.kind = HUI_VAL_COLOR;
                decl.val.data.u32 = color;
            } else if (name_len == 5 && strncmp(name_ptr, "width", 5) == 0) {
                decl.id = HUI_DECL_WIDTH;
                decl.val.kind = HUI_VAL_AUTO;
                if (value_len > 2 && strncmp(value_ptr + value_len - 2, "px", 2) == 0) {
                    decl.val.kind = HUI_VAL_PX;
                    decl.val.data.num = parse_float_token(value_ptr, value_len - 2);
                }
            } else if (name_len == 6 && strncmp(name_ptr, "height", 6) == 0) {
                decl.id = HUI_DECL_HEIGHT;
                decl.val.kind = HUI_VAL_AUTO;
                if (value_len > 2 && strncmp(value_ptr + value_len - 2, "px", 2) == 0) {
                    decl.val.kind = HUI_VAL_PX;
                    decl.val.data.num = parse_float_token(value_ptr, value_len - 2);
                }
            } else if (name_len == 10 && strncmp(name_ptr, "min-height", 10) == 0) {
                decl.id = HUI_DECL_MIN_HEIGHT;
                decl.val.kind = HUI_VAL_AUTO;
                if (value_len > 2 && strncmp(value_ptr + value_len - 2, "px", 2) == 0) {
                    decl.val.kind = HUI_VAL_PX;
                    decl.val.data.num = parse_float_token(value_ptr, value_len - 2);
                }
            } else if (name_len >= 6 && strncmp(name_ptr, "margin", 6) == 0) {
                float sides[4];
                if (parse_px_shorthand(value_ptr, value_len, sides) == 0) {
                    decl.val.kind = HUI_VAL_PX;
                    decl.id = HUI_DECL_MARGIN_TOP;
                    decl.val.data.num = sides[0];
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_MARGIN_RIGHT;
                    decl.val.data.num = sides[1];
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_MARGIN_BOTTOM;
                    decl.val.data.num = sides[2];
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_MARGIN_LEFT;
                    decl.val.data.num = sides[3];
                    hui_vec_push(&rule.decls, decl);
                    goto after_push;
                }
            } else if (name_len >= 7 && strncmp(name_ptr, "padding", 7) == 0) {
                float sides[4];
                if (parse_px_shorthand(value_ptr, value_len, sides) == 0) {
                    decl.val.kind = HUI_VAL_PX;
                    decl.id = HUI_DECL_PADDING_TOP;
                    decl.val.data.num = sides[0];
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_PADDING_RIGHT;
                    decl.val.data.num = sides[1];
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_PADDING_BOTTOM;
                    decl.val.data.num = sides[2];
                    hui_vec_push(&rule.decls, decl);
                    decl.id = HUI_DECL_PADDING_LEFT;
                    decl.val.data.num = sides[3];
                    hui_vec_push(&rule.decls, decl);
                    goto after_push;
                }
            } else if (name_len == 9 && strncmp(name_ptr, "font-size", 9) == 0) {
                decl.id = HUI_DECL_FONT_SIZE;
                decl.val.kind = HUI_VAL_PX;
                if (value_len > 2 && strncmp(value_ptr + value_len - 2, "px", 2) == 0)
                    decl.val.data.num = parse_float_token(value_ptr, value_len - 2);
                else
                    decl.val.data.num = 16.0f;
            } else if (name_len == 11 && strncmp(name_ptr, "font-weight", 11) == 0) {
                decl.id = HUI_DECL_FONT_WEIGHT;
                decl.val.kind = HUI_VAL_NUMBER;
                decl.val.data.num = (float) parse_font_weight_value(value_ptr, value_len);
            } else if (name_len == 10 && strncmp(name_ptr, "font-style", 10) == 0) {
                decl.id = HUI_DECL_FONT_STYLE;
                decl.val.kind = HUI_VAL_ENUM;
                decl.val.data.u32 = parse_font_style_value(value_ptr, value_len);
            } else if (name_len == 11 && strncmp(name_ptr, "font-family", 11) == 0) {
                char *family = parse_font_family_token(value_ptr, value_len);
                if (family) {
                    decl.id = HUI_DECL_FONT_FAMILY;
                    decl.val.kind = HUI_VAL_ATOM;
                    decl.val.data.atom = hui_intern_put(atoms, family, strlen(family));
                    free(family);
                }
            } else if (name_len == 11 && strncmp(name_ptr, "line-height", 11) == 0) {
                decl.id = HUI_DECL_LINE_HEIGHT;
                if (value_len == 6 && strncmp(value_ptr, "normal", 6) == 0) {
                    decl.val.kind = HUI_VAL_ENUM;
                    decl.val.data.u32 = 0;
                } else if (value_len > 2 && strncmp(value_ptr + value_len - 2, "px", 2) == 0) {
                    decl.val.kind = HUI_VAL_PX;
                    decl.val.data.num = parse_float_token(value_ptr, value_len - 2);
                } else {
                    decl.val.kind = HUI_VAL_NUMBER;
                    decl.val.data.num = parse_float_token(value_ptr, value_len);
                }
            } else if (name_len == 14 && strncmp(name_ptr, "flex-direction", 14) == 0) {
                uint32_t dir = 0;
                if (parse_flex_direction_value(value_ptr, value_len, &dir) != 0) goto after_push;
                decl.id = HUI_DECL_FLEX_DIRECTION;
                decl.val.kind = HUI_VAL_ENUM;
                decl.val.data.u32 = dir;
            } else if (name_len == 15 && strncmp(name_ptr, "justify-content", 15) == 0) {
                uint32_t jc = 0;
                if (parse_justify_content_value(value_ptr, value_len, &jc) != 0) goto after_push;
                decl.id = HUI_DECL_JUSTIFY_CONTENT;
                decl.val.kind = HUI_VAL_ENUM;
                decl.val.data.u32 = jc;
            } else if (name_len == 11 && strncmp(name_ptr, "align-items", 11) == 0) {
                uint32_t align = 0;
                if (parse_align_value(value_ptr, value_len, &align, 0) != 0) goto after_push;
                decl.id = HUI_DECL_ALIGN_ITEMS;
                decl.val.kind = HUI_VAL_ENUM;
                decl.val.data.u32 = align;
            } else if (name_len == 9 && strncmp(name_ptr, "flex-grow", 9) == 0) {
                decl.id = HUI_DECL_FLEX_GROW;
                decl.val.kind = HUI_VAL_NUMBER;
                decl.val.data.num = parse_float_token(value_ptr, value_len);
                if (decl.val.data.num < 0.0f) decl.val.data.num = 0.0f;
            } else if (name_len == 11 && strncmp(name_ptr, "flex-shrink", 11) == 0) {
                decl.id = HUI_DECL_FLEX_SHRINK;
                decl.val.kind = HUI_VAL_NUMBER;
                decl.val.data.num = parse_float_token(value_ptr, value_len);
                if (decl.val.data.num < 0.0f) decl.val.data.num = 0.0f;
            } else if (name_len == 10 && strncmp(name_ptr, "flex-basis", 10) == 0) {
                const char *bp = value_ptr;
                size_t bl = value_len;
                trim_ws(&bp, &bl);
                decl.id = HUI_DECL_FLEX_BASIS;
                if (bl == 4 && strncmp(bp, "auto", 4) == 0) {
                    decl.val.kind = HUI_VAL_ENUM;
                    decl.val.data.u32 = 0;
                } else if (bl > 2 && strncmp(bp + bl - 2, "px", 2) == 0) {
                    decl.val.kind = HUI_VAL_NUMBER;
                    decl.val.data.num = parse_float_token(bp, bl - 2);
                } else {
                    decl.val.kind = HUI_VAL_NUMBER;
                    decl.val.data.num = parse_float_token(bp, bl);
                }
            } else if (name_len == 4 && strncmp(name_ptr, "flex", 4) == 0) {
                float grow = 0.0f;
                float shrink = 1.0f;
                float basis = -1.0f;
                int basis_auto = 1;
                const char *fp = value_ptr;
                size_t fl = value_len;
                trim_ws(&fp, &fl);
                if (fl == 4 && strncmp(fp, "none", 4) == 0) {
                    grow = 0.0f;
                    shrink = 0.0f;
                    basis = -1.0f;
                    basis_auto = 1;
                } else if (fl == 4 && strncmp(fp, "auto", 4) == 0) {
                    grow = 1.0f;
                    shrink = 1.0f;
                    basis = -1.0f;
                    basis_auto = 1;
                } else {
                    size_t pos = 0;
                    float first_number = 0.0f;
                    float second_number = 1.0f;
                    size_t number_count = 0;
                    float basis_value = -1.0f;
                    int basis_set = 0;
                    while (pos < fl) {
                        while (pos < fl && isspace((unsigned char) fp[pos])) pos++;
                        if (pos >= fl) break;
                        size_t start = pos;
                        while (pos < fl && !isspace((unsigned char) fp[pos])) pos++;
                        size_t tok_len = pos - start;
                        if (tok_len == 0) continue;
                        const char *tok = fp + start;
                        if (tok_len == 4 && strncmp(tok, "auto", 4) == 0) {
                            basis = -1.0f;
                            basis_auto = 1;
                        } else if (tok_len > 2 && strncmp(tok + tok_len - 2, "px", 2) == 0) {
                            basis = parse_float_token(tok, tok_len - 2);
                            basis_auto = 0;
                            basis_set = 1;
                        } else {
                            float num = parse_float_token(tok, tok_len);
                            if (number_count == 0) {
                                first_number = num;
                                number_count = 1;
                            } else if (number_count == 1) {
                                second_number = num;
                                number_count = 2;
                            } else if (!basis_set) {
                                basis_value = num;
                                basis_set = 1;
                            }
                        }
                    }
                    if (number_count >= 1) grow = first_number;
                    if (number_count >= 2) shrink = second_number;
                    if (basis_set) {
                        basis = basis_value;
                        basis_auto = 0;
                    }
                }
                decl.id = HUI_DECL_FLEX_GROW;
                decl.val.kind = HUI_VAL_NUMBER;
                decl.val.data.num = (grow < 0.0f) ? 0.0f : grow;
                hui_vec_push(&rule.decls, decl);
                decl.id = HUI_DECL_FLEX_SHRINK;
                decl.val.data.num = (shrink < 0.0f) ? 0.0f : shrink;
                hui_vec_push(&rule.decls, decl);
                decl.id = HUI_DECL_FLEX_BASIS;
                if (basis_auto) {
                    decl.val.kind = HUI_VAL_ENUM;
                    decl.val.data.u32 = 0;
                } else {
                    decl.val.kind = HUI_VAL_NUMBER;
                    decl.val.data.num = basis;
                }
                hui_vec_push(&rule.decls, decl);
                goto after_push;
            } else if (name_len == 10 && strncmp(name_ptr, "align-self", 10) == 0) {
                uint32_t align = 0;
                if (parse_align_value(value_ptr, value_len, &align, 1) != 0) goto after_push;
                decl.id = HUI_DECL_ALIGN_SELF;
                decl.val.kind = HUI_VAL_ENUM;
                decl.val.data.u32 = align;
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
