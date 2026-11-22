/* Built-in cheats (cheats.c): RAM values held every frame. */
#ifndef CHEATS_H
#define CHEATS_H

#include <stdbool.h>
#include <stdint.h>

/* Raw codes: "FFxxxx:VV" (byte) or "FFxxxx:VVVV" (word), comma-separated. Returns
 * the number of codes parsed. They are held every frame until cheats_clear_codes(). */
int  cheats_add_codes(const char *spec);
/* Writes RAM codes in the same form once, now (tests: --poke-at). */
void cheats_poke_once(const char *spec);
void cheats_clear_codes(void);

/* Called once per frame before the game runs: applies the enabled cheats. */
void cheats_frame(void);

/* After every machine reset (machine_override): the Options screens' hidden entries, for
 * settings.cheat_unlock. */
void cheats_hooks(void);

/* Comix Zone: holds $FFBF1E (the published "Level Modifier" Action Replay address) at
 * stage (0-5) for a couple of seconds after a reset, so a fresh boot reads it whatever
 * the moment it checks it -- call right before machine_reset(). */
void cheats_jump_to_level(int stage);
/* True for a few frames every so often during that hold, pulsing Start so the SEGA logo,
 * title and any menu in between go by on their own instead of the player having to do it. */
bool cheats_force_start(void);

#endif
