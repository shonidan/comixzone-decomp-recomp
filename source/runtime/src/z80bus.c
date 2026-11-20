/* Z80 sound CPU: its memory map, bank window into 68000 space, and scheduling. */
#include "genesis.h"
#include "z80.h"
#include <stdio.h>

static z80 cpu;
static uint32_t bank;                   /* bits 15-23 of the 68000 address seen at $8000-$FFFF */
static double cycle_debt;               /* fractional Z80 cycles owed */

/* GEMS_DEBUG: call/ret nesting depth, reconstructed from CALL/RST/RET opcodes (not real Z80
 * state -- the real stack also holds PUSHed registers, which this ignores). Lets us log the
 * CALL/RET sequence instead of only a one-sample-per-frame PC, to see which routine the Z80 is
 * stuck in when the GEMS sequencer hangs. See decomp/NOTES.md. */
static int calltrace_depth;
static int saw_callret_this_frame;      /* set by z80_trace_call_ret, read/reset once per frame */
static int stall_frames;                /* consecutive frames with zero CALL/RET events */
static int hang_triggered;              /* true once the full-instruction dump below has fired */
static int hang_dump_budget;            /* instructions left to dump, once triggered (bounded: the
                                          * hang is permanent, so this caps log growth) */

/* GEMS_DEBUG: small ring of recently-executed instructions (pc/op/sp before each step), so that
 * when something unusual is detected at a call site we can print how execution actually got
 * there, instead of guessing from a cold read of the disassembly. See decomp/NOTES.md. */
#define RINGBUF_SIZE 48
static uint16_t ring_pc[RINGBUF_SIZE];
static uint8_t  ring_op[RINGBUF_SIZE];
static uint16_t ring_sp[RINGBUF_SIZE];
static int      ring_pos;
static int      anomaly_dumped;         /* only dump the ring once, the first time this fires */
static int      anomaly_trace_budget;   /* forward step trace armed right when the anomaly fires */

/* GEMS_DEBUG: full instruction trace of one NORMAL (returning) call to the $0699->$03B0 DACME
 * path, to diff against the hang. Triggers once, the first time that path is entered with the
 * usual hl=0000/bc=0003 -- before assuming a timer-pacing fix is the right call, we need to see
 * where a successful call actually returns from, not just guess from a cold disassembly read.
 * See decomp/NOTES.md, parte 4. */
static int normal_trace_active;
static int normal_trace_entry_depth;
static int normal_trace_budget;
static int normal_trace_done;

static void ring_push(uint16_t pc0, uint8_t op, uint16_t sp0) {
    ring_pc[ring_pos] = pc0;
    ring_op[ring_pos] = op;
    ring_sp[ring_pos] = sp0;
    ring_pos = (ring_pos + 1) % RINGBUF_SIZE;
}

static void ring_dump(void) {
    for (int i = 0; i < RINGBUF_SIZE; i++) {
        int idx = (ring_pos + i) % RINGBUF_SIZE;
        fprintf(stderr, "[dbg] frame=%u z80 RINGBUF pc=%04x op=%02x sp=%04x\n",
                g_dbg_frame, ring_pc[idx], ring_op[idx], ring_sp[idx]);
    }
}

static int is_call_op(uint8_t op) {
    switch (op) {
    case 0xCD: case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC:
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        return 1;
    default: return 0;
    }
}

static int is_ret_op(uint8_t op, uint8_t op2) {
    switch (op) {
    case 0xC9: case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        return 1;
    case 0xED: return op2 == 0x4D || op2 == 0x45; /* reti / retn */
    default: return 0;
    }
}

/* Called right after z80_step(). pc0/sp0 are the CPU state *before* that step; op/op2 are the
 * opcode (and, for 0xED, its second byte) fetched at pc0. A taken CALL/RST (or, folded into the
 * same z80_step, an interrupt entry right after it) lowers SP by 2 per push; a taken RET/RETcc/
 * RETI/RETN raises it by 2 per pop. Conditional calls/returns not taken leave SP untouched and
 * are silently skipped. */
