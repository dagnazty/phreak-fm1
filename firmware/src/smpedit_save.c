/* SPDX-License-Identifier: GPL-3.0-only */
/* PHREAK 1.5: the SAMPLE EDITOR's SAVE (smpedit.c): the slot rebuilt in the delay line's RAM, then written.
 * Included by felucca.c after the flash driver and editor.c (ed_smp_inval). */
/* ---- SAVE: the slot built again in dly_buf, then written */
#define SE_STAGE ((uint8_t *)(void *)dly_buf)
#define SE_STAGE_MAX SMP_USER_SIZE
#define SE_CKP ((uint32_t *)(void *)((uint8_t *)(void *)dly_buf + (72u * 1024u)))   /* checkpoints: (pred & 0xFFFF) | idx << 16 */
#define SE_BUF ((int16_t *)(void *)((uint8_t *)(void *)dly_buf + (76u * 1024u)))    /* a block of decoded samples (512) */
_Static_assert(sizeof dly_buf >= (76u * 1024u) + 1024u, "the editor's staging fits the delay line");
_Static_assert(SE_STAGE_MAX <= (72u * 1024u), "the staged slot below the checkpoints");

static struct {
    const uint8_t *d;                                /* the zone's source ADPCM */
    uint32_t n, rev, pos, b0, bn;                    /* samples; reversed; next source index (forward) */
    int32_t pred, idx;                               /* the forward decoder */
} se_rd;
static uint32_t se_out, se_half;
static int32_t se_epred, se_eidx;

static void se_rd_init(const uint8_t *d, uint32_t n, uint32_t rev)
{
    uint32_t i;
    se_rd.d = d, se_rd.n = n, se_rd.rev = rev, se_rd.pos = 0, se_rd.pred = 0, se_rd.idx = 0, se_rd.bn = 0;
    if (!rev)
        return;
    for (i = 0; i < n; i++) {                        /* checkpoints: the state before sample k * SE_CK */
        if (!(i % SE_CK))
            SE_CKP[i / SE_CK] = ((uint32_t)se_rd.pred & 0xFFFFu) | (uint32_t)se_rd.idx << 16;
        se_dec(d, i, &se_rd.pred, &se_rd.idx);
    }
}
/* the sample at position p as shown (p ascending for a forward zone) */
static int32_t se_rd_at(uint32_t p)
{
    uint32_t s, lo, c, i;
    int32_t pred, idx, x;
    if (!se_rd.rev) {
        while (se_rd.pos < p)
            se_dec(se_rd.d, se_rd.pos++, &se_rd.pred, &se_rd.idx);
        se_rd.pos++;
        return se_dec(se_rd.d, p, &se_rd.pred, &se_rd.idx);
    }
    s = se_rd.n - 1u - p;
    if (!se_rd.bn || s < se_rd.b0 || s >= se_rd.b0 + se_rd.bn) {   /* the block of up to 256 below s, decoded */
        lo = s >= 255u ? s - 255u : 0u;
        c = lo / SE_CK;
        pred = (int16_t)(SE_CKP[c] & 0xFFFFu), idx = (int32_t)(SE_CKP[c] >> 16);
        for (i = c * SE_CK; i <= s; i++) {
            x = se_dec(se_rd.d, i, &pred, &idx);
            if (i >= lo)
                SE_BUF[i - lo] = (int16_t)x;
        }
        se_rd.b0 = lo, se_rd.bn = s - lo + 1u;
    }
    return SE_BUF[s - se_rd.b0];
}

static void se_put(uint32_t b) { SE_STAGE[SMP_USER_DATA + se_out++] = (uint8_t)b; }
static void se_enc(int32_t x)
{
    int32_t step = IMA_STEP[se_eidx], d = x - se_epred, vd = step >> 3;
    uint32_t code = 0;
    if (d < 0)
        code = 8, d = -d;
    if (d >= step)
        code |= 4, d -= step, vd += step;
    if (d >= step >> 1)
        code |= 2, d -= step >> 1, vd += step >> 1;
    if (d >= step >> 2)
        code |= 1, vd += step >> 2;
    se_epred = clamp(se_epred + ((code & 8u) ? -vd : vd), -32768, 32767);
    se_eidx = clamp(se_eidx + IMA_IDX[code & 7u], 0, 88);
    if (!se_half) {
        se_half = 0x10u | code;
        return;
    }
    se_put((se_half & 15u) | code << 4);
    se_half = 0;
}

