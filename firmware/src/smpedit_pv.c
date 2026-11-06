/* SPDX-License-Identifier: GPL-3.0-only */
/* PHREAK 1.6: the SAMPLE EDITOR's preview (smpedit.c). PLAY in the editor builds the zone as edited (trim, fades,
 * reverse, normalize, loop: smpedit_save.c se_build_zone, as SAVE would write it) into the delay line's RAM, and
 * this plays it, mixed after the metronome click (fx.c), at the sample's own pitch. While the editor has the
 * line (se_dly_off) the tempo delay is silent and writes nothing; it starts clean again after. Included by fx.c,
 * right after dly_buf. */
static volatile uint8_t se_dly_off;                  /* the delay line is the editor's: fx skips the delay */
static struct {
    volatile uint8_t on;                             /* playing (the audio side ends it at the zone's end) */
    volatile uint8_t held;                           /* PLAY held: a HELD loop keeps looping */
    smp_zone_t z;                                    /* the zone built, off from the stage's data */
    uint32_t pos, frac;                              /* the next sample to decode; Q16 between two */
    int32_t pred, idx, prev, cur;
} se_pv;
#define SE_PV_DATA ((const uint8_t *)(const void *)dly_buf + SMP_USER_DATA)

/* IMA ADPCM, a sample at a time (eng_sample.c sample_next without the loop) */
static inline int32_t se_dec(const uint8_t *d, uint32_t pos, int32_t *pred, int32_t *idx)
{
    uint32_t b = d[pos >> 1], code = (pos & 1u) ? (b >> 4) : (b & 15u);
    int32_t step = IMA_STEP[*idx], vd = step >> 3;
    if (code & 4u)
        vd += step;
    if (code & 2u)
        vd += step >> 1;
    if (code & 1u)
        vd += step >> 2;
    *pred = clamp(*pred + ((code & 8u) ? -vd : vd), -32768, 32767);
    *idx = clamp(*idx + IMA_IDX[code & 7u], 0, 88);
    return *pred;
}

static void se_pv_render(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    if (!se_pv.on)
        return;
    for (i = 0; i < n; i++) {
        int32_t y;
        se_pv.frac += se_pv.z.rate;                  /* (the zone's rate / 44100, Q16: 0.5 at 22050) */
        while (se_pv.frac >= 65536u) {
            se_pv.frac -= 65536u;
            if (se_pv.pos > se_pv.z.le && se_pv.z.looped && (se_pv.z.looped != SMP_LOOP_HELD || se_pv.held)) {
                se_pv.pos = se_pv.z.ls;              /* the loop, as SAMPLE plays it */
                se_pv.pred = se_pv.z.pred;
                se_pv.idx = se_pv.z.idx;
            }
            if (se_pv.pos >= se_pv.z.n) {
                se_pv.on = 0;
                return;
            }
            se_pv.prev = se_pv.cur;
            se_pv.cur = se_dec(SE_PV_DATA + se_pv.z.off, se_pv.pos++, &se_pv.pred, &se_pv.idx);
        }
        y = se_pv.prev + (int32_t)(((int64_t)(se_pv.cur - se_pv.prev) * (int32_t)se_pv.frac) >> 16);
        y = (y * 3) >> 2;                            /* (about the level of a voice at full LEVEL) */
        l[i] += y;
        r[i] += y;
    }
}