static void z80_trace_call_ret(uint16_t pc0, uint16_t sp0, uint8_t op, uint8_t op2) {
    int diff = (int16_t)(uint16_t)(cpu.sp - sp0);   /* multiple of 2, could be negative */
    if (diff <= -2 && (is_call_op(op) || cpu.pc == 0x0038 || cpu.pc == 0x0066)) {
        calltrace_depth += (-diff) / 2;
        saw_callret_this_frame = 1;
        fprintf(stderr, "[dbg] frame=%u z80 CALL depth=%d pc=%04x op=%02x -> target=%04x sp=%04x\n",
                g_dbg_frame, calltrace_depth, pc0, op, cpu.pc, cpu.sp);
        /* GEMS_DEBUG: register/memory snapshot on every entry to DACME (local $03B0, see
         * decomp/NOTES.md) -- DE is its PCM sample pointer; $03E9/$03EE are suspected
         * counter/state cells read back by its inner loop. Lets us compare the call that never
         * returns against the thousands that do. */
        if (cpu.pc == 0x03B0) {
            uint16_t de = (cpu.d << 8) | cpu.e, hl = (cpu.h << 8) | cpu.l, bc = (cpu.b << 8) | cpu.c;
            /* GEMS_DEBUG: the SHADOW bank (b_/c_/d_/e_/h_/l_) is what $03B0's body actually uses
             * once its own EXX runs -- de/hl/bc above are the MAIN bank, still live at the
             * instant this CALL lands, and turned out to be the wrong thing to compare (see
             * decomp/NOTES.md, parte 6): Batman Returns' real source shows D'=$1F and HL'=$4000
             * are fixed for the whole game, set once at boot, so the SHADOW bank is what should
             * be checked against that expectation, not the main one. */
            uint16_t de_ = (cpu.d_ << 8) | cpu.e_, hl_ = (cpu.h_ << 8) | cpu.l_, bc_ = (cpu.b_ << 8) | cpu.c_;
            fprintf(stderr, "[dbg] frame=%u z80 DACME entry de=%04x hl=%04x bc=%04x a=%02x de'=%04x hl'=%04x bc'=%04x mem03e9=%02x mem03ee=%02x mem03bd=%02x,%02x,%02x mem03c8=%02x\n",
                    g_dbg_frame, de, hl, bc, cpu.a, de_, hl_, bc_, bus.zram[0x3E9], bus.zram[0x3EE],
                    bus.zram[0x3BD], bus.zram[0x3BE], bus.zram[0x3BF], bus.zram[0x3C8]);
            /* GEMS_DEBUG: the caller at local $0699 normally enters here with hl=0000 bc=0003
             * (seen 171 times before the hang in one repro) -- any other hl/bc from that same
             * caller is the first sign of the call that then never returns. Dump the last 48
             * instructions executed to see what actually computed that hl/bc. See NOTES.md. */
            if (pc0 == 0x0699 && de == 0x0118 && (hl != 0x0000 || bc != 0x0003) && !anomaly_dumped) {
                anomaly_dumped = 1;
                fprintf(stderr, "[dbg] frame=%u z80 ANOMALY at $0699 entry: de=%04x hl=%04x bc=%04x a=%02x -- dumping last %d instructions\n",
                        g_dbg_frame, de, hl, bc, cpu.a, RINGBUF_SIZE);
                ring_dump();
                anomaly_trace_budget = 400;   /* forward trace: what THIS call does, step by step */
            }
            if (pc0 == 0x0699 && de == 0x0118 && hl == 0x0000 && bc == 0x0003 && !normal_trace_done) {
                normal_trace_done = 1;
                normal_trace_active = 1;
                normal_trace_entry_depth = calltrace_depth;
                normal_trace_budget = 2000;
                fprintf(stderr, "[dbg] frame=%u z80 NORMALTRACE start depth=%d -- tracing until this call returns\n",
                        g_dbg_frame, calltrace_depth);
            }
        }
    } else if (diff >= 2 && is_ret_op(op, op2)) {
        int pops = diff / 2;
        calltrace_depth -= pops < calltrace_depth ? pops : calltrace_depth;
        saw_callret_this_frame = 1;
        fprintf(stderr, "[dbg] frame=%u z80 RET  depth=%d pc=%04x -> target=%04x sp=%04x\n",
                g_dbg_frame, calltrace_depth, pc0, cpu.pc, cpu.sp);
        if (normal_trace_active && calltrace_depth < normal_trace_entry_depth) {
            normal_trace_active = 0;
            fprintf(stderr, "[dbg] frame=%u z80 NORMALTRACE done: returned to depth=%d at pc=%04x\n",
                    g_dbg_frame, calltrace_depth, cpu.pc);
        }
    }
}