/* zone i into the stage at se_out: its new header zone in *o */
static void se_build_zone(uint32_t i, smp_zone_t *o)
{
    const smp_zone_t *z = &se_hdr()->zone[i];
    const se_zone_t *e = &se_z[i];
    const uint8_t *d = se_data() + z->off;
    uint32_t n = z->n, len = e->en - e->st, k, fi, fo, pk = 0;
    int32_t pred = 0, idx = 0, x, g = 1 << 15;
    *o = *z;
    o->off = se_out;
    o->root16 = e->root16, o->lo = e->lo, o->hi = e->hi, o->looped = e->looped;
    if (!e->st && e->en == n && !e->rev && !e->norm && !e->fin && !e->fout) {   /* only LOOP / KEYS: the bytes kept */
        for (k = 0; k < (n + 1u) / 2u; k++)
            se_put(d[k]);
        for (k = 0; k < e->ls; k++)
            se_dec(d, k, &pred, &idx);
        o->ls = e->ls, o->le = e->le;
        o->pred = (int16_t)pred, o->idx = (uint8_t)idx;
        return;
    }
    if (e->norm) {                                   /* the peak of what stays */
        uint32_t a = e->rev ? n - e->en : e->st, b = e->rev ? n - e->st : e->en;
        for (k = 0; k < b; k++) {
            x = se_dec(d, k, &pred, &idx);
            if (k >= a && (uint32_t)(x < 0 ? -x : x) > pk)
                pk = (uint32_t)(x < 0 ? -x : x);
        }
        g = pk ? clamp((int32_t)((30000u << 15) / pk), 1 << 13, 16 << 15) : 1 << 15;   /* (no 64-bit divide here) */
    }
    se_rd_init(d, n, e->rev);
    fi = (uint32_t)e->fin * se_hz_of(z) / 1000u, fo = (uint32_t)e->fout * se_hz_of(z) / 1000u;   /* ms -> samples */
    se_epred = se_eidx = 0, se_half = 0;
    o->pred = 0, o->idx = 0;
    for (k = 0; k < len; k++) {
        x = se_rd_at(e->st + k);
        x = clamp((int32_t)(((int64_t)x * g) >> 15), -32768, 32767);
        if (k < fi)                                  /* (a Q15 ramp: 32-bit divides only) */
            x = (x * (int32_t)(k * 32768u / fi)) >> 15;
        if (len - 1u - k < fo)
            x = (x * (int32_t)((len - 1u - k) * 32768u / fo)) >> 15;
        if (k == e->ls - e->st)                      /* the loop's state: before its first sample */
            o->pred = (int16_t)se_epred, o->idx = (uint8_t)se_eidx;
        se_enc(clamp(x, -32768, 32767));
        if (!(k & 0x3FFFu))
            fm1_wdt_feed();
    }
    if (se_half)
        se_put(se_half & 15u);
    se_half = 0;
    o->n = len, o->ls = e->ls - e->st, o->le = e->le - e->st;
}

/* ---- PHREAK 1.6: the preview. The zone as edited, built into the stage (se_build_zone, as SAVE), played by
 * smpedit_pv.c. The delay line is handed over first (se_dly_off: the audio side stops using it within a block) */
#define SE_BLOCK_US 12000u                           /* (two audio blocks: the one being rendered is done) */
static void se_pv_start(void)
{
    smp_zone_t z;
    se_pv.on = 0;
    if (!se_dly_off) {
        se_dly_off = 1;
        fm1_delay_us(SE_BLOCK_US);
    }
    se_out = 0;
    se_build_zone(se_zi, &z);
    z.off = 0;
    se_pv.z = z;
    se_pv.pos = 0, se_pv.frac = 0, se_pv.pred = 0, se_pv.idx = 0, se_pv.prev = 0, se_pv.cur = 0;
    se_pv.on = 1;
}
static void se_pv_stop(void)
{
    if (!se_dly_off)
        return;
    se_pv.on = 0;
    fm1_delay_us(SE_BLOCK_US);
    memset(dly_buf, 0, sizeof dly_buf);              /* the delay line, silent again */
    fx.dly_lp = 0;
    se_dly_off = 0;
}

