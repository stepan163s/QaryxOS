#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "font.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Shared atlas: 2048×2048 holds ASCII + Cyrillic strips for each baked size.
 * ASCII  : U+0020–U+007F  (96 glyphs)
 * Cyrillic: U+0400–U+045F  (96 glyphs — covers full Russian alphabet incl. Ё/ё) */

#define ATLAS_W         2048
#define ATLAS_H         2048

#define ASCII_FIRST      32
#define ASCII_NUM        96    /* U+0020–U+007F */

#define CYR_FIRST       0x0400
#define CYR_NUM          96    /* U+0400–U+045F */

typedef struct {
    float            scale;
    stbtt_bakedchar  ascii[ASCII_NUM];
    int              ascii_y;   /* y offset of ASCII strip in atlas */
    stbtt_bakedchar  cyr[CYR_NUM];
    int              cyr_y;     /* y offset of Cyrillic strip in atlas */
} BakedFont;

#define NUM_SIZES 2
static float      g_sizes[NUM_SIZES] = { 22.0f, 38.0f };
static BakedFont  g_fonts[NUM_SIZES];
static GLuint     g_atlas_tex;
static uint8_t    g_atlas_buf[ATLAS_W * ATLAS_H];
static int        g_atlas_cursor_y = 0;
static int        g_ready = 0;

static uint8_t *g_ttf_data = NULL;

/* ── Atlas baking ────────────────────────────────────────────────────────── */

/* Bake a contiguous Unicode range into the shared atlas.
 * Allocates as many rows as needed so all num_glyphs fit.
 * Returns 0 on success, -1 if atlas is full. */
static int bake_range(stbtt_bakedchar *out, int first_cp, int num_glyphs,
                      float px_height, int *out_y) {
    stbtt_fontinfo fi;
    stbtt_InitFont(&fi, g_ttf_data, 0);

    float scale = stbtt_ScaleForPixelHeight(&fi, px_height);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&fi, &ascent, &descent, &line_gap);
    int row_h = (int)((ascent - descent + line_gap) * scale) + 4;

    /* Estimate chars per row conservatively (0.6× px_height average glyph width) */
    int chars_per_row = (int)((float)ATLAS_W / (px_height * 0.6f));
    if (chars_per_row < 1) chars_per_row = 1;
    int num_rows = (num_glyphs + chars_per_row - 1) / chars_per_row;
    if (num_rows < 1) num_rows = 1;
    int strip_h = row_h * num_rows;

    if (g_atlas_cursor_y + strip_h > ATLAS_H) {
        fprintf(stderr, "font: atlas full at y=%d\n", g_atlas_cursor_y);
        return -1;
    }

    uint8_t *strip = g_atlas_buf + g_atlas_cursor_y * ATLAS_W;
    int ret = stbtt_BakeFontBitmap(g_ttf_data, 0, px_height,
                                    strip, ATLAS_W, strip_h,
                                    first_cp, num_glyphs, out);
    /* ret < 0 means some glyphs didn't fit — log but don't abort */
    if (ret < 0)
        fprintf(stderr, "font: BakeFontBitmap cp=0x%04x num=%d ret=%d\n",
                first_cp, num_glyphs, ret);

    *out_y = g_atlas_cursor_y;
    g_atlas_cursor_y += strip_h;
    return 0;
}