static uint8_t z_read(void *ud, uint16_t a) {
    (void)ud;
    if (a < 0x4000) {
        /* GEMS_DEBUG: Comix Zone's command FIFO ($1B40-$1B7F) and its indices ($0036/$0037).
         * Game-specific, see decomp/NOTES.md. */
        if (gems_debug_on() && a >= 0x1B40 && a <= 0x1B7F)
            fprintf(stderr, "[dbg] frame=%u z80 reads fifo[%04X]=%02X rptr=%02X wptr=%02X\n",
                    g_dbg_frame, a, bus.zram[a & 0x1FFF], bus.zram[0x37], bus.zram[0x36]);
        return bus.zram[a & 0x1FFF];
    }
    if (a < 0x6000) {
        uint8_t v = ym_read();
        /* GEMS_DEBUG: YM2612 status-port reads (bit0/1 = Timer A/B overflow, bit7 = busy) --
         * DACME spins on bit0 of this port waiting for Timer A to overflow. See decomp/NOTES.md. */
        if (gems_debug_on())
            fprintf(stderr, "[dbg] frame=%u z80 YM status=%02x timerA_left=%d cyc=%lu\n",
                    g_dbg_frame, v, sound_debug_timer_a_left(), cpu.cyc);
        return v;
    }
    if (a >= 0x8000) return bus_read8(bank | (a & 0x7FFF));
    return 0xFF;
}

static void z_write(void *ud, uint16_t a, uint8_t v) {
    (void)ud;
    if (a < 0x4000) {
        /* GEMS_DEBUG: TICKFLG+1 at $003E/$003F -- the VBlank ISR does `ld (TICKFLG),SP; reti`,
         * leaving interrupts disabled until CHECKTICK notices and re-enables them. See
         * decomp/NOTES.md. */
        if (gems_debug_on() && (a == 0x003E || a == 0x003F))
            fprintf(stderr, "[dbg] frame=%u z80 writes TICKFLG[%04X]=%02X\n", g_dbg_frame, a, v);
        bus.zram[a & 0x1FFF] = v;
    }
    else if (a < 0x6000) {
        /* GEMS_DEBUG: every YM2612 port write, not just the register/value pairs ym_write()
         * recognizes (reg $24/$25/$27/$28) -- lets us see writes to registers that aren't on
         * that list. See decomp/NOTES.md. */
        if (gems_debug_on())
            fprintf(stderr, "[dbg] frame=%u z80 YM port=%04x data=%02x\n", g_dbg_frame, a, v);
        ym_write(a & 3, v);
    }
    else if (a < 0x6100) bank = ((bank >> 1) | ((uint32_t)(v & 1) << 23)) & 0xFF8000; /* serial, 9 writes */
    else if (a == 0x7F11 || a == 0x7F13) psg_write(v);
    else if (a >= 0x8000) bus_write8(bank | (a & 0x7FFF), v);
}

static uint8_t z_in(z80 *z, uint8_t p) { (void)z; (void)p; return 0xFF; }
static void z_out(z80 *z, uint8_t p, uint8_t v) { (void)z; (void)p; (void)v; }

void z80_reset_cpu(void) {
    z80_init(&cpu);
    cpu.read_byte = z_read;
    cpu.write_byte = z_write;
    cpu.port_in = z_in;
    cpu.port_out = z_out;
    bank = 0;
    cycle_debt = 0;
    calltrace_depth = 0;
    saw_callret_this_frame = 0;
    stall_frames = 0;
    hang_triggered = 0;
    hang_dump_budget = 0;
    ring_pos = 0;
    anomaly_dumped = 0;
    anomaly_trace_budget = 0;
    normal_trace_active = 0;
    normal_trace_done = 0;
}

