#ifndef HUI_HUI_HTML_TAGS_H
#define HUI_HUI_HTML_TAGS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    size_t length;
} hui_html_tag_entry;

extern const char *const hui_html_tag_names[];
extern const hui_html_tag_entry hui_html_tag_entries[];
extern const size_t hui_html_tag_count;

#ifdef __cplusplus
}
#endif

/* Keep alphabetical; source: https://github.com/wooorm/html-tag-names (2025-10-21). */
#define HUI_HTML_TAG_LIST(M) \
    M(a, A) \
    M(abbr, ABBR) \
    M(acronym, ACRONYM) \
    M(address, ADDRESS) \
    M(applet, APPLET) \
    M(area, AREA) \
    M(article, ARTICLE) \
    M(aside, ASIDE) \
    M(audio, AUDIO) \
    M(b, B) \
    M(base, BASE) \
    M(basefont, BASEFONT) \
    M(bdi, BDI) \
    M(bdo, BDO) \
    M(bgsound, BGSOUND) \
    M(big, BIG) \
    M(blink, BLINK) \
    M(blockquote, BLOCKQUOTE) \
    M(body, BODY) \
    M(br, BR) \
    M(button, BUTTON) \
    M(canvas, CANVAS) \
    M(caption, CAPTION) \
    M(center, CENTER) \
    M(cite, CITE) \
    M(code, CODE) \
    M(col, COL) \
    M(colgroup, COLGROUP) \
    M(command, COMMAND) \
    M(content, CONTENT) \
    M(data, DATA) \
    M(datalist, DATALIST) \
    M(dd, DD) \
    M(del, DEL) \
    M(details, DETAILS) \
    M(dfn, DFN) \
    M(dialog, DIALOG) \
    M(dir, DIR) \
    M(div, DIV) \
    M(dl, DL) \
    M(dt, DT) \
    M(element, ELEMENT) \
    M(em, EM) \
    M(embed, EMBED) \
    M(fieldset, FIELDSET) \
    M(figcaption, FIGCAPTION) \
    M(figure, FIGURE) \
    M(font, FONT) \
    M(footer, FOOTER) \
    M(form, FORM) \
    M(frame, FRAME) \
    M(frameset, FRAMESET) \
    M(h1, H1) \
    M(h2, H2) \
    M(h3, H3) \
    M(h4, H4) \
    M(h5, H5) \
    M(h6, H6) \
    M(head, HEAD) \
    M(header, HEADER) \
    M(hgroup, HGROUP) \
    M(hr, HR) \
    M(html, HTML) \
    M(i, I) \
    M(iframe, IFRAME) \
    M(image, IMAGE) \
    M(img, IMG) \
    M(input, INPUT) \
    M(ins, INS) \
    M(isindex, ISINDEX) \
    M(kbd, KBD) \
    M(keygen, KEYGEN) \
    M(label, LABEL) \
    M(legend, LEGEND) \
    M(li, LI) \
    M(link, LINK) \
    M(listing, LISTING) \
    M(main, MAIN) \
    M(map, MAP) \
    M(mark, MARK) \
    M(marquee, MARQUEE) \
    M(math, MATH) \
    M(menu, MENU) \
    M(menuitem, MENUITEM) \
    M(meta, META) \
    M(meter, METER) \
    M(multicol, MULTICOL) \
    M(nav, NAV) \
    M(nextid, NEXTID) \
    M(nobr, NOBR) \
    M(noembed, NOEMBED) \
    M(noframes, NOFRAMES) \
    M(noscript, NOSCRIPT) \
    M(object, OBJECT) \
    M(ol, OL) \
    M(optgroup, OPTGROUP) \
    M(option, OPTION) \
    M(output, OUTPUT) \
    M(p, P) \
    M(param, PARAM) \
    M(picture, PICTURE) \
    M(plaintext, PLAINTEXT) \
    M(pre, PRE) \
    M(progress, PROGRESS) \
    M(q, Q) \
    M(rb, RB) \
    M(rbc, RBC) \
    M(rp, RP) \
    M(rt, RT) \
    M(rtc, RTC) \
    M(ruby, RUBY) \
    M(s, S) \
    M(samp, SAMP) \
    M(script, SCRIPT) \
    M(search, SEARCH) \
    M(section, SECTION) \
    M(select, SELECT) \
    M(shadow, SHADOW) \
    M(slot, SLOT) \
    M(small, SMALL) \
    M(source, SOURCE) \
    M(spacer, SPACER) \
    M(span, SPAN) \
    M(strike, STRIKE) \
    M(strong, STRONG) \
    M(style, STYLE) \
    M(sub, SUB) \
    M(summary, SUMMARY) \
    M(sup, SUP) \
    M(svg, SVG) \
    M(table, TABLE) \
    M(tbody, TBODY) \
    M(td, TD) \
    M(template, TEMPLATE) \
    M(textarea, TEXTAREA) \
    M(tfoot, TFOOT) \
    M(th, TH) \
    M(thead, THEAD) \
    M(time, TIME) \
    M(title, TITLE) \
    M(tr, TR) \
    M(track, TRACK) \
    M(tt, TT) \
    M(u, U) \
    M(ul, UL) \
    M(var, VAR) \
    M(video, VIDEO) \
    M(wbr, WBR) \
    M(xmp, XMP)

#ifdef __cplusplus
#define HUI_HTML_DEFINE_TAG(name, upper) constexpr const char *const HUI_HTML_TAG_##upper = #name;
#else
#define HUI_HTML_DEFINE_TAG(name, upper) static const char HUI_HTML_TAG_##upper[] = #name;
#endif

HUI_HTML_TAG_LIST(HUI_HTML_DEFINE_TAG)

#undef HUI_HTML_DEFINE_TAG

#endif /* HUI_HUI_HTML_TAGS_H */
