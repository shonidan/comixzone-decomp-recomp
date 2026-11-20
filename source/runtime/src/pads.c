/* Controllers: see pads.h.
 *
 * SDL announces the controllers connected at start only once, so whoever opens them keeps
 * them: the launcher and the game share this list instead of each opening its own. A
 * controller that disconnects (a wireless one sleeping, Bluetooth dropping) is closed by
 * its instance id, and opened again when it comes back. */
#include "pads.h"
#include "input.h"

#include <string.h>

#define MAX_PADS 8

static SDL_GameController *pads[MAX_PADS];
static SDL_JoystickID ids[MAX_PADS];
static int count;
static int active = -1;                 /* index of the one used last */

static int find(SDL_JoystickID id) {
    for (int i = 0; i < count; i++) if (ids[i] == id) return i;
    return -1;
}

static void open_device(int device) {
    if (!SDL_IsGameController(device)) return;
    if (find(SDL_JoystickGetDeviceInstanceID(device)) >= 0 || count >= MAX_PADS) return;
    SDL_GameController *c = SDL_GameControllerOpen(device);
    if (!c) return;
    pads[count] = c;
    ids[count] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
    active = count++;                   /* the one just connected is the one to use */
}

static void close_id(SDL_JoystickID id) {
    int i = find(id);
    if (i < 0) return;
    SDL_GameControllerClose(pads[i]);
    /* The others keep the order they came in: it says who is player 1 and who is 2. */
    for (int k = i; k < count - 1; k++) { pads[k] = pads[k + 1]; ids[k] = ids[k + 1]; }
    count--;
    if (active > i) active--;
    else if (active == i) active = count ? 0 : -1;
}

/* SDL's bundled gamecontrollerdb.h doesn't know every controller; these are ones we hit
 * in practice that it's missing. Each is added before enumerating devices, so one already
 * plugged in at startup gets recognized too. SDL_GameControllerAddMapping() is a no-op
 * (returns 0) if its exact GUID is already known, so there's no harm in always trying. */
static const char *extra_mappings[] = {
    /* 8BitDo M30 2.4g (the Sega-licensed 6-button Mega Drive/Genesis pad), Linux, as
     * SDL_JoystickGetGUIDString() reports it over this machine's USB dongle driver. Raw
     * button indices, confirmed with a small test app that lights up a labelled box for
     * whatever the mapping below says is held, so there is no room for a mislabelled
     * button or a misread capture order (both bit us earlier before this app existed):
     * A=2, B=1, C=5, X=3, Y=0, Z=4, Start=9; the D-pad reports as two full-deflection axes
     * (0=X, 1=Y), not a hat. Past the six main buttons, the pad's own manual
     * (download.8bitdo.com/Manual/Controller/M30/M30_Manual.pdf) shows a small L and R
     * (not full-travel triggers), a "-"/select/mode button and a star button; confirmed
     * on this unit: L=b6, R=b7, "-"=b8 (also what the manual separately draws as a heart
     * icon: same physical button, this pad's firmware just doesn't tell the two apart),
     * star sends no event at all (most likely reserved for power/pairing).
     *
     * This mirrors, button for button, what this same pad's *other* identity (plugged in
     * over USB cable, where it reports as a generic "Xbox 360 Controller") gives for free
     * from SDL's own bundled mapping: A/B/X/Y on their matching names, Z on the right
     * shoulder, and C -- which that identity can only deliver as a trigger pull, not a
     * button -- on the right trigger here too (a plain digital raw button standing in for
     * an analog trigger is normal SDL mapping syntax). input.c's GAME_SIX_BUTTON
     * default_buttons[] expects exactly this layout, so both connections play the same. */
    "03001a0da30c00002400000011010000,8BitDo M30 2.4g,"
    "a:b2,b:b1,righttrigger:b5,x:b3,y:b0,rightshoulder:b4,start:b9,"
    "dpup:-a1,dpdown:+a1,dpleft:-a0,dpright:+a0,"
    "leftshoulder:b6,rightstick:b7,back:b8,platform:Linux,",
};

void pads_init(void) {
    for (size_t i = 0; i < sizeof extra_mappings / sizeof *extra_mappings; i++)
        SDL_GameControllerAddMapping(extra_mappings[i]);
    SDL_GameControllerEventState(SDL_ENABLE);
    for (int d = 0; d < SDL_NumJoysticks(); d++) open_device(d);
}

void pads_event(const SDL_Event *e) {
    switch (e->type) {
    case SDL_CONTROLLERDEVICEADDED: open_device(e->cdevice.which); break;
    case SDL_CONTROLLERDEVICEREMOVED: close_id(e->cdevice.which); break;
    case SDL_CONTROLLERBUTTONDOWN: { int i = find(e->cbutton.which); if (i >= 0) active = i; break; }
    case SDL_CONTROLLERAXISMOTION: {
        input_trigger_pressed(e);       /* every move, so a trigger's press is known wherever it comes */
        int i = find(e->caxis.which);
        if (i >= 0 && (e->caxis.value > 16000 || e->caxis.value < -16000)) active = i;
        break;
    }
    default: break;
    }
}

SDL_GameController *pads_active(void) {
    if (active >= 0 && active < count && SDL_GameControllerGetAttached(pads[active])) return pads[active];
    for (int i = 0; i < count; i++) if (SDL_GameControllerGetAttached(pads[i])) return pads[i];
    return NULL;
}

SDL_GameController *pads_player(int player) {
    return player >= 0 && player < count && SDL_GameControllerGetAttached(pads[player]) ? pads[player] : NULL;
}

int pads_count(void) { return count; }

bool pads_active_six_button(void) {
    SDL_GameController *c = pads_active();
    if (!c) c = pads_player(0);         /* connected, but not pressed yet: SDL hasn't marked it active */
    if (!c) return true;                /* keyboard: its own X/Y/Z keys, unaffected by this */
    char guid[33];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(c)), guid, sizeof guid);
    return !strcmp(guid, "03001a0da30c00002400000011010000");   /* the 8BitDo M30 2.4g, see above */
}

void pads_rumble(float low, float high, int ms) {
    SDL_GameController *c = pads_active();
    if (!c) return;
    Uint16 lo = (Uint16)(low < 0 ? 0 : low > 1 ? 65535 : low * 65535), hi = (Uint16)(high < 0 ? 0 : high > 1 ? 65535 : high * 65535);
    SDL_GameControllerRumble(c, lo, hi, (Uint32)ms);
}