int font_init(const char *ttf_path) {
    FILE *f = fopen(ttf_path, "rb");
    if (!f) { fprintf(stderr, "font: cannot open %s\n", ttf_path); return -1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    g_ttf_data = malloc(sz);
    if (!g_ttf_data) { fclose(f); return -1; }
    fread(g_ttf_data, 1, sz, f);
    fclose(f);

    memset(g_atlas_buf, 0, sizeof(g_atlas_buf));

    for (int i = 0; i < NUM_SIZES; i++) {
        g_fonts[i].scale = g_sizes[i];
        if (bake_range(g_fonts[i].ascii, ASCII_FIRST, ASCII_NUM,
                       g_sizes[i], &g_fonts[i].ascii_y) < 0) return -1;
        if (bake_range(g_fonts[i].cyr,   CYR_FIRST,   CYR_NUM,
                       g_sizes[i], &g_fonts[i].cyr_y)   < 0) return -1;
    }

    glGenTextures(1, &g_atlas_tex);
    glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, ATLAS_W, ATLAS_H, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, g_atlas_buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_ready = 1;
    fprintf(stderr, "font: atlas %dx%d, %d sizes × 2 ranges (ASCII+Cyrillic) baked\n",
            ATLAS_W, ATLAS_H, NUM_SIZES);
    return 0;
}

/* ── UTF-8 decoder ───────────────────────────────────────────────────────── */

/* Decode one Unicode codepoint from *p, advance *p past the sequence.
 * Returns the codepoint, or -1 on invalid/continuation byte. */
static int utf8_next(const char **p) {
    unsigned char c = (unsigned char)**p;
    if (!c) return 0;
    if (c < 0x80) { (*p)++; return c; }
    if (c < 0xC0) { (*p)++; return -1; }  /* stray continuation byte */
    if (c < 0xE0) {
        unsigned char b = (unsigned char)(*p)[1];
        if ((b & 0xC0) != 0x80) { (*p)++; return -1; }
        *p += 2;
        return ((c & 0x1F) << 6) | (b & 0x3F);
    }
    if (c < 0xF0) {
        unsigned char b1 = (unsigned char)(*p)[1];
        unsigned char b2 = (unsigned char)(*p)[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) { (*p)++; return -1; }
        *p += 3;
        return ((c & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
    }
    *p += 4; return -1;  /* 4-byte (emoji etc.) — skip */
}

/* ── Size selection ──────────────────────────────────────────────────────── */

static int best_size(float size) {
    int best = 0;
    float best_d = fabsf(size - g_sizes[0]);
    for (int i = 1; i < NUM_SIZES; i++) {
        float d = fabsf(size - g_sizes[i]);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

/* ── Glyph rendering helper ──────────────────────────────────────────────── */

static float render_bc(const stbtt_bakedchar *bc, int atlas_y,
                        float cx, float cy, float scale,
                        float cr, float cg, float cb, float ca) {
    float gx = cx + bc->xoff * scale;
    float gy = cy + bc->yoff * scale;
    float gw = (bc->x1 - bc->x0) * scale;
    float gh = (bc->y1 - bc->y0) * scale;
    float u0 = (float)bc->x0 / ATLAS_W;
    float v0 = (float)(atlas_y + bc->y0) / ATLAS_H;
    float u1 = (float)bc->x1 / ATLAS_W;
    float v1 = (float)(atlas_y + bc->y1) / ATLAS_H;
    render_glyph((int)gx, (int)gy, (int)(gw + 0.5f), (int)(gh + 0.5f),
                 g_atlas_tex, u0, v0, u1, v1, cr, cg, cb, ca);
    return bc->xadvance * scale;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void font_draw(int x, int y, const char *str, float size, uint32_t color) {
    if (!g_ready || !str || !*str) return;

    float cr = ((color >> 16) & 0xff) / 255.0f;
    float cg = ((color >>  8) & 0xff) / 255.0f;
    float cb = ((color      ) & 0xff) / 255.0f;
    float ca = ((color >> 24) & 0xff) / 255.0f;

    int idx = best_size(size);
    BakedFont *bf  = &g_fonts[idx];
    float scale    = size / bf->scale;
    float cx       = (float)x;
    float cy       = (float)y + bf->scale * scale;  /* baseline */

    const char *p = str;
    while (*p) {
        int cp = utf8_next(&p);
        if (cp <= 0) continue;

        if (cp >= ASCII_FIRST && cp < ASCII_FIRST + ASCII_NUM) {
            cx += render_bc(&bf->ascii[cp - ASCII_FIRST], bf->ascii_y,
                            cx, cy, scale, cr, cg, cb, ca);
        } else if (cp >= CYR_FIRST && cp < CYR_FIRST + CYR_NUM) {
            cx += render_bc(&bf->cyr[cp - CYR_FIRST], bf->cyr_y,
                            cx, cy, scale, cr, cg, cb, ca);
        } else {
            cx += size * 0.5f;  /* unsupported: leave a gap */
        }
    }
}

float font_measure(const char *str, float size) {
    if (!g_ready || !str) return 0;
    int idx = best_size(size);
    BakedFont *bf = &g_fonts[idx];
    float scale   = size / bf->scale;
    float cx      = 0;

    const char *p = str;
    while (*p) {
        int cp = utf8_next(&p);
        if (cp <= 0) continue;

        if (cp >= ASCII_FIRST && cp < ASCII_FIRST + ASCII_NUM)
            cx += bf->ascii[cp - ASCII_FIRST].xadvance * scale;
        else if (cp >= CYR_FIRST && cp < CYR_FIRST + CYR_NUM)
            cx += bf->cyr[cp - CYR_FIRST].xadvance * scale;
        else
            cx += size * 0.5f;
    }
    return cx;
}

void font_destroy(void) {
    if (g_atlas_tex) { glDeleteTextures(1, &g_atlas_tex); g_atlas_tex = 0; }
    free(g_ttf_data); g_ttf_data = NULL;
    g_ready = 0;
}
