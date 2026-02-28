#pragma once
#include <GLES2/gl2.h>
#include <stdint.h>

/* Screen dimensions set at init */
extern int g_screen_w;
extern int g_screen_h;

/* Initialise shader programs.
   Must be called after EGL context is current. */
int  render_init(int screen_w, int screen_h);

/* ── Per-frame lifecycle ───────────────────────────────────────────────────── */

/* Clear the framebuffer with the background colour. */
void render_begin_frame(void);

/* Flush — called by egl_swap internally; you don't need to call this. */
void render_end_frame(void);

/* ── Primitives ────────────────────────────────────────────────────────────── */

/* Fill a rectangle. color = 0xAARRGGBB. */
void render_rect(int x, int y, int w, int h, uint32_t color);

/* Draw a rounded-corner rectangle outline. */
void render_rect_outline(int x, int y, int w, int h, uint32_t color, int border);

/* Draw a GL texture quad with alpha blending.
   tex must be a RGBA GL_TEXTURE_2D. */
void render_texture(int x, int y, int w, int h, GLuint tex, float alpha);

/* ── Colour helpers ────────────────────────────────────────────────────────── */
static inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* Common palette */
#define COL_BG      rgba( 10,  10,  10, 255)
#define COL_TILE    rgba( 28,  28,  28, 255)
#define COL_TILE_HL rgba( 55,  55,  80, 255)
#define COL_ACCENT  rgba(100, 120, 220, 255)
#define COL_WHITE   rgba(255, 255, 255, 255)
#define COL_GRAY    rgba(140, 140, 140, 255)
