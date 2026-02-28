#pragma once
#include <GLES2/gl2.h>
#include <stdint.h>

/* Initialise font system. Loads TTF from path and bakes a GL texture atlas.
   Returns 0 on success. */
int  font_init(const char *ttf_path);

/* Draw a UTF-8 string at pixel position (x, y) — top-left origin.
   size: approximate pixel height (e.g. 24, 32, 48).
   color: 0xAARRGGBB. */
void font_draw(int x, int y, const char *str, float size, uint32_t color);

/* Measure width of a string in pixels at given size. */
float font_measure(const char *str, float size);

void font_destroy(void);
