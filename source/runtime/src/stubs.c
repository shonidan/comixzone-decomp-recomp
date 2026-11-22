/* Stand-ins for the modules this fork drops: Streets of Rage's fighter/combat/AI logic
 * (ai.c, combat.c, outfit.c, fighters.c, automap.c, ost.c jukebox) and the
 * enhanced-edition visual extras (menu.c, hd.c upscaling, texpack.c, motion.c smooth
 * motion, preview.c). vdp.c, launcher.c, settings.c and main.c still include their
 * headers and call into them unchanged; every one of those calls is a safe no-op here,
 * so nothing else needed editing. cheats.c is a real module again (pause.c's Infinite
 * health/lives rows and --poke need it); menu.c stays dropped, pause.c is our own
 * menu. See README.md. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <SDL.h>

#include "ai.h"
#include "automap.h"
#include "combat.h"
#include "fighters.h"
#include "hd.h"
#include "menu.h"
#include "motion.h"
#include "ost.h"
#include "outfit.h"
#include "preview.h"
#include "recomp_rt.h"
#include "texpack.h"

/* ---- ai.h ------------------------------------------------------------------------ */
uint16_t ai_pad(void) { return 0; }
uint16_t ai_pilot_pad(void) { return 0; }
void ai_reset(void) {}

/* ---- automap.h ------------------------------------------------------------------- */
void automap_frame(const HdFrame *f, bool world, float cam_x, float cam_y) {
    (void)f; (void)world; (void)cam_x; (void)cam_y;
}
void automap_load(const char *base_dir) { (void)base_dir; }
void automap_save(const char *base_dir) { (void)base_dir; }
bool automap_draw(float x, float y, float w, float h, float t, float alpha) {
    (void)x; (void)y; (void)w; (void)h; (void)t; (void)alpha;
    return false;
}

/* ---- combat.h ------------------------------------------------------------------ */
void combat_frame(const HdFrame *f, bool world, float cam_x, float cam_y, bool live) {
    (void)f; (void)world; (void)cam_x; (void)cam_y; (void)live;
}
void combat_clear(void) {}
bool combat_active(const HdPost *p) { (void)p; return false; }
void combat_draw(uint32_t *px, int w, int h, const HdPost *p) { (void)px; (void)w; (void)h; (void)p; }
void combat_stats_load(const char *base_dir) { (void)base_dir; }
void combat_stats_save(const char *base_dir) { (void)base_dir; }
int  combat_stats_lines(char lines[][64], int max) { (void)lines; (void)max; return 0; }

/* ---- fighters.h ------------------------------------------------------------------ */
const char *fighter_name(int f) { (void)f; return NULL; }
void fighters_defaults(void) {}
bool fighters_ini_load(const char *key, const char *value) { (void)key; (void)value; return false; }
void fighters_ini_save(FILE *f) { (void)f; }
void fighters_capture_colours(void) {}
bool fighters_preview_save(const char *dir) { (void)dir; return false; }
bool fighters_preview_load(const char *dir) { (void)dir; return false; }
bool fighters_have_preview(int fighter) { (void)fighter; return false; }
void fighters_draw(int f, float cx, float bottom, float scale, float alpha, double time_s) {
    (void)f; (void)cx; (void)bottom; (void)scale; (void)alpha; (void)time_s;
}
void fighters_open(int f) { (void)f; }
bool fighters_is_open(void) { return false; }
bool fighters_event(const SDL_Event *e, float mx, float my) { (void)e; (void)mx; (void)my; return false; }
void fighters_render(double time_s) { (void)time_s; }

/* ---- hd.h: just the storage (vdp.c writes into hd_frame every line) and no-op scaling */
HdFrame hd_frame;
unsigned hd_serial;
uint32_t *hd_px;
int hd_w, hd_h;
double hd_ms[4];
int hd_owned;
int hd_scaler;
bool hd_texpack;
float hd_time;
bool hd_water;
bool hd_sides_black;
void hd_compose(int scale) { (void)scale; }
void hd_frame_done(void) {}
void hd_compose_at(int scale, float frac) { (void)scale; (void)frac; }
float hd_motion_t(float frac) { (void)frac; return 0; }
void hd_compose_sprites(int scale) { (void)scale; }
const uint32_t *hd_post(const uint32_t *src, int w, int h, int pitch, const HdPost *p) {
    (void)w; (void)h; (void)pitch; (void)p;
    return src;
}

