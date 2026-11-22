/* Built-in cheats: RAM values held every frame, like a Pro Action Replay, plus recompiler
 * patches (config/patches.json).
 *
 * Streets of Rage (docs/ram.md): sorHit routes every hit's damage through sor_hit
 * (recomp_rt.h) for infinite health and the attack power; lives ($FFFF20/23), specials
 * ($FFFF21/24) and the round timer ($FFFB01) are held in RAM.
 *
 * Streets of Rage 2 (docs/ram-sor2.md): sor2Hit does the same for its hits and for what a
 * special move costs; health (+$80), lives (+$83) and the timer ($FFFC3C) are held in RAM.
 *
 * Streets of Rage 3 (docs/ram-sor3.md): sor3Hit for the hits; health (+$6C, its maximum
 * +$6D) and lives (+$8B) are held in RAM. It has no round timer.
 *
 * Beyond Oasis: playerDamage for infinite health, hitDamage for the attack power (double,
 * quadruple, one-hit kills), playerSpeed for the move speed. RAM used:
 *   $FF19E8         player object; word +$00 is 2 while the player exists
 *   $FF1A70/$FF1A72 max / current health  (player +$88/+$8A)
 *   $FF1A74/$FF1A76 max / current spirit  (player +$8C/+$8E; max is 0 before the first spirit)
 *   $FF0D80         8 weapon slots of (id, uses); uses $FF = unbreakable (the knife) */
#include "cheats.h"

#include <stdlib.h>
#include <string.h>

#include "genesis.h"
#include "settings.h"
#include "recomp_rt.h"

int patch_no_damage;
int patch_attack;
int patch_free_specials;
int patch_speed = 100;
int patch_random;

typedef struct { uint16_t addr; uint16_t value; bool word; } Code;

static Code codes[64];                  /* --poke codes */
static int code_count;
static Code user_codes[64];             /* settings.cheat_codes */
static int user_count;
static char user_spec[sizeof settings.cheat_codes];
static uint8_t weapon_id[8], weapon_uses[8];

static int parse(const char *spec, Code *out, int *count, int cap) {
    int added = 0;
    while (*spec && *count < cap) {
        char *end;
        unsigned long a = strtoul(spec, &end, 16);
        if (*end != ':') break;
        const char *v = end + 1;
        unsigned long val = strtoul(v, &end, 16);
        if (end == v) break;
        out[(*count)++] = (Code){ (uint16_t)(a & 0xFFFF), (uint16_t)val, end - v > 2 };
        added++;
        while (*end == ' ') end++;
        spec = *end == ',' ? end + 1 : end;
        while (*spec == ' ') spec++;
    }
    return added;
}

int cheats_add_codes(const char *spec) { return parse(spec, codes, &code_count, 64); }
void cheats_clear_codes(void) { code_count = 0; }

static void apply(const Code *c, int n);
void cheats_poke_once(const char *spec) {
    Code once[16];
    int n = 0;
    parse(spec, once, &n, 16);
    apply(once, n);
}

static uint16_t peek16(uint16_t a) { return (uint16_t)(bus.ram[a] << 8 | bus.ram[(uint16_t)(a + 1)]); }
static void poke16(uint16_t a, uint16_t v) { bus.ram[a] = (uint8_t)(v >> 8); bus.ram[(uint16_t)(a + 1)] = (uint8_t)v; }

static void apply(const Code *c, int n) {
    for (int i = 0; i < n; i++) {
        if (c[i].word) poke16(c[i].addr, c[i].value);
        else bus.ram[c[i].addr] = (uint8_t)c[i].value;
    }
}

/* Hold a current value at its max, when the max looks like a real one. */
static void fill(uint16_t max_addr, uint16_t cur_addr) {
    uint16_t max = peek16(max_addr);
    if (max > 0 && max < 1000 && peek16(cur_addr) < max) poke16(cur_addr, max);
}

/* Weapons lose a use per hit (sub at $8530) and vanish at 0. Restore the uses they had,
 * so turning the cheat off leaves the real counts; a weapon on its last use gets 2. */
