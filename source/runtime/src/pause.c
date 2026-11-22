/* Pause menu: see pause.h. A comic-ink panel (black, a red rule, bold white type) over a
 * dimmed, unblurred copy of the paused frame, so the game stays sharp and visible behind
 * it, pixel art and all. */
#include "pause.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheats.h"
#include "input.h"
#include "savestate.h"
#include "settings.h"
#include "ui.h"

#define C_INK    rgb_hex(0x100C08)      /* panel fill */
#define C_RULE   rgb_hex(0xD81E2C)      /* comic-red rule, the selected row */
#define C_TEXT   rgb_hex(0xF4EEDC)      /* paper white */
#define C_DIM    rgb_hex(0x8A8272)

#define PANEL_X 390
#define PANEL_Y 170
#define PANEL_W 500
#define PANEL_H 500
#define ROW_H 44
#define LIST_Y (PANEL_Y + 118)

enum { ROW_SAVE, ROW_LOAD, ROW_REWIND, ROW_CHEATS, ROW_QUIT, ROWS };
enum { CROW_LEVEL, CROW_HEALTH, CROW_LIVES, CROW_ONEHIT, CROW_MULTIJUMP, CROW_BACK, CROWS };

static bool open_, in_cheats;
static int selected;
static int level_choice;                /* 0-5: which stage "Jump to level" sets ($FFBF1E) */
static float mouse_scale = 1;
static uint32_t *frame;                 /* the paused frame, fb_w x fb_h */
static int frame_w, frame_h;
static int g_x, g_y, g_w, g_h, out_w, out_h;
static uint32_t *backdrop;               /* out_w x out_h, dimmed, ready to show */
static double now, flash_until;

void (*pause_sound)(int kind);
static void sound(int kind) { if (pause_sound) pause_sound(kind); }

bool pause_is_open(void) { return open_; }

void pause_open(const uint32_t *fb, int fb_w, int fb_h, int fb_pitch, int game_x, int game_y, int game_w, int game_h,
                 int out_w_, int out_h_, float window_to_pixels) {
    open_ = true;
    in_cheats = false;
    selected = 0;
    mouse_scale = window_to_pixels;
    g_x = game_x; g_y = game_y; g_w = game_w; g_h = game_h; out_w = out_w_; out_h = out_h_;
    sound(1);
    ui_begin(out_w, out_h);

    free(frame);
    frame_w = fb_w; frame_h = fb_h;
    frame = malloc((size_t)fb_w * fb_h * 4);
    for (int y = 0; y < fb_h; y++) memcpy(frame + (size_t)y * fb_w, fb + (size_t)y * fb_pitch, (size_t)fb_w * 4);

    /* The frame where the game shows it, nearest-neighbour (no blur: stays pixel art),
     * just dimmed a little so it clearly reads as "paused" without hiding it. */
    free(backdrop);
    backdrop = calloc((size_t)out_w * out_h, 4);
    for (int y = 0; y < out_h; y++)
        for (int x = 0; x < out_w; x++) {
            int fx = x - g_x, fy = y - g_y;
            uint32_t px = 0;
            if (fx >= 0 && fy >= 0 && fx < g_w && fy < g_h)
                px = frame[(fy * frame_h / g_h) * frame_w + fx * frame_w / g_w];
            Rgb c = scale(unpack(px), .55f);
            backdrop[y * out_w + x] = pack(c);
        }
}

void pause_close(void) { open_ = false; }

static int rows(void) { return in_cheats ? CROWS : ROWS; }

static int hit_test(float mx, float my) {
    for (int i = 0; i < rows(); i++) {
        float y = LIST_Y + i * ROW_H;
        if (mx >= PANEL_X + 24 && mx <= PANEL_X + PANEL_W - 24 && my >= y && my < y + ROW_H) return i;
    }
    return -1;
}

