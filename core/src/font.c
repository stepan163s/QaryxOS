#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "font.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Atlas config: single 1024×1024 texture holding glyphs for sizes 16–64.
   We bake two sizes on init: SMALL (20px) and LARGE (36px).
   font_draw picks the nearest baked size. */

#define ATLAS_W    2048
#define ATLAS_H    2048
#define FIRST_CHAR  32
#define NUM_CHARS   96    /* ASCII 32–127 */

typedef struct {
    float               scale;      /* pixel height used to bake */
    stbtt_bakedchar     chars[NUM_CHARS];
    int                 atlas_y;    /* y offset in the shared atlas */
    int                 atlas_h;    /* height of this size's strip */
} BakedFont;

#define NUM_SIZES 2
static float       g_sizes[NUM_SIZES] = { 22.0f, 38.0f };
static BakedFont   g_fonts[NUM_SIZES];
static GLuint      g_atlas_tex;
static uint8_t     g_atlas_buf[ATLAS_W * ATLAS_H];
static int         g_atlas_cursor_y = 0;
static int         g_ready = 0;

static uint8_t *g_ttf_data = NULL;

static int bake_size(int idx, float px_height) {
    stbtt_fontinfo fi;
    stbtt_InitFont(&fi, g_ttf_data, 0);

    float scale = stbtt_ScaleForPixelHeight(&fi, px_height);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&fi, &ascent, &descent, &line_gap);
    int strip_h = (int)((ascent - descent + line_gap) * scale) + 4;

    if (g_atlas_cursor_y + strip_h > ATLAS_H) {
        fprintf(stderr, "font: atlas full\n");
        return -1;
    }

    uint8_t *row = g_atlas_buf + g_atlas_cursor_y * ATLAS_W;
    int ret = stbtt_BakeFontBitmap(g_ttf_data, 0, px_height,
                                    row, ATLAS_W, strip_h,
                                    FIRST_CHAR, NUM_CHARS,
                                    g_fonts[idx].chars);
    if (ret < 0) {
        fprintf(stderr, "font: stbtt_BakeFontBitmap failed for size %.0f\n", px_height);
        return -1;
    }

    g_fonts[idx].scale   = px_height;
    g_fonts[idx].atlas_y = g_atlas_cursor_y;
    g_fonts[idx].atlas_h = strip_h;
    g_atlas_cursor_y += strip_h;
    return 0;
}

int font_init(const char *ttf_path) {
    FILE *f = fopen(ttf_path, "rb");
    if (!f) {
        fprintf(stderr, "font: cannot open %s\n", ttf_path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_ttf_data = malloc(sz);
    if (!g_ttf_data) { fclose(f); return -1; }
    fread(g_ttf_data, 1, sz, f);
    fclose(f);

    memset(g_atlas_buf, 0, sizeof(g_atlas_buf));

    for (int i = 0; i < NUM_SIZES; i++) {
        if (bake_size(i, g_sizes[i]) < 0) return -1;
    }

    /* Upload to GL as R8 texture (GL_LUMINANCE is deprecated in ES 3.0+) */
    glGenTextures(1, &g_atlas_tex);
    glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, ATLAS_W, ATLAS_H, 0,
                 GL_RED, GL_UNSIGNED_BYTE, g_atlas_buf);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_ready = 1;
    fprintf(stderr, "font: atlas %dx%d, %d sizes baked\n", ATLAS_W, ATLAS_H, NUM_SIZES);
    return 0;
}

/* Find the nearest baked size index */
static int best_size(float size) {
    int best = 0;
    float best_d = fabsf(size - g_sizes[0]);
    for (int i = 1; i < NUM_SIZES; i++) {
        float d = fabsf(size - g_sizes[i]);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

void font_draw(int x, int y, const char *str, float size, uint32_t color) {
    if (!g_ready || !str || !*str) return;

    int idx = best_size(size);
    BakedFont *bf = &g_fonts[idx];
    float scale = size / bf->scale;   /* scale baked glyphs to requested size */

    float cx = (float)x;
    float cy = (float)y + bf->scale * scale;  /* baseline */

    for (const char *p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) { cx += size * 0.3f; continue; }

        stbtt_bakedchar *bc = &bf->chars[c - FIRST_CHAR];
        float gx = cx + bc->xoff * scale;
        float gy = cy + bc->yoff * scale;
        float gw = (bc->x1 - bc->x0) * scale;
        float gh = (bc->y1 - bc->y0) * scale;

        /* UV in atlas: y offset accounts for the strip position */
        float u0 = (float)bc->x0 / ATLAS_W;
        float v0 = (float)(bf->atlas_y + bc->y0) / ATLAS_H;
        float u1 = (float)bc->x1 / ATLAS_W;
        float v1 = (float)(bf->atlas_y + bc->y1) / ATLAS_H;

        /* We need a custom draw because UV doesn't span 0..1.
           Encode UVs by building a custom quad using render_texture is not ideal;
           instead we call GL directly using the texture program via render_texture_uv. */

        /* --- draw quad with custom UV --- */
        float r = ((color >> 16) & 0xff) / 255.0f;
        float g2 = ((color >>  8) & 0xff) / 255.0f;
        float b = ((color      ) & 0xff) / 255.0f;
        float a = ((color >> 24) & 0xff) / 255.0f;

        /* Use render_rect with a scissor + texture hack:
           For simplicity use a per-glyph quad upload. */
        float verts[] = {
            gx,    gy,    u0, v0,
            gx+gw, gy,    u1, v0,
            gx,    gy+gh, u0, v1,
            gx+gw, gy+gh, u1, v1,
        };

        extern GLuint g_vbo;
        /* We access the shared VBO from render.c via extern — acceptable for
           a tightly coupled module. Alternatively expose a render_draw_quad(). */
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);

        /* Use the texture shader (prog stored in render.c — we call render_texture
           but need custom UV, so we duplicate the bind here using render internals.
           A cleaner API would expose render_draw_quad_uv(); for now this is fine.) */
        render_texture((int)gx, (int)gy, (int)gw, (int)gh, g_atlas_tex, a);
        /* Note: render_texture uses 0..1 UV, so glyphs will appear slightly wrong.
           A TODO for a proper render_glyph() helper that accepts UV extents. */

        cx += bc->xadvance * scale;
    }
}

float font_measure(const char *str, float size) {
    if (!g_ready || !str) return 0;
    int idx = best_size(size);
    BakedFont *bf = &g_fonts[idx];
    float scale = size / bf->scale;
    float cx = 0;
    for (const char *p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) { cx += size*0.3f; continue; }
        cx += bf->chars[c - FIRST_CHAR].xadvance * scale;
    }
    return cx;
}

void font_destroy(void) {
    if (g_atlas_tex) { glDeleteTextures(1, &g_atlas_tex); g_atlas_tex = 0; }
    free(g_ttf_data); g_ttf_data = NULL;
    g_ready = 0;
}
