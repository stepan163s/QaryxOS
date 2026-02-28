#pragma once

typedef enum {
    SCREEN_HOME,
    SCREEN_YOUTUBE,
    SCREEN_IPTV,
    SCREEN_SETTINGS,
} Screen;

/* Current active screen — set by navigate() */
extern Screen g_screen;

/* Navigate to a named screen */
void navigate(const char *name);

/* Draw the home tile grid */
void ui_home_draw(void);

/* Handle a key event on the home screen */
void ui_home_key(const char *key);

/* Called when entering the home screen */
void ui_home_enter(void);