static void hold_weapons(void) {
    for (int k = 0; k < 8; k++) {
        uint8_t id = bus.ram[0x0D80 + 2 * k], uses = bus.ram[0x0D81 + 2 * k];
        bool breakable = id >= 2 && id <= 0x0C && uses != 0xFF;
        if (breakable && id == weapon_id[k] && uses < weapon_uses[k]) bus.ram[0x0D81 + 2 * k] = uses = weapon_uses[k];
        if (breakable && uses == 1) bus.ram[0x0D81 + 2 * k] = uses = 2;
        weapon_id[k] = id;
        weapon_uses[k] = uses;
    }
}

/* ---- Randomizer -------------------------------------------------------------------------- *
 * Item IDs (the game's own names): $01-$0C weapons (Knife, Sword, Broad, Death, Omega, Bow,
 * Fire, Metal, Bomb, Power, Hyper, Atm Bow), $0D-$10 keys, $11 HP Up, $12-$15 the spirit
 * stones, $16-$19 lasting upgrades, $1A-$1D the spirit calls, $1E-$29 the restoratives
 * (HP, SP, HP+SP: 1/4, 1/2, 3/4, Max), $2A Elixir, $31 L. Cube. Only restoratives (and, at
 * the higher setting, weapons) are swapped, each for one of its own kind: the keys, upgrades,
 * stones, calls, the Knife Ali starts with and the Bomb (which the way on may need) never
 * change, so the game can always be finished. */
static const uint8_t restoratives[] = { 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A };
static const uint8_t weapons[] = { 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C };

static bool in(const uint8_t *set, size_t n, uint32_t id) { for (size_t i = 0; i < n; i++) if (set[i] == id) return true; return false; }

uint32_t random_drop_swap(uint32_t item, uint32_t place) {
    item &= 0xFFFF;
    uint32_t id = item >> 8;
    if (item >= 0x8000 || !id) return item;           /* the one-time table, or nothing */
    const uint8_t *set;
    size_t n;
    if (in(restoratives, sizeof restoratives, id)) { set = restoratives; n = sizeof restoratives; }
    else if (patch_random >= 2 && in(weapons, sizeof weapons, id)) { set = weapons; n = sizeof weapons; }
    else return item;
    /* The same seed, area, place and item always give the same: a chest keeps its prize. */
    uint32_t h = (uint32_t)settings.randomizer_seed * 2654435761u;
    for (uint16_t a = 0x16F6; a < 0x16FE; a++) h = (h ^ bus.ram[a]) * 16777619u;
    h = (h ^ place) * 16777619u;
    h = (h ^ item) * 16777619u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
    return (uint32_t)set[h % n] << 8 | (item & 0xFF);
}

/* Streets of Rage (docs/ram.md). */
static void cheats_sor1(void) {
    /* Damage goes through sor_hit (recomp_rt.h): none for the players with infinite
     * health, more from their hits with the attack power. */
    patch_no_damage = settings.cheat_health;
    patch_attack = settings.cheat_attack;
    static uint8_t timer_held;
    uint8_t in = bus.ram[0xFF18];
    if (bus.ram[0xFF2A] || !(in & 3)) timer_held = 0;      /* the demo, or between games */
    else {
        for (int p = 0; p < 2; p++) {
            if (!(in & (1 << p))) continue;
            uint16_t obj = p ? 0xB880 : 0xB800, hp = peek16(obj + 0x32);
            if (settings.cheat_health && peek16(obj) && hp < 0x50) poke16(obj + 0x32, 0x50);
            if (settings.cheat_lives && bus.ram[0xFF20 + 3 * p] < 3) bus.ram[0xFF20 + 3 * p] = 3;
            /* Not in the last two rooms (round 8, part 5 on: the two bosses and Mr. X): there
             * the police car never finishes and the game waits for it for good, in the
             * interpreter too, so the cheat takes the call away rather than give one. */
            if (settings.cheat_specials) {
                bool no_car = bus.ram[0xFF03] == 7 && bus.ram[0xFF05] >= 5;
                if (no_car) bus.ram[0xFF21 + 3 * p] = 0;
                else if (!bus.ram[0xFF21 + 3 * p]) bus.ram[0xFF21 + 3 * p] = 1;
            }
        }
        /* The timer never goes down: held at the highest it has been this stretch (it
         * starts each part of a round full). */
        uint8_t t = bus.ram[0xFB01];
        if (!settings.cheat_time || t > timer_held) timer_held = t;
        else if (t < timer_held) bus.ram[0xFB01] = timer_held;
    }
}

