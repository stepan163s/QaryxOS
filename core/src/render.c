#include "render.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int g_screen_w = 1920;
int g_screen_h = 1080;

/* ── Shader sources (GLSL ES 1.00) ──────────────────────────────────────── */

/* u_screen now holds the precomputed NDC scale: vec2(2.0/w, 2.0/h).
 * Replacing the per-vertex division  (a_pos / screen_size * 2.0 - 1.0)
 * with a multiplication (a_pos * u_screen - 1.0) saves one GPU fdiv per
 * vertex component.  On Mali-G52, fdiv ≈ 20 cycles vs fmul ≈ 2 cycles.
 * With ~5100 quads/frame × 4 verts × 2 components = ~41 000 divisions
 * eliminated per frame. */
static const char *VERT_SRC =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying   vec2 v_uv;\n"
    "uniform   vec2 u_screen;\n"   /* precomputed: vec2(2.0/w, 2.0/h) */
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    vec2 ndc = a_pos * u_screen - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "}\n";

static const char *FRAG_RECT_SRC =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "void main() { gl_FragColor = u_color; }\n";

static const char *FRAG_TEX_SRC =
    "precision mediump float;\n"
    "varying   vec2      v_uv;\n"
    "uniform   sampler2D u_tex;\n"
    "uniform   float     u_alpha;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, v_uv);\n"
    "    gl_FragColor = vec4(c.rgb, c.a * u_alpha);\n"
    "}\n";

/* Glyph shader: single-channel atlas texture used as coverage mask.
   Works with both GL_ALPHA (ES 2.0) and GL_R8/GL_RED (ES 3.0) textures —
   on GL_ALPHA the coverage is in .a; on GL_R8 it is in .r.
   We sample both and take whichever is non-zero. */
static const char *FRAG_GLYPH_SRC =
    "precision mediump float;\n"
    "varying   vec2      v_uv;\n"
    "uniform   sampler2D u_tex;\n"
    "uniform   vec4      u_color;\n"
    "void main() {\n"
    "    vec4 s = texture2D(u_tex, v_uv);\n"
    "    float coverage = max(s.a, s.r);\n"
    "    gl_FragColor = vec4(u_color.rgb, u_color.a * coverage);\n"
    "}\n";

/* ── Batch shaders (colour as per-vertex attribute) ──────────────────────── */
/* Rect and glyph draws accumulate into one large vertex buffer, flushed once *
 * per shader-type-run — eliminates per-draw glBufferData + glUseProgram.    */
static const char *VERT_BATCH_SRC =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_color;\n"
    "varying   vec2 v_uv;\n"
    "varying   vec4 v_color;\n"
    "uniform   vec2 u_screen;\n"
    "void main() {\n"
    "    v_uv    = a_uv;\n"
    "    v_color = a_color;\n"
    "    vec2 ndc = a_pos * u_screen - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "}\n";

static const char *FRAG_RECT_BATCH_SRC =
    "precision mediump float;\n"
    "varying vec4 v_color;\n"
    "void main() { gl_FragColor = v_color; }\n";

static const char *FRAG_GLYPH_BATCH_SRC =
    "precision mediump float;\n"
    "varying vec2      v_uv;\n"
    "varying vec4      v_color;\n"
    "uniform sampler2D u_tex;\n"
    "void main() {\n"
    "    vec4 s = texture2D(u_tex, v_uv);\n"
    "    float cov = max(s.a, s.r);\n"
    "    gl_FragColor = vec4(v_color.rgb, v_color.a * cov);\n"
    "}\n";

/* ── Internal state ─────────────────────────────────────────────────────── */

typedef struct {
    GLuint prog;
    GLint  a_pos, a_uv;
    GLint  u_screen, u_color;
} RectProg;

typedef struct {
    GLuint prog;
    GLint  a_pos, a_uv;
    GLint  u_screen, u_tex, u_alpha;
} TexProg;

typedef struct {
    GLuint prog;
    GLint  a_pos, a_uv;
    GLint  u_screen, u_tex, u_color;
} GlyphProg;

static RectProg  g_rect;
static TexProg   g_tex;
static GlyphProg g_glyph;
GLuint   g_vbo;

/* ── Batch programs ──────────────────────────────────────────────────────── */
typedef struct {
    GLuint prog;
    GLint  a_pos, a_uv, a_color;
    GLint  u_screen;
} BatchRectProg;

typedef struct {
    GLuint prog;
    GLint  a_pos, a_uv, a_color;
    GLint  u_screen, u_tex;
} BatchGlyphProg;

