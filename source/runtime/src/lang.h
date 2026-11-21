/* Spanish text patch (lang.c): see its own header comment. */
#ifndef LANG_H
#define LANG_H

#include <stdint.h>

/* Applies (spanish != 0) or reverts (spanish == 0) the Spanish text patch on the loaded
 * ROM image in memory. Safe to call again on a language change: every record carries its
 * own English bytes too, so switching back and forth never needs a second copy of the ROM. */
void lang_apply(uint8_t *rom, uint32_t rom_size, int spanish);

#endif