/* Streets of Rage 2 (docs/ram-sor2.md). */
static void cheats_sor2(void) {
    patch_no_damage = settings.cheat_health;
    patch_attack = settings.cheat_attack;
    patch_free_specials = settings.cheat_specials;
    static uint8_t timer_held;
    if (bus.ram[0xFD10]) { timer_held = 0; return; }            /* the demo */
    for (int p = 0; p < 2; p++) {
        uint16_t obj = p ? 0xF000 : 0xEF00;
        uint16_t state = peek16(obj);
        if (!state || state == 0x34 || state == 0x36) continue;  /* not playing ($34/$36: not joined) */
        if (settings.cheat_health && peek16(obj + 0x80) && peek16(obj + 0x80) < 0x68) poke16(obj + 0x80, 0x68);
        if (settings.cheat_lives && bus.ram[obj + 0x83] < 2) bus.ram[obj + 0x83] = 2;   /* the HUD shows one more */
    }
    /* The timer (BCD seconds) never goes down: held at the highest it has been since the
     * last refill. */
    uint8_t t = bus.ram[0xFC3C];
    if (!settings.cheat_time || t > timer_held) timer_held = t;
    else if (t < timer_held) bus.ram[0xFC3C] = timer_held;
}

/* Streets of Rage 3 (docs/ram-sor3.md). */
static void cheats_sor3(void) {
    patch_no_damage = settings.cheat_health;
    patch_attack = settings.cheat_attack;
    /* The secret fighters, as the game unlocks them: $FF88EF (Up+B+Start held on the title)
     * adds Roo to the select screen, and $FFFB0C / $FFFB0D offer Ash and Shiva in the
     * fighter choice a player gets in the HUD on joining or continuing. The game clears
     * them for a new game, so they are held. */
    if (settings.cheat_fighters) bus.ram[0x88EF] = bus.ram[0xFB0C] = bus.ram[0xFB0D] = 1;
    if (bus.ram[0xFB13]) return;                                 /* the demo */
    for (int p = 0; p < 2; p++) {
        uint16_t obj = p ? 0xE000 : 0xDF00, state = peek16(obj);
        if (!state || state == 0x5C || state == 0x5E) continue;  /* not playing */
        uint8_t hp = bus.ram[obj + 0x6C], max = bus.ram[obj + 0x6D];
        if (settings.cheat_health && hp && hp < max) bus.ram[obj + 0x6C] = max;
        if (settings.cheat_lives && bus.ram[obj + 0x8B] < 2) bus.ram[obj + 0x8B] = 2;
    }
}

/* Comix Zone. Not reverse-engineered from our own RAM traces yet -- taken from a published
 * Action Replay code list (almarsguides.com, USA release): an Enable/Disable pair at $FFBEE7
 * (00 = off, FF = on), not a "hold this value" poke. $FFBEE8 is listed right next to it as a
 * jukebox/level-select debug flag, which together look like a leftover developer debug-flags
 * byte range rather than something built from a normal gameplay counter -- consistent with the
 * game's own damage code checking this flag itself, not us fighting its decrement every frame.
 * Not yet confirmed against this engine's own instrumentation (no --watch session has isolated
 * it independently); treat as a first try pending real play-testing. No equivalent code for
 * lives ("pages") was found published anywhere searched so far. */
static int level_jump_stage = -1, level_jump_frames, level_jump_press_frames;
void cheats_jump_to_level(int stage) {
    level_jump_stage = stage;
    level_jump_frames = 3600;        /* hold $FFFFBFD0 a full minute: plenty even if the
                                       * player takes their time past the logo and title */
    level_jump_press_frames = 900;   /* but only PRESS Start for the first 15 seconds -- Start
                                       * is also the real game's own pause button, so once a
                                       * new game is actually under way, pulsing it ourselves
                                       * would pause the player's own session over and over */
}
bool cheats_force_start(void) { return level_jump_press_frames > 0 && level_jump_press_frames % 100 < 6; }

