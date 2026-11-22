/* A small pause menu of our own (pause.c): Resume, Save state, Load state, Rewind/
 * fast-forward on or off, Cheats (a Back-able sub-list: Infinite health, Infinite
 * lives, Jump to level), Quit. Not Streets of Rage's tabbed menu (outfits, fighter
 * select) -- just these few things, over a dimmed, crisp (no blur) copy of the
 * paused frame, so the game stays visible and in its own pixel-art look behind the
 * panel. */
#ifndef PAUSE_H
#define PAUSE_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PAUSE_NONE,
    PAUSE_CLOSE,                        /* resume */
    PAUSE_LOADED,                       /* a save state was loaded: resume */
    PAUSE_LEVEL_JUMP,                   /* Jump to level: main.c resets the machine into it */
    PAUSE_QUIT,
} PauseAction;

/* Opens the menu over the paused frame fb (fb_w x fb_h, pitch in pixels), which the
 * window shows in the rectangle game_x/y/w/h of an out_w x out_h output.
 * window_to_pixels converts mouse coordinates to output pixels. */
void pause_open(const uint32_t *fb, int fb_w, int fb_h, int fb_pitch, int game_x, int game_y, int game_w, int game_h,
                 int out_w, int out_h, float window_to_pixels);
void pause_close(void);
bool pause_is_open(void);
PauseAction pause_event(const SDL_Event *e);
void pause_render(double time_s);       /* into ui_px (ui_w x ui_h) */
/* Called for the menu's sounds: 0 move, 1 confirm, 2 back. */
extern void (*pause_sound)(int kind);

#endif
