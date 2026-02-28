#include "render.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int g_screen_w = 1920;
int g_screen_h = 1080;

/* ── Shader sources (GLSL ES 1.00) ──────────────────────────────────────── */

static const char *VERT_SRC =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying   vec2 v_uv;\n"
    "uniform   vec2 u_screen;\n"   /* (width, height) */
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    vec2 ndc = a_pos / u_screen * 2.0 - 1.0;\n"
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

static RectProg g_rect;
static TexProg  g_tex;
GLuint   g_vbo;

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

    /* Shared VBO (quad: pos xy + uv xy, 4 vertices) */
    glGenBuffers(1, &g_vbo);

    return 0;
}

void render_begin_frame(void) {
    uint8_t r = (COL_BG >> 16) & 0xff;
    uint8_t g = (COL_BG >>  8) & 0xff;
    uint8_t b = (COL_BG      ) & 0xff;
    glClearColor(r/255.0f, g/255.0f, b/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void render_end_frame(void) {
    /* Nothing — caller calls egl_swap() which does eglSwapBuffers */
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
    float a = ((color >> 24) & 0xff) / 255.0f;
    float r = ((color >> 16) & 0xff) / 255.0f;
    float g = ((color >>  8) & 0xff) / 255.0f;
    float b = ((color      ) & 0xff) / 255.0f;

    upload_quad(x, y, w, h);
    glUseProgram(g_rect.prog);
    glUniform2f(g_rect.u_screen, (float)g_screen_w, (float)g_screen_h);
    glUniform4f(g_rect.u_color, r, g, b, a);
    bind_quad_attribs(g_rect.a_pos, g_rect.a_uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(g_rect.a_pos);
    glDisableVertexAttribArray(g_rect.a_uv);
}

void render_rect_outline(int x, int y, int w, int h, uint32_t color, int border) {
    render_rect(x,          y,          w,      border, color);
    render_rect(x,          y+h-border, w,      border, color);
    render_rect(x,          y,          border, h,      color);
    render_rect(x+w-border, y,          border, h,      color);
}

void render_texture(int x, int y, int w, int h, GLuint tex, float alpha) {
    upload_quad(x, y, w, h);
    glUseProgram(g_tex.prog);
    glUniform2f(g_tex.u_screen, (float)g_screen_w, (float)g_screen_h);
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