/* ---- menu.h ------------------------------------------------------------------------ */
void menu_open(const uint32_t *fb, int fb_w, int fb_h, int fb_pitch, int game_x, int game_y, int game_w, int game_h,
               int out_w, int out_h, float window_to_pixels) {
    (void)fb; (void)fb_w; (void)fb_h; (void)fb_pitch; (void)game_x; (void)game_y; (void)game_w; (void)game_h;
    (void)out_w; (void)out_h; (void)window_to_pixels;
}
void menu_close(void) {}
bool menu_is_open(void) { return false; }
MenuAction menu_event(const SDL_Event *e) { (void)e; return MENU_NONE; }
void menu_render(double time_s) { (void)time_s; }
void (*menu_sound)(int kind) = 0;
void menu_select(int tab, int row) { (void)tab; (void)row; }

/* ---- motion.h ------------------------------------------------------------------ */
void motion_block(uint32_t pc) { (void)pc; }
void motion_reset(void) {}
void motion_player_entries(const uint8_t *sat, int count, bool out[SPRITES_MAX]) {
    (void)sat;
    for (int i = 0; i < count && i < SPRITES_MAX; i++) out[i] = false;
}
bool motion_entry_owner(int i, uint32_t *object, uint16_t *kind) {
    (void)i;
    if (object) *object = 0;
    if (kind) *kind = 0;
    return false;
}
unsigned motion_sprite_moves(const HdFrame *f, int dx[SPRITES_MAX], int dy[SPRITES_MAX], int *owned) {
    (void)f;
    for (int i = 0; i < SPRITES_MAX; i++) { dx[i] = 0; dy[i] = 0; }
    if (owned) *owned = 0;
    return 0;
}

/* ---- ost.h: the game's own sound driver played standalone, for the launcher's jukebox */
uint32_t ost_driver(void) { return 0; }
const char *ost_song_name(int song) { (void)song; return NULL; }
int ost_song_of_request(uint16_t request) { (void)request; return -1; }
int ost_title_song(void) { return -1; }
void ost_start(void) {}
void ost_end(void) {}
bool ost_command(uint16_t cmd) { (void)cmd; return false; }
bool ost_request(uint16_t id) { (void)id; return false; }
void ost_frame(void) {}

/* ---- outfit.h --------------------------------------------------------------------- */
void outfit_defaults(void) {}
bool outfit_ini_load(const char *key, const char *value) { (void)key; (void)value; return false; }
void outfit_ini_save(FILE *f) { (void)f; }
void outfit_apply(void) {}
void outfit_preview_save(const char *dir) { (void)dir; }
bool outfit_preview_load(const char *dir) { (void)dir; return false; }
void outfit_open(void) {}
bool outfit_is_open(void) { return false; }
bool outfit_event(const SDL_Event *e, float mx, float my) { (void)e; (void)mx; (void)my; return false; }
void outfit_render(double time_s) { (void)time_s; }
void (*outfit_sound)(int kind) = 0;

/* ---- preview.h ---------------------------------------------------------------------- */
void preview_set_world(bool world, float cam_x, float cam_y, float focus_x, float focus_y) {
    (void)world; (void)cam_x; (void)cam_y; (void)focus_x; (void)focus_y;
}
void preview_save(const char *dir) { (void)dir; }
bool preview_load(const char *dir) { (void)dir; return false; }
bool preview_ready(void) { return false; }
bool preview_supported(int option) { (void)option; return false; }
void preview_draw(int option, float x, float y, float w, float h, float t) {
    (void)option; (void)x; (void)y; (void)w; (void)h; (void)t;
}

/* ---- texpack.h ------------------------------------------------------------------- */
int  texpack_load(const char *base, const char *name) { (void)base; (void)name; return 0; }
void texpack_dump(const char *base, bool on) { (void)base; (void)on; }
bool texpack_active(void) { return false; }
int  texpack_count(void) { return 0; }
int  texpack_list(const char *base, char names[][64], int max) { (void)base; (void)names; (void)max; return 0; }
uint16_t texpack_tile(int tile, int pal) { (void)tile; (void)pal; return 0; }
void texpack_frame(void) {}
const uint32_t *texpack_texels(uint16_t id, int *size) { (void)id; if (size) *size = 0; return NULL; }