static void se_save(void)
{
    smp_user_hdr_t h;
    const smp_user_hdr_t *src = se_hdr();
    uint32_t i, k = se_k, total, took;
    int rc = 0;
    if (song.playing || transport_req) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (!se_dirty || !se_nz) {
        ui_message("NOTHING TO SAVE");
        return;
    }
    lcd_fill(0, 200, 240, 40, C_BLACK);
    cv_begin(240, 20, C_BLACK);
    cv_text(120 - text_w(&FONT_S, "SAVING...") / 2, 2, &FONT_S, "SAVING...", C_WHITE);
    cv_blit(0, 210);
    se_pv_stop();                                    /* (a preview playing: it stops, the line handed back) */
    fm1_irq_mask(FM1_IRQ_ALNK0);                     /* no audio while dly_buf is the stage */
    fm1_delay_us(3000u);                             /* (a block being rendered finishes) */
    memset(&h, 0, sizeof h);
    h.magic = SMP_USER_MAGIC, h.version = 1, h.nz = src->nz;
    memcpy(h.name, src->name, sizeof h.name);
    se_out = 0;
    for (i = 0; i < src->nz; i++)
        se_build_zone(i, &h.zone[i]);
    h.data_len = se_out;
    h.crc = st_crc32(SE_STAGE + SMP_USER_DATA, se_out);
    memset(SE_STAGE, 0xFF, SMP_USER_DATA);
    memcpy(SE_STAGE, &h, sizeof h);
    total = SMP_USER_DATA + se_out;
    usr_nz[k] = 0;                                   /* (no voice reads the slot while it is written) */
    for (i = 0; i < 16u; i++)
        usr_zone[k][i].n = 0;
    for (i = 0; i < (total + 0xFFFu) / 0x1000u && !rc; i++) {
        rc = fl_erase4k_quiet(SMP_USER_OFF(k) + i * 0x1000u, &took);
        fm1_wdt_feed();
    }
    if (!rc)
        rc = fl_write(SMP_USER_OFF(k), SE_STAGE, total);
    ed_smp_inval(k);
    memset(dly_buf, 0, sizeof dly_buf);              /* the delay line, silent again */
    fx.dly_lp = 0;
    fm1_irq_unmask(FM1_IRQ_ALNK0);
    smp_user_scan(k);
    se_load();
    ui_message(rc ? "FLASH ERROR" : usr_nz[k] ? "SAVED" : "SLOT UNREADABLE");
    ui.force = 1;
}


/* ---- PHREAK 1.8: RESAMPLE's take (resample.c, in dly_buf's lower half) into its slot: one zone over every key, root
 * C4, at the take's rate. The main loop calls it with the transport stopped (an erase stops the audio: ~1 s for a
 * full slot); the audio interrupt is held off while the line is the source, the delay line cleared and whole after */
static int rs_write(void)
{
    smp_user_hdr_t h;
    uint8_t hb[SMP_USER_DATA];
    uint32_t i, k = rsm.k % SMP_USER_SLOTS, bytes = (rsm.out + 1u) / 2u, took;
    int rc = 0;
    fm1_irq_mask(FM1_IRQ_ALNK0);                     /* (the line is the source: nothing writes it now) */
    fm1_delay_us(3000u);
    memset(&h, 0, sizeof h);
    h.magic = SMP_USER_MAGIC, h.version = 1, h.nz = 1;
    memcpy(h.name, "RESAMPLE", 8);
    h.data_len = bytes;
    h.crc = st_crc32(RS_DATA, bytes);
    h.zone[0].off = 0, h.zone[0].n = rsm.out, h.zone[0].ls = 0, h.zone[0].le = rsm.out ? rsm.out - 1u : 0u;
    h.zone[0].rate = rsm.rate2 ? 1u << 15 : 1u << 16;   /* (the rate / 44100, Q16) */
    h.zone[0].root16 = 60 * 16, h.zone[0].lo = 0, h.zone[0].hi = 127;
    memset(hb, 0xFF, sizeof hb);
    memcpy(hb, &h, sizeof h);
    usr_nz[k] = 0;                                   /* (no voice reads the slot while it is written) */
    for (i = 0; i < 16u; i++)
        usr_zone[k][i].n = 0;
    for (i = 0; i < (SMP_USER_DATA + bytes + 0xFFFu) / 0x1000u && !rc; i++) {
        rc = fl_erase4k_quiet(SMP_USER_OFF(k) + i * 0x1000u, &took);
        fm1_wdt_feed();
    }
    if (!rc)
        rc = fl_write(SMP_USER_OFF(k), hb, SMP_USER_DATA);
    if (!rc && bytes)
        rc = fl_write(SMP_USER_OFF(k) + SMP_USER_DATA, RS_DATA, bytes);
    ed_smp_inval(k);
    rsm.st = RS_OFF;
    memset(dly_buf, 0, sizeof dly_buf);              /* the delay line, whole and silent again */
    fx.dly_lp = 0;
    rs_dly_half = 0;
    fm1_irq_unmask(FM1_IRQ_ALNK0);
    smp_user_scan(k);
    return rc ? -1 : usr_nz[k] ? 0 : -2;
}
/* the delay line for a take (1: the delay on the upper half, the take's half cleared) or back (0: whole, cleared). The
 * audio side picks the half up at its next block: one block's wait, then the clear */
static void rs_line(uint32_t take)
{
    rs_dly_half = (uint8_t)take;
    fm1_delay_us(SE_BLOCK_US);
    memset(dly_buf, 0, sizeof dly_buf);
    fx.dly_lp = 0;
}