/* Runs the Z80 for the time that `m68k_cycles` 68000 clocks take (Z80 = 68000 * 7/15). */
void z80_run(int m68k_cycles) {
    cycle_debt += m68k_cycles * 7.0 / 15.0;
    if (bus.z80_reset) { z80_reset_cpu(); return; }
    if (bus.z80_busreq) { cycle_debt = 0; return; } /* 68000 owns the bus: Z80 is halted */
    /* Counted from the start: cpu.cyc is 32 bits on Windows and wraps after 2^32 cycles
     * (20 minutes), where comparing against an end value would stop the Z80 for good. */
    unsigned long start = cpu.cyc, n = (unsigned long)cycle_debt;
    cycle_debt -= (double)n;
    if (gems_debug_on()) {
        while (cpu.cyc - start < n) {
            uint16_t pc0 = cpu.pc, sp0 = cpu.sp;
            uint8_t op = (pc0 < 0x4000) ? bus.zram[pc0 & 0x1FFF] : 0;
            uint8_t op2 = (op == 0xED && pc0 < 0x4000) ? bus.zram[(pc0 + 1) & 0x1FFF] : 0;
            ring_push(pc0, op, sp0);
            z80_step(&cpu);
            z80_trace_call_ret(pc0, sp0, op, op2);
            /* GEMS_DEBUG: once no CALL/RET has fired for 2 whole frames (see epilogue below),
             * the Z80 is presumably stuck in a jump-only loop -- dump a bounded run of raw
             * per-instruction PCs (the once-per-frame sample below can't show the loop's shape)
             * to see exactly which addresses it cycles through. Capped since the hang is
             * permanent and this would otherwise log forever. */
            if (hang_dump_budget > 0) {
                fprintf(stderr, "[dbg] frame=%u z80 HANGSTEP pc=%04x op=%02x op2=%02x -> pc'=%04x sp'=%04x\n",
                        g_dbg_frame, pc0, op, op2, cpu.pc, cpu.sp);
                hang_dump_budget--;
            }
            if (anomaly_trace_budget > 0) {
                fprintf(stderr, "[dbg] frame=%u z80 ANOMALYSTEP pc=%04x op=%02x op2=%02x -> pc'=%04x sp'=%04x\n",
                        g_dbg_frame, pc0, op, op2, cpu.pc, cpu.sp);
                anomaly_trace_budget--;
            }
            if (normal_trace_active) {
                fprintf(stderr, "[dbg] frame=%u z80 NORMALTRACE pc=%04x op=%02x op2=%02x -> pc'=%04x sp'=%04x\n",
                        g_dbg_frame, pc0, op, op2, cpu.pc, cpu.sp);
                if (--normal_trace_budget <= 0) {
                    normal_trace_active = 0;
                    fprintf(stderr, "[dbg] frame=%u z80 NORMALTRACE budget exhausted without returning\n", g_dbg_frame);
                }
            }
        }
    } else {
        while (cpu.cyc - start < n) z80_step(&cpu);
    }
    if (gems_debug_on()) {   /* GEMS_DEBUG: one Z80 PC sample per frame, plus hang auto-detection */
        static uint32_t last_logged;
        if (g_dbg_frame != last_logged) {
            if (!saw_callret_this_frame) {
                stall_frames++;
                if (stall_frames == 2 && !hang_triggered) {
                    hang_triggered = 1;
                    hang_dump_budget = 400;
                    fprintf(stderr, "[dbg] frame=%u z80 HANG? no CALL/RET for %d frames, dumping next %d instructions\n",
                            g_dbg_frame, stall_frames, hang_dump_budget);
                }
            } else {
                stall_frames = 0;
                hang_triggered = 0;   /* re-arm: this stall ended, so the next one gets dumped too */
            }
            saw_callret_this_frame = 0;
            last_logged = g_dbg_frame;
            fprintf(stderr, "[dbg] frame=%u z80.pc=%04x\n", g_dbg_frame, cpu.pc);
        }
    }
}

/* The VDP raises the Z80 interrupt at the start of V blank, for about one line. */
void z80_set_irq(bool on) {
    if (on) z80_gen_int(&cpu, 0xFF);
    else cpu.int_pending = false;
}

void z80_state(StateIO *s) {
    /* The callbacks and userdata point into this process: keep the live ones. */
    z80 live = cpu;
    state_io(s, &cpu, sizeof cpu);
    cpu.read_byte = live.read_byte; cpu.write_byte = live.write_byte;
    cpu.port_in = live.port_in; cpu.port_out = live.port_out; cpu.userdata = live.userdata;
    state_io(s, &bank, sizeof bank);
    state_io(s, &cycle_debt, sizeof cycle_debt);
}
