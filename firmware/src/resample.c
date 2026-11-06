/* SPDX-License-Identifier: GPL-3.0-only */
/* PHREAK 1.8: RESAMPLE, the audio side. The SAMPLE EDITOR's RESAMPLE page (smpedit.c) arms a take into a user
 * slot; this records the FM-1's own mix (before MASTER, as USB AUDIO = FULL: the knob is only what you hear),
 * left + right to mono, as IMA ADPCM, a sample at a time, into the lower 64 KiB of the delay line's RAM. Meanwhile
 * the tempo delay runs on the upper half (rs_dly_half: 0.74 s at most, fx.c fx_buses), so the delay and the
 * punch-in FX stay in the take. smpedit_save.c rs_write puts the take in the slot once the transport stops.
 *
 *   RATE    22 kHz (two samples averaged: 5.9 s) or 44 kHz (3 s)
 *   BARS    FREE (until REC or full) or 1, 2, 4, 8 bars at the tempo when armed
 *   START   PLAY (the first block of a bar, the transport running: PLAY from stopped starts it on bar 1),
 *           NOW, NOTE (the first sample over RS_GATE: a key, a step)
 *   SOURCE  MIX (all of it) or DRY (before the chorus, delay, reverb, DUST, punch-in FX and the DJ filter)
 *
 * Included by fx.c before mix_block. rsm.st, rs_dly_half: declared at the top of fx.c. */
#define RS_CAP (64u * 1024u)                          /* ADPCM bytes: dly_buf's lower half */
#define RS_DATA ((uint8_t *)(void *)dly_buf)
#define RS_MAXN (RS_CAP * 2u)                         /* samples a take holds */
#define RS_GATE 330                                   /* NOTE: about -40 dB */
_Static_assert(sizeof dly_buf == 2u * RS_CAP, "the take is the delay line's lower half");
_Static_assert(RS_MAXN <= 2u * SMP_USER_SIZE && RS_CAP <= SMP_USER_SIZE - SMP_USER_DATA, "a take fits a slot");

/* source frames (44.1 kHz) the take can hold at this rate */
static uint32_t rs_cap_frames(uint32_t rate2) { return RS_MAXN * (rate2 ? 2u : 1u); }
/* source frames for bars at bpm */
static uint32_t rs_bar_frames(uint32_t bars, uint32_t bpm) { return bars * 4u * BEAT_U / (bpm ? bpm : 120u); }

static void rs_put(int32_t x)                          /* one sample, encoded */
{
    int32_t step = IMA_STEP[rsm.idx], d = x - rsm.pred, vd = step >> 3;
    uint32_t code = 0;
    if (d < 0)
        code = 8, d = -d;
    if (d >= step)
        code |= 4, d -= step, vd += step;
    if (d >= step >> 1)
        code |= 2, d -= step >> 1, vd += step >> 1;
    if (d >= step >> 2)
        code |= 1, vd += step >> 2;
    rsm.pred = clamp(rsm.pred + ((code & 8u) ? -vd : vd), -32768, 32767);
    rsm.idx = clamp(rsm.idx + IMA_IDX[code & 7u], 0, 88);
    if (rsm.out & 1u)
        RS_DATA[rsm.out >> 1] = (uint8_t)(RS_DATA[rsm.out >> 1] | code << 4);
    else
        RS_DATA[rsm.out >> 1] = (uint8_t)code;
    rsm.out++;
}

static void rs_finish(void)                            /* the take is done: rs_write takes it from here */
{
    rsm.st = RS_DONE;
}

static void rs_begin(void)
{
    rsm.st = RS_REC;
    rsm.got = rsm.out = 0;
    rsm.pred = rsm.idx = 0;
    rsm.acc = 0;
    rsm.peak = 0;
}

/* at a block's start, before its events: PLAY's start on the grid, the stops of the transport and of REC */
static void rs_block_begin(uint32_t n)
{
    uint32_t bpm = (uint32_t)song.g[G_BPM];
    if (rsm.st == RS_ARMED) {
        if (rsm.start == RS_START_NOW)
            rs_begin();
        else if (rsm.start == RS_START_PLAY && song.playing && !(clk_beat & 3u) && clk_pos < n * bpm)
            rs_begin();                                /* (the block a bar starts in) */
    } else if (rsm.st == RS_REC) {
        if (rsm.stop_req || (rsm.start == RS_START_PLAY && !song.playing))
            rs_finish();
    }
    rsm.stop_req = 0;
}

/* the mix (dry 0: after everything, before MASTER; 1: before the effects), a block of it */
static void rs_tap(const int32_t *l, const int32_t *r, uint32_t n, uint32_t dry)
{
    uint32_t i;
    if ((rsm.st != RS_ARMED && rsm.st != RS_REC) || rsm.dry != dry)
        return;
    for (i = 0; i < n; i++) {
        int32_t s = knee((l[i] >> 1) + (r[i] >> 1)), a = s < 0 ? -s : s;
        if (rsm.st == RS_ARMED) {
            if (rsm.start != RS_START_NOTE || a < RS_GATE)
                continue;
            rs_begin();
        }
        if (a > rsm.peak)
            rsm.peak = a;
        if (a >= 32000)
            rsm.clip = 1;
        rsm.got++;
        if (rsm.rate2) {                                /* 22 kHz: the pair's mean (a gentle low-pass, then half) */
            if (rsm.got & 1u) {
                rsm.acc = s;
                continue;
            }
            s = (rsm.acc + s) >> 1;
        }
        rs_put(s);
        if (rsm.out >= RS_MAXN || (rsm.want && rsm.got >= rsm.want)) {
            rs_finish();
            return;
        }
    }
}