static PauseAction activate(int row) {
    if (in_cheats) {
        switch (row) {
        case CROW_LEVEL:
            /* "Level Modifier" (published Action Replay code FFBF1E:00-05, one byte per
             * stage): not confirmed yet whether the game reads it live or only at a level
             * load, so this resets into it rather than claiming a seamless warp. */
            cheats_jump_to_level(level_choice);
            sound(1);
            return PAUSE_LEVEL_JUMP;
        case CROW_HEALTH: settings.cheat_health = !settings.cheat_health; sound(1); break;
        case CROW_LIVES: settings.cheat_lives = !settings.cheat_lives; sound(1); break;
        case CROW_ONEHIT: settings.cheat_onehit = !settings.cheat_onehit; sound(1); break;
        case CROW_MULTIJUMP: settings.cheat_multijump = !settings.cheat_multijump; sound(1); break;
        case CROW_BACK: in_cheats = false; selected = ROW_CHEATS; sound(2); break;
        }
        return PAUSE_NONE;
    }
    switch (row) {
    case ROW_SAVE:
        if (savestate_save(settings.state_slot, frame, frame_w, frame_h, frame_w)) { sound(1); flash_until = now + 1; }
        else sound(2);
        return PAUSE_NONE;
    case ROW_LOAD:
        if (savestate_load(settings.state_slot)) { sound(1); return PAUSE_LOADED; }
        sound(2);
        return PAUSE_NONE;
    case ROW_REWIND:
        settings.rewind = !settings.rewind;
        sound(1);
        return PAUSE_NONE;
    case ROW_CHEATS:
        in_cheats = true;
        selected = 0;
        sound(1);
        return PAUSE_NONE;
    case ROW_QUIT:
        sound(1);
        return PAUSE_QUIT;
    }
    return PAUSE_NONE;
}

PauseAction pause_event(const SDL_Event *e) {
    if (!open_) return PAUSE_NONE;
    if (getenv("PAUSE_DEBUG") && e->type == SDL_CONTROLLERBUTTONDOWN)
        fprintf(stderr, "[pause] button %d (%s) down\n", e->cbutton.button,
                SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e->cbutton.button));
    if (input_event_is(IN_MENU, e) || (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE)) {
        if (in_cheats) { in_cheats = false; selected = ROW_CHEATS; sound(2); return PAUSE_NONE; }
        sound(2); pause_close(); return PAUSE_CLOSE;
    }
    if (input_event_is(IN_B, e)) {
        if (in_cheats) { in_cheats = false; selected = ROW_CHEATS; sound(2); return PAUSE_NONE; }
        sound(2); pause_close(); return PAUSE_CLOSE;
    }
    if (input_event_is(IN_A, e)) return activate(selected);
    switch (e->type) {
    case SDL_KEYDOWN:
        switch (e->key.keysym.sym) {
        case SDLK_UP: selected = (selected + rows() - 1) % rows(); sound(0); break;
        case SDLK_DOWN: selected = (selected + 1) % rows(); sound(0); break;
        case SDLK_LEFT: if (in_cheats && selected == CROW_LEVEL) { level_choice = (level_choice + 5) % 6; sound(0); } break;
        case SDLK_RIGHT: if (in_cheats && selected == CROW_LEVEL) { level_choice = (level_choice + 1) % 6; sound(0); } break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return activate(selected);
        }
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: selected = (selected + rows() - 1) % rows(); sound(0); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: selected = (selected + 1) % rows(); sound(0); break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: if (in_cheats && selected == CROW_LEVEL) { level_choice = (level_choice + 5) % 6; sound(0); } break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: if (in_cheats && selected == CROW_LEVEL) { level_choice = (level_choice + 1) % 6; sound(0); } break;
        }
        break;
    case SDL_MOUSEMOTION: {
        float mx = (e->motion.x * mouse_scale - ui_ox) / ui_s, my = (e->motion.y * mouse_scale - ui_oy) / ui_s;
        int h = hit_test(mx, my);
        if (h >= 0) selected = h;
        break;
    }
    case SDL_MOUSEBUTTONDOWN: {
        float mx = (e->button.x * mouse_scale - ui_ox) / ui_s, my = (e->button.y * mouse_scale - ui_oy) / ui_s;
        if (e->button.button == SDL_BUTTON_RIGHT) {
            if (in_cheats) { in_cheats = false; selected = ROW_CHEATS; sound(2); return PAUSE_NONE; }
            sound(2); pause_close(); return PAUSE_CLOSE;
        }
        int h = hit_test(mx, my);
        if (h >= 0 && e->button.button == SDL_BUTTON_LEFT) { selected = h; return activate(h); }
        break;
    }
    }
    return PAUSE_NONE;
}