static BatchRectProg  g_brc;
static BatchGlyphProg g_bgl;

/* ── Batch state ─────────────────────────────────────────────────────────── */
/* Vertex layout: x, y, u, v, r, g, b, a  (8 floats = 32 bytes per vertex). *
 * Each quad = 6 verts (GL_TRIANGLES): TL-TR-BL + TR-BR-BL.                 *
 * Flush-on-type-switch preserves draw-call ordering (rects under text, etc).*
 * B_MAX_QUADS=4096: 4096 × 6 × 8 × 4 = 786 KB CPU buffer (BSS).           */
#define B_MAX_QUADS  4096
#define B_FPV        8                          /* floats per vertex */
#define B_VPQ        6                          /* vertices per quad */
#define B_STRIDE     (B_FPV * (int)sizeof(float))

static float  g_b_buf[B_MAX_QUADS * B_VPQ * B_FPV];
static int    g_b_n     = 0;    /* quads accumulated this frame */
static int    g_b_type  = 0;    /* 0=none, 1=rect, 2=glyph */
static GLuint g_b_atlas = 0;    /* current glyph atlas (flush if it changes) */
static GLuint g_b_vbo;          /* pre-allocated VBO for batch uploads */

/* ── Helpers ────────────────────────────────────────────────────────────── */

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(s, sizeof(buf), NULL, buf);
        fprintf(stderr, "render: shader compile error: %s\n", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(const char *vert_src, const char *frag_src) {
    GLuint vert = compile_shader(GL_VERTEX_SHADER,   vert_src);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    if (!vert || !frag) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetProgramInfoLog(prog, sizeof(buf), NULL, buf);
        fprintf(stderr, "render: program link error: %s\n", buf);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int render_init(int sw, int sh) {
    g_screen_w = sw;
    g_screen_h = sh;

    glViewport(0, 0, sw, sh);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Rect program */
    g_rect.prog = link_program(VERT_SRC, FRAG_RECT_SRC);
    if (!g_rect.prog) return -1;
    g_rect.a_pos   = glGetAttribLocation (g_rect.prog, "a_pos");
    g_rect.a_uv    = glGetAttribLocation (g_rect.prog, "a_uv");
    g_rect.u_screen= glGetUniformLocation(g_rect.prog, "u_screen");
    g_rect.u_color = glGetUniformLocation(g_rect.prog, "u_color");

    /* Texture program */
    g_tex.prog = link_program(VERT_SRC, FRAG_TEX_SRC);
    if (!g_tex.prog) return -1;
    g_tex.a_pos   = glGetAttribLocation (g_tex.prog, "a_pos");
    g_tex.a_uv    = glGetAttribLocation (g_tex.prog, "a_uv");
    g_tex.u_screen= glGetUniformLocation(g_tex.prog, "u_screen");
    g_tex.u_tex   = glGetUniformLocation(g_tex.prog, "u_tex");
    g_tex.u_alpha = glGetUniformLocation(g_tex.prog, "u_alpha");

    /* Glyph program (font atlas, single-channel, coloured) */
    g_glyph.prog = link_program(VERT_SRC, FRAG_GLYPH_SRC);
    if (!g_glyph.prog) return -1;
    g_glyph.a_pos   = glGetAttribLocation (g_glyph.prog, "a_pos");
    g_glyph.a_uv    = glGetAttribLocation (g_glyph.prog, "a_uv");
    g_glyph.u_screen= glGetUniformLocation(g_glyph.prog, "u_screen");
    g_glyph.u_tex   = glGetUniformLocation(g_glyph.prog, "u_tex");
    g_glyph.u_color = glGetUniformLocation(g_glyph.prog, "u_color");

    /* Shared VBO (quad: pos xy + uv xy, 4 vertices) — used for textures */
    glGenBuffers(1, &g_vbo);

    /* Batch rect program */
    g_brc.prog = link_program(VERT_BATCH_SRC, FRAG_RECT_BATCH_SRC);
    if (!g_brc.prog) return -1;
    g_brc.a_pos   = glGetAttribLocation (g_brc.prog, "a_pos");
    g_brc.a_uv    = glGetAttribLocation (g_brc.prog, "a_uv");
    g_brc.a_color = glGetAttribLocation (g_brc.prog, "a_color");
    g_brc.u_screen= glGetUniformLocation(g_brc.prog, "u_screen");

    /* Batch glyph program */
    g_bgl.prog = link_program(VERT_BATCH_SRC, FRAG_GLYPH_BATCH_SRC);
    if (!g_bgl.prog) return -1;
    g_bgl.a_pos   = glGetAttribLocation (g_bgl.prog, "a_pos");
    g_bgl.a_uv    = glGetAttribLocation (g_bgl.prog, "a_uv");
    g_bgl.a_color = glGetAttribLocation (g_bgl.prog, "a_color");
    g_bgl.u_screen= glGetUniformLocation(g_bgl.prog, "u_screen");
    g_bgl.u_tex   = glGetUniformLocation(g_bgl.prog, "u_tex");

    /* Large VBO for batch uploads (pre-allocated, re-used every frame) */
    glGenBuffers(1, &g_b_vbo);

    return 0;
}

void render_begin_frame(void) {
    /* Reset batch state for the new frame */
    g_b_n     = 0;
    g_b_type  = 0;
    g_b_atlas = 0;

    /* Restore GL state that libmpv may have changed */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_screen_w, g_screen_h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Upload NDC scale once per frame to every program.
     * Value is 2.0/size so the vertex shader can multiply instead of divide
     * (u_screen now means vec2(2/w, 2/h), not vec2(w, h)). */
    float sw = 2.0f / (float)g_screen_w, sh = 2.0f / (float)g_screen_h;
    glUseProgram(g_rect.prog);  glUniform2f(g_rect.u_screen,  sw, sh);
    glUseProgram(g_tex.prog);   glUniform2f(g_tex.u_screen,   sw, sh);
    glUseProgram(g_glyph.prog); glUniform2f(g_glyph.u_screen, sw, sh);
    glUseProgram(g_brc.prog);   glUniform2f(g_brc.u_screen,   sw, sh);
    glUseProgram(g_bgl.prog);   glUniform2f(g_bgl.u_screen,   sw, sh);
    glUseProgram(0);

    uint8_t r = (COL_BG >> 16) & 0xff;
    uint8_t g = (COL_BG >>  8) & 0xff;
    uint8_t b = (COL_BG      ) & 0xff;
    glClearColor(r/255.0f, g/255.0f, b/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void render_batch_flush(void);   /* forward declaration */

void render_end_frame(void) {
    render_batch_flush();   /* flush any pending rect or glyph quads */
}

/* ── Batch accumulator ───────────────────────────────────────────────────── */

static void batch_push_quad(float x1, float y1, float x2, float y2,
                             float u0, float v0, float u1, float v1,
                             float r,  float g,  float b,  float a) {
    float *p = g_b_buf + g_b_n * (B_VPQ * B_FPV);
    /* TL */ p[ 0]=x1; p[ 1]=y1; p[ 2]=u0; p[ 3]=v0; p[ 4]=r; p[ 5]=g; p[ 6]=b; p[ 7]=a;
    /* TR */ p[ 8]=x2; p[ 9]=y1; p[10]=u1; p[11]=v0; p[12]=r; p[13]=g; p[14]=b; p[15]=a;
    /* BL */ p[16]=x1; p[17]=y2; p[18]=u0; p[19]=v1; p[20]=r; p[21]=g; p[22]=b; p[23]=a;
    /* TR */ p[24]=x2; p[25]=y1; p[26]=u1; p[27]=v0; p[28]=r; p[29]=g; p[30]=b; p[31]=a;
    /* BR */ p[32]=x2; p[33]=y2; p[34]=u1; p[35]=v1; p[36]=r; p[37]=g; p[38]=b; p[39]=a;
    /* BL */ p[40]=x1; p[41]=y2; p[42]=u0; p[43]=v1; p[44]=r; p[45]=g; p[46]=b; p[47]=a;
    g_b_n++;
}

static void render_batch_flush(void) {
    if (!g_b_n) { g_b_type = 0; return; }

    glBindBuffer(GL_ARRAY_BUFFER, g_b_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 g_b_n * B_VPQ * B_FPV * sizeof(float),
                 g_b_buf, GL_STREAM_DRAW);

    if (g_b_type == 1) {                            /* rect batch */
        glUseProgram(g_brc.prog);
        glEnableVertexAttribArray(g_brc.a_pos);
        glVertexAttribPointer(g_brc.a_pos,   2, GL_FLOAT, GL_FALSE, B_STRIDE, (void*)0);
        glEnableVertexAttribArray(g_brc.a_uv);
        glVertexAttribPointer(g_brc.a_uv,    2, GL_FLOAT, GL_FALSE, B_STRIDE, (void*)(2*sizeof(float)));
        glEnableVertexAttribArray(g_brc.a_color);
        glVertexAttribPointer(g_brc.a_color, 4, GL_FLOAT, GL_FALSE, B_STRIDE, (void*)(4*sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, g_b_n * B_VPQ);
        glDisableVertexAttribArray(g_brc.a_pos);
        glDisableVertexAttribArray(g_brc.a_uv);
        glDisableVertexAttribArray(g_brc.a_color);
    } else {                                        /* glyph batch */
        glUseProgram(g_bgl.prog);
        glUniform1i(g_bgl.u_tex, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_b_atlas);
        glEnableVertexAttribArray(g_bgl.a_pos);
        glVertexAttribPointer(g_bgl.a_pos,   2, GL_FLOAT, GL_FALSE, B_STRIDE, (void*)0);
        glEnableVertexAttribArray(g_bgl.a_uv);
        glVertexAttribPointer(g_bgl.a_uv,    2, GL_FLOAT, GL_FALSE, B_STRIDE, (void*)(2*sizeof(float)));
        glEnableVertexAttribArray(g_bgl.a_color);
        glVertexAttribPointer(g_bgl.a_color, 4, GL_FLOAT, GL_FALSE, B_STRIDE, (void*)(4*sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, g_b_n * B_VPQ);
        glDisableVertexAttribArray(g_bgl.a_pos);
        glDisableVertexAttribArray(g_bgl.a_uv);
        glDisableVertexAttribArray(g_bgl.a_color);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    g_b_n    = 0;
    g_b_type = 0;
}

static void upload_quad(int x, int y, int w, int h) {
    float x1 = x, y1 = y, x2 = x+w, y2 = y+h;
    float verts[] = {
        /* pos         uv   */
        x1, y1,   0.0f, 0.0f,
        x2, y1,   1.0f, 0.0f,
        x1, y2,   0.0f, 1.0f,
        x2, y2,   1.0f, 1.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
}

static void bind_quad_attribs(GLint a_pos, GLint a_uv) {
    glEnableVertexAttribArray(a_pos);
    glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(a_uv);
    glVertexAttribPointer(a_uv, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
}

void render_rect(int x, int y, int w, int h, uint32_t color) {
    if (g_b_type == 2) render_batch_flush();    /* flush glyph run first */
    if (g_b_n >= B_MAX_QUADS) render_batch_flush();
    float a = ((color >> 24) & 0xff) / 255.0f;
    float r = ((color >> 16) & 0xff) / 255.0f;
    float g = ((color >>  8) & 0xff) / 255.0f;
    float b = ((color      ) & 0xff) / 255.0f;
    g_b_type = 1;
    batch_push_quad((float)x, (float)y, (float)(x+w), (float)(y+h),
                    0.0f, 0.0f, 0.0f, 0.0f, r, g, b, a);
}

void render_rect_outline(int x, int y, int w, int h, uint32_t color, int border) {
    render_rect(x,          y,          w,      border, color);
    render_rect(x,          y+h-border, w,      border, color);
    render_rect(x,          y,          border, h,      color);
    render_rect(x+w-border, y,          border, h,      color);
}

void render_texture(int x, int y, int w, int h, GLuint tex, float alpha) {
    render_batch_flush();   /* flush pending rects/glyphs to preserve z-order */
    upload_quad(x, y, w, h);
    glUseProgram(g_tex.prog);
    glUniform1i(g_tex.u_tex, 0);
    glUniform1f(g_tex.u_alpha, alpha);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    bind_quad_attribs(g_tex.a_pos, g_tex.a_uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(g_tex.a_pos);
    glDisableVertexAttribArray(g_tex.a_uv);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void upload_quad_uv(int x, int y, int w, int h,
                            float u0, float v0, float u1, float v1) {
    float x1 = x, y1 = y, x2 = x+w, y2 = y+h;
    float verts[] = {
        x1, y1,  u0, v0,
        x2, y1,  u1, v0,
        x1, y2,  u0, v1,
        x2, y2,  u1, v1,
    };
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
}

void render_glyph(int x, int y, int w, int h,
                  GLuint tex,
                  float u0, float v0, float u1, float v1,
                  float r,  float g,  float b,  float a) {
    if (g_b_type == 1) render_batch_flush();                  /* flush rect run */
    if (g_b_type == 2 && g_b_atlas != tex) render_batch_flush(); /* atlas change */
    if (g_b_n >= B_MAX_QUADS) render_batch_flush();
    g_b_type  = 2;
    g_b_atlas = tex;
    batch_push_quad((float)x, (float)y, (float)(x+w), (float)(y+h),
                    u0, v0, u1, v1, r, g, b, a);
}
