/* I/O chip: version register and two control pads: 3-button, or 6-button (its X, Y and Z)
 * for Streets of Rage 3 or a game.json with "sixButton". Without this, a game that probes
 * for a 6-button pad (toggling TH to look for the "all zero" / Z Y X Mode replies) gets
 * ordinary 3-button replies at every pulse count instead, which it can misparse as if they
 * were the extended ones -- A, B and C then read as if shifted to a different button. */
#include "genesis.h"
#include "pads.h"
#include <stdio.h>
#include <stdlib.h>

static bool io_debug_on(void) {
    static int on = -1;
    if (on < 0) on = getenv("IO_DEBUG") ? 1 : 0;
    return on;
}

uint16_t pad_state[2];

IoState io;
#define data_reg io.data
#define ctrl_reg io.ctrl

/* A 6-button pad counts the times TH is raised: after the second, TH low reads all four
 * directions low ("six buttons here"); after the third, TH high reads Z Y X Mode in their
 * place, and TH low the four high. It starts over once TH has not been raised for about
 * 1.5 ms (Streets of Rage 3 probes the ports with other pulses before it reads them). */
#define PAD_RESET_CYCLES 11500          /* 1.5 ms of 68000 clocks */
static uint8_t th_pulses[2];
static int th_last[2];                  /* when TH was last raised, in clocks into the frame */

static int frame_clock(void) { return vdp.line * CYCLES_PER_LINE + machine_cycles_in_line(); }

static void pad_timeout(int port) {
    int t = frame_clock();
    if (t < th_last[port] || t - th_last[port] > PAD_RESET_CYCLES) th_pulses[port] = 0;
}

void io_frame(void) {
    for (int p = 0; p < 2; p++) {
        th_pulses[p] = 0;
        th_last[p] = -PAD_RESET_CYCLES - 1;
    }
}

/* Pin levels the pad drives (active low). TH (bit 6) selects which buttons appear. */
static uint8_t pad_pins(int port) {
    uint16_t p = pad_state[port];
    uint8_t pressed;
    bool six = GAME_SOR == 3 || (GAME_SIX_BUTTON && pads_active_six_button());
    if (six) pad_timeout(port);
    if (data_reg[port] & 0x40) {
        if (six && th_pulses[port] == 3)
            pressed = (uint8_t)((p & PAD_Z ? 1 : 0) | (p & PAD_Y ? 2 : 0) | (p & PAD_X ? 4 : 0) | (p & PAD_MODE ? 8 : 0) | (p & 0x30));
        else
            pressed = (uint8_t)(p & 0x3F);                                      /* C B Right Left Down Up */
    } else {
        pressed = (uint8_t)((p & 0x03) | 0x0C | (p & PAD_A ? 0x10 : 0) | (p & PAD_START ? 0x20 : 0)); /* Left/Right read low */
        if (six && th_pulses[port] == 2) pressed |= 0x0F;                       /* all four low */
        if (six && th_pulses[port] >= 3) pressed &= (uint8_t)~0x0F;             /* all four high */
    }
    return (uint8_t)(~pressed & 0x3F) | 0x40;
}

uint8_t io_read8(uint32_t a) {
    switch (a & 0x1F) {
    case 0x01:                            /* NTSC, no expansion unit, no TMSS; the cartridge's region */
        return game && game->region == REGION_JP ? 0x20 : 0xA0;
    case 0x03: case 0x05: {
        int port = ((a & 0x1F) - 3) >> 1;
        uint8_t in = pad_pins(port);
        uint8_t out = (uint8_t)((data_reg[port] & (ctrl_reg[port] | 0x80)) | (in & ~ctrl_reg[port] & 0x7F));
        if (io_debug_on() && port == 0)
            fprintf(stderr, "[io] port0 read th_pulses=%d TH=%d -> $%02X\n", th_pulses[port],
                    (data_reg[port] & 0x40) != 0, out);
        return out;
    }
    case 0x07: return data_reg[2];
    case 0x09: case 0x0B: case 0x0D: return ctrl_reg[((a & 0x1F) - 9) >> 1];
    }
    return 0x00;
}

void io_write8(uint32_t a, uint8_t v) {
    switch (a & 0x1F) {
    case 0x03: case 0x05: {
        int port = ((a & 0x1F) - 3) >> 1;
        if ((ctrl_reg[port] & 0x40) && !(data_reg[port] & 0x40) && (v & 0x40)) {
            int t = frame_clock(), gap = t - th_last[port];
            pad_timeout(port);
            if (th_pulses[port] < 7) th_pulses[port]++;
            th_last[port] = t;
            if (io_debug_on() && port == 0)
                fprintf(stderr, "[io] port0 TH rise #%d gap=%d%s\n", th_pulses[port], gap,
                        gap > PAD_RESET_CYCLES ? " (TIMED OUT, reset to 1)" : "");
        }
        data_reg[port] = v;
        break;
    }
    case 0x07: data_reg[2] = v; break;
    case 0x09: case 0x0B: case 0x0D: ctrl_reg[((a & 0x1F) - 9) >> 1] = v; break;
    }
}