void pause_render(double time_s) {
    now = time_s;
    if (!open_) return;
    memcpy(ui_px, backdrop, (size_t)out_w * out_h * 4);

    ui_fill_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, C_INK, 1);
    ui_fill_rect(PANEL_X, PANEL_Y, PANEL_W, 6, C_RULE, 1);
    ui_fill_rect(PANEL_X, PANEL_Y + PANEL_H - 6, PANEL_W, 6, C_RULE, 1);
    ui_draw_text(in_cheats ? "CHEATS" : "PAUSE", "Impact", 46, 400, 4, PANEL_X + 32, PANEL_Y + 26, C_TEXT, C_TEXT, 1);
    ui_fill_rect(PANEL_X + 32, PANEL_Y + 92, PANEL_W - 64, 3, C_RULE, 1);

    static const char *base_labels[ROWS] = { "Save state", "Load state", "Rewind / fast-forward", "Cheats", "Quit" };
    static const char *cheat_labels[CROWS] = { "Jump to level", "Infinite health", "Infinite lives",
                                                "1-hit kill enemies", "Multi-jump", "Back" };
    /* Not confirmed to be in story order -- this is the raw index the game's own hidden
     * Jukebox level-select writes (melody track N -> N-1), and at least one value is now
     * known to NOT land where its "Stage" number implied (index 5 landed on scene 3). */
    static const char *level_names[6] = { "Scene 1", "Scene 2", "Scene 3", "Scene 4", "Scene 5", "Scene 6" };
    for (int i = 0; i < rows(); i++) {
        float y = LIST_Y + i * ROW_H;
        bool focus = selected == i;
        if (focus) ui_fill_rect(PANEL_X + 24, y, PANEL_W - 48, ROW_H - 6, C_RULE, .85f);
        char text[64];
        if (!in_cheats && i == ROW_SAVE && now < flash_until) snprintf(text, sizeof text, "Save state -- Saved!");
        else if (in_cheats && i == CROW_LEVEL) snprintf(text, sizeof text, "Jump to level: < %s >", level_names[level_choice]);
        else if (in_cheats && i == CROW_HEALTH) snprintf(text, sizeof text, "Infinite health: %s", settings.cheat_health ? "ON" : "OFF");
        else if (in_cheats && i == CROW_LIVES) snprintf(text, sizeof text, "Infinite lives: %s", settings.cheat_lives ? "ON" : "OFF");
        else if (in_cheats && i == CROW_ONEHIT) snprintf(text, sizeof text, "1-hit kill enemies: %s", settings.cheat_onehit ? "ON" : "OFF");
        else if (in_cheats && i == CROW_MULTIJUMP) snprintf(text, sizeof text, "Multi-jump: %s", settings.cheat_multijump ? "ON" : "OFF");
        else if (in_cheats) snprintf(text, sizeof text, "%s", cheat_labels[i]);
        else if (i == ROW_REWIND) snprintf(text, sizeof text, "Rewind / fast-forward: %s", settings.rewind ? "ON" : "OFF");
        else snprintf(text, sizeof text, "%s", base_labels[i]);
        Rgb c = focus ? C_INK : (!in_cheats && i == ROW_SAVE && now < flash_until ? C_RULE : C_TEXT);
        ui_draw_text(text, "Bahnschrift", 22, 600, 0, PANEL_X + 40, y + 9, c, c, 1);
    }
    ui_draw_text(in_cheats ? "Up/Down select, Left/Right change, Enter confirm, Esc back"
                           : "Up/Down select, Enter confirm, Esc resume",
                 "Segoe UI", 13, 400, 0, PANEL_X + 32, PANEL_Y + PANEL_H - 34, C_DIM, C_DIM, 1);
}