static void cheats_comixzone(void) {
    bus.ram[0xBEE7] = settings.cheat_health ? 0xFF : 0x00;
    if (level_jump_press_frames > 0) level_jump_press_frames--;
    if (level_jump_frames > 0) {
        /* $FFBF1E isn't read at boot -- it's only ever set by the new-game init routine
         * itself ($1E4710-$1E4714: move.b ($FFFFBFD0).w,d0 ; move.w d0,($FFFFBF1E).w), which
         * only runs once the player presses Start past the SEGA logo and title. Holding
         * $FFBF1E a couple of seconds after a reset (the first attempt) missed that entirely
         * -- init hadn't run yet, so by the time it did the RAM was back to its post-reset
         * zero. $FFFFBFD0 is the real source: it's also what the game's own hidden Jukebox
         * level select writes (holding C on melody track N writes N-1 there, see $1CE78C),
         * confirming the 0-5 range. Held a full minute, long enough to sit through the logo
         * and title and press Start whenever the player gets to it; nothing else in the
         * generated code reads or writes $FFFFBFD0 outside that one init copy, so holding it
         * this long doesn't fight any other system. */
        bus.ram[0xBFD0] = (uint8_t)level_jump_stage;
        level_jump_frames--;
    }
}

void cheats_frame(void) {
    if (!GAME_KNOWS_RAM) {
        if (strcmp(user_spec, settings.cheat_codes)) {
            memcpy(user_spec, settings.cheat_codes, sizeof user_spec);
            user_count = 0;
            parse(user_spec, user_codes, &user_count, 64);
        }
        if (GAME_SOR == 1) cheats_sor1();
        if (GAME_SOR == 2) cheats_sor2();
        if (GAME_SOR == 3) cheats_sor3();
        if (GAME_SOR == 0) cheats_comixzone();
        apply(user_codes, user_count);
        apply(codes, code_count);
        return;
    }
    if (strcmp(user_spec, settings.cheat_codes)) {
        memcpy(user_spec, settings.cheat_codes, sizeof user_spec);
        user_count = 0;
        parse(user_spec, user_codes, &user_count, 64);
    }
    bool player = peek16(0x19E8) == 2;
    patch_no_damage = settings.cheat_health;
    patch_attack = settings.cheat_attack;
    patch_random = settings.randomizer;
    static const int speeds[3] = { 100, 150, 200 };
    patch_speed = speeds[settings.cheat_speed >= 0 && settings.cheat_speed < 3 ? settings.cheat_speed : 0];
    if (player && settings.cheat_health) fill(0x1A70, 0x1A72);
    if (player && settings.cheat_spirit) fill(0x1A74, 0x1A76);
    if (player && settings.cheat_weapons) hold_weapons();
    else memset(weapon_id, 0, sizeof weapon_id);
    apply(user_codes, user_count);
    apply(codes, code_count);
}


/* ---- Hidden options ------------------------------------------------------------------------ *
 * Each game's Options screen has entries its makers kept behind a code; the code sets a flag
 * the screen reads. With cheat_unlock the flag is set where the code would set it, as the
 * screen starts, so whatever pads are held:
 *   Streets of Rage:   A+B+C+Right held on controller 2 as Options is picked ($001164) sets
 *                      $FFFA7B: the round select and the lives setting.
 *   Streets of Rage 2: A+B held on controller 2 as the Options screen starts ($00FC58) sets
 *                      $FFA902: Very Easy and Mania, up to 9 lives and the stage select.
 *   Streets of Rage 3: Up+B held as Options is picked on the title menu ($013DC2 in the US
 *                      edition: game mode $1C) sets $FFF525: the stage select. Found by its
 *                      bytes, as the Japanese editions have it elsewhere. */
static uint32_t unlock_original(uint32_t pc) {
    RecompBlock fn = machine_generated(pc);
    return fn ? fn(pc) : (pc | BAIL);
}

