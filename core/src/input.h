#pragma once
#include <stddef.h>

/* Key name strings returned by input_next_key() */
#define KEY_UP     "up"
#define KEY_DOWN   "down"
#define KEY_LEFT   "left"
#define KEY_RIGHT  "right"
#define KEY_OK     "ok"
#define KEY_BACK   "back"
#define KEY_HOME   "home"
#define KEY_PLAY   "play"
#define KEY_PAUSE  "pause"
#define KEY_VOLUP  "vol_up"
#define KEY_VOLDOWN "vol_down"

/* Init libinput with udev backend.
   Returns 0 on success. */
int  input_init(void);

/* Return the libinput fd — add to epoll with EPOLLIN. */
int  input_fd(void);

/* Drain libinput events. For each keyboard key-press, appends
   a key-name string (one of the KEY_* macros above) to the
   caller-supplied queue.
   queue:   caller-allocated array of char pointers
   max:     size of queue
   Returns number of keys written. */
int  input_dispatch(const char **queue, int max);

void input_destroy(void);