#if GAME_SOR == 1
static uint32_t unlock_hook(uint32_t pc) {
    if (settings.cheat_unlock) bus.ram[0xFA7B] = 1;
    return unlock_original(pc);
}
#elif GAME_SOR == 2
static uint32_t unlock_hook(uint32_t pc) {
    if (settings.cheat_unlock && !peek16(0xA902)) poke16(0xA902, 1);
    return unlock_original(pc);
}
#elif GAME_SOR == 3
static uint16_t unlock_flag;
static uint32_t unlock_hook(uint32_t pc) {
    if (settings.cheat_unlock && peek16(0xFB00) == 0x1C) bus.ram[unlock_flag] = 1;
    return unlock_original(pc);
}
#endif

/* Comix Zone: each of these patches one published Game Genie code's branch -- forced to NOP
 * (always fall through) or to an unconditional short branch (always taken) -- but the first
 * version of this hooked the branch instruction's OWN address. That's reachable two ways: a
 * fresh dispatch through recomp_fn[] (which machine_override() replaces -- the only case the
 * hook actually ran), or plain C fallthrough within the same generated function, entered
 * earlier and already running when it reaches that address (the common case in play, which
 * never consults recomp_fn[] again and so never saw the hook at all -- confirmed against
 * generated/comixzone/main/*.c: each target's enclosing function starts well before the
 * branch itself, with no interruption in between). Hooking each function's REAL start
 * instead (still confirmed a genuine recomp_fn[] entry, so overriding it loses nothing) and
 * either running the original then overriding its outcome, or skipping it outright when the
 * original step must not happen, fixes that. */
static uint32_t hook_onehit(uint32_t pc) {
    /* function start 0x3068; sub.w d0,$4E(a0) then 0x306C bhi.s $30A6 (enemy survives) or
     * falls through to 0x306E (defeated). GG RE2A-A6XN NOPs the branch -- always defeated. */
    uint32_t ret = unlock_original(pc);
    return (settings.cheat_onehit && ret == 0x30A6) ? 0x306E : ret;
}
static uint32_t hook_multijump_a(uint32_t pc) {
    /* function start 0x1D0C0E; 0x1D0C16 bgt.s $1D0C2E (blocked) or falls through to 0x1D0C18.
     * GG REGB-46T0 NOPs the branch -- never blocked. */
    uint32_t ret = unlock_original(pc);
    return (settings.cheat_multijump && ret == 0x1D0C2E) ? 0x1D0C18 : ret;
}
static uint32_t hook_multijump_b(uint32_t pc) {
    /* function start 0x1D0C24; 0x1D0C26 rts, unconditionally. GG A2GB-4A3G replaces the rts
     * itself with bra.s $1D0C2E -- the real return (popping the call stack) must not happen
     * at all when this is on, so skip calling the original rather than override its result. */
    if (settings.cheat_multijump) return 0x1D0C2E;
    return unlock_original(pc);
}
void cheats_hooks(void) {
#if GAME_SOR == 1
    machine_override(0x001174, unlock_hook);   /* after the check at $001164, either way */
#elif GAME_SOR == 2
    machine_override(0x00FC80, unlock_hook);   /* after the check at $00FC58, either way */
#elif GAME_SOR == 3
    /* cmpi.w #$1C,($FB00).w; bne.s; btst #4,($F704).w; beq.s; btst #0,($F704).w; beq.s;
     * move.b #1,($xxxx).w; move.l #sound,d0; jsr (sound).l: hooked after it, where the
     * ways meet. */
    static const uint8_t code[] = { 0x0C, 0x78, 0x00, 0x1C, 0xFB, 0x00, 0x66, 0x22, 0x08, 0x38, 0x00, 0x04, 0xF7, 0x04,
                                    0x67, 0x1A, 0x08, 0x38, 0x00, 0x00, 0xF7, 0x04, 0x67, 0x12, 0x11, 0xFC, 0x00, 0x01 };
    for (uint32_t a = 0; a + 42 <= bus.rom_size; a += 2)
        if (!memcmp(bus.rom + a, code, sizeof code)) {
            unlock_flag = (uint16_t)(bus.rom[a + 28] << 8 | bus.rom[a + 29]);
            machine_override(a + 42, unlock_hook);
            break;
        }
#else
    machine_override(0x3068, hook_onehit);
    machine_override(0x1D0C0E, hook_multijump_a);
    machine_override(0x1D0C24, hook_multijump_b);
#endif
}
