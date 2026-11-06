/* SPDX-License-Identifier: GPL-3.0-only */
/* PHREAK 1.5: the SAMPLE EDITOR. Hold LFO (0.7 s) to open it on a user sample slot (USR1..USR6): the one the
 * selected track plays (SAMPLE SET / GRAIN SRC, or a drum track's USR kit), else the last one edited.
 *
 *   PRESETS     the slot            ALGORITHM   the zone (a sample of the slot: a key split, a drum lane)
 *   LFO tap / OCT- OCT+   the page: TRIM, LOOP, KEYS, FX
 *   TRIM   K1 START  K2 END  K3 FADE IN  K4 FADE OUT
 *   LOOP   K1 MODE (OFF, LOOP, HELD: loops while the key is held, then plays to the end)  K2 L.START  K3 L.END
 *   KEYS   K1 ROOT  K2 LOW  K3 HIGH
 *   FX     K1 REVERSE  K2 NORMALIZE
 *   RESAMPLE (PHREAK 1.8) K1 RATE  K2 BARS  K3 START  K4 SOURCE; REC arms a take of the mix into this slot (resample.c)
 *   SAVE   writes the slot (the song stopped)      HOME / LFO held   leaves (a second time when there are edits)
 *   PLAY   (PHREAK 1.6) hears the zone as edited, before SAVE (smpedit_pv.c); PLAY again stops it. A HELD loop
 *          loops while PLAY is held
 * The keys play the track as ever: what is saved. Nothing changes in flash before SAVE.
 *
 * SAVE rebuilds the slot: a zone with only LOOP / KEYS changes keeps its ADPCM bytes (its loop state found by
 * decoding to L.START); a trimmed, faded, reversed or normalized one is decoded and encoded again (IMA ADPCM from
 * 0 / 0, as usbstick.c and tools/sampleio.py), shorter when trimmed: the slot gets that room back. The new slot is
 * built in the delay line's RAM (fx.c dly_buf, 128 KB) with the audio interrupt held off; the line is cleared
 * after. Positions are kept on the zone as shown (after REVERSE): START <= L.START <= L.END < END. */
#define SE_PAGES 5u
#define SE_PG_RS 4u                                  /* (PHREAK 1.8) the RESAMPLE page */
#define SE_W 240u
#define SE_WAVE_Y 44
#define SE_WAVE_H 128
#define SE_CK 256u                                   /* a decoder checkpoint every SE_CK samples (REVERSE) */
typedef struct {
    uint32_t st, en, ls, le;                         /* START, END (exclusive), L.START, L.END (as shown) */
    int16_t root16;
    uint8_t looped, lo, hi, rev, norm, rsv;
    uint16_t fin, fout;                              /* fades, ms */
} se_zone_t;
static se_zone_t se_z[16] __attribute__((section(".pool")));
static int8_t se_env[SE_W][2] __attribute__((section(".pool")));   /* the zone's min / max a column, >> 8 */
static uint8_t se_k, se_zi, se_nz, se_page, se_dirty, se_arm, se_env_ok, se_draw_ok;   /* (se_on, se_shown: ui_draw.c; se_lfo_t0: ui_input.c) */

static const smp_user_hdr_t *se_hdr(void) { return (const smp_user_hdr_t *)smp_user_xip(se_k); }
static const uint8_t *se_data(void) { return smp_user_xip(se_k) + SMP_USER_DATA; }
static uint32_t se_hz_of(const smp_zone_t *z) { uint32_t hz = ((z->rate >> 4) * 44100u + 2048u) >> 12; return hz ? hz : 22050u; }
static int se_valid(void)
{
    const smp_user_hdr_t *h = se_hdr();
    return h->magic == SMP_USER_MAGIC && h->version == 1u && h->nz && h->nz <= 16u &&
           h->data_len <= SMP_USER_SIZE - SMP_USER_DATA;
}

/* the edits from the slot as it is */
static void se_load(void)
{
    const smp_user_hdr_t *h = se_hdr();
    uint32_t i;
    se_nz = usr_nz[se_k] && se_valid() ? h->nz : 0u;   /* (an empty slot: its flash not read) */
    for (i = 0; i < se_nz; i++) {
        const smp_zone_t *z = &h->zone[i];
        se_zone_t *e = &se_z[i];
        e->st = 0, e->en = z->n;
        e->looped = z->looped;
        e->ls = z->ls < z->n ? z->ls : 0u;
        e->le = z->le < z->n ? z->le : z->n - 1u;
        e->root16 = z->root16;
        e->lo = z->lo, e->hi = z->hi;
        e->rev = e->norm = 0;
        e->fin = e->fout = 0;
    }
    se_zi = se_zi < se_nz ? se_zi : 0u;
    se_dirty = se_arm = 0;
    se_env_ok = 0;
    se_draw_ok = 0;
}

/* (se_dec, the IMA decoder: smpedit_pv.c, with the preview player) */

/* the waveform: min / max a column over the zone as shown */
static void se_envelope(void)
{
    const smp_zone_t *z = &se_hdr()->zone[se_zi];
    const uint8_t *d = se_data() + z->off;
    uint32_t n = z->n, i, c;
    int32_t pred = 0, idx = 0, x;
    for (c = 0; c < SE_W; c++)
        se_env[c][0] = 127, se_env[c][1] = -128;
    for (i = 0; i < n; i++) {
        x = se_dec(d, i, &pred, &idx) >> 8;
        c = (se_z[se_zi].rev ? n - 1u - i : i) * SE_W / n;   /* (n < 2^18: 32 bits) */
        if (x < se_env[c][0]) se_env[c][0] = (int8_t)x;
        if (x > se_env[c][1]) se_env[c][1] = (int8_t)x;
        if (!(i & 0x3FFFu))
            fm1_wdt_feed();
    }
    for (c = 0; c < SE_W; c++)
        if (se_env[c][0] > se_env[c][1])
            se_env[c][0] = se_env[c][1] = 0;
    se_env_ok = 1;
}

#ifndef SE_CORE_ONLY                                /* (tests/smpedit_test.c: the data side only) */
/* the slot the selected track plays, else -1 */
static int32_t se_track_slot(void)
{
    uint32_t u;
    if (is_drum(TSEL)) {
        uint32_t kit = drum_kit();
        if (!drum_usr(kit))
            return -1;
        return kit >= DRUM_USR5 ? (int32_t)(4u + kit - DRUM_USR5) : kit == DRUM_PAIR ? 2 : (int32_t)(kit - DRUM_USR);
    }
    if (ENGINES[TSEL->eng_req % NENGINES] != &ENG_SAMPLE && ENGINES[TSEL->eng_req % NENGINES] != &ENG_GRAIN)
        return -1;
    u = smp_src(TSEL->p[P_E0]);
    return u >= SMP_USR0 ? (int32_t)(u - SMP_USR0) : -1;
}

static void se_open(void)
{
    int32_t k = se_track_slot();
    if (k >= 0)
        se_k = (uint8_t)k;
    se_k %= SMP_USER_SLOTS;
    se_on = 1;
    se_page = 0;
    se_load();
    ui.force = 1;
}

/* ---- SAVE (smpedit_save.c, after the flash driver and editor.c: felucca.c defines SE_SAVE_LATER). The host
 * harnesses that include the UI without them get a stub */
#if SE_SAVE_LATER
static void se_save(void);
static void se_pv_start(void);
static void se_pv_stop(void);
static int rs_write(void);
static void rs_line(uint32_t take);
#else
static void se_save(void) { ui_message("NO FLASH HERE"); }
static void se_pv_start(void) {}
static void se_pv_stop(void) {}
static int rs_write(void) { rsm.st = RS_OFF; rs_dly_half = 0; return -1; }
static void rs_line(uint32_t take) { rs_dly_half = (uint8_t)take; memset(dly_buf, 0, sizeof dly_buf); }
#endif

/* ---- PHREAK 1.8: RESAMPLE (resample.c records; smpedit_save.c rs_write writes). The page's settings, kept while
 * the FM-1 is on */
static const uint8_t RS_BARS[5] = {0, 1, 2, 4, 8};
static const char *const RS_START_NAME[3] = {"PLAY", "NOW", "NOTE"};
static uint8_t rs_cfg_rate2 = 1, rs_cfg_bars, rs_cfg_start, rs_cfg_dry;
static uint8_t rs_shown;                             /* the banner is on the screen */
static int rs_fits(uint32_t bi)
{
    return !RS_BARS[bi] || rs_bar_frames(RS_BARS[bi], (uint32_t)song.g[G_BPM]) <= rs_cap_frames(rs_cfg_rate2);
}
static void rs_cancel(void)                          /* the take dropped, the delay line whole again */
{
    rsm.st = RS_OFF;
    rs_line(0);
}
static void rs_arm(void)
{
    uint32_t bars = RS_BARS[rs_cfg_bars % 5u];
    if (!rs_fits(rs_cfg_bars % 5u)) {
        ui_message("TOO LONG AT THIS TEMPO");
        return;
    }
    se_pv_stop();
    rs_line(1);                                      /* (the delay on the upper half; the take's half cleared) */
    rsm.k = se_k, rsm.rate2 = rs_cfg_rate2, rsm.start = rs_cfg_start, rsm.dry = rs_cfg_dry;
    rsm.want = bars ? rs_bar_frames(bars, (uint32_t)song.g[G_BPM]) : 0u;
    rsm.got = rsm.out = 0, rsm.peak = 0, rsm.clip = 0, rsm.stop_req = 0;
    rsm.st = RS_ARMED;
    se_on = 0;
    ui.force = 1;
}
/* the panel while a take is armed, recording or waiting: LFO tap stops it (armed: drops it), LFO held drops it.
 * 1: the press is used */
static int rs_input(uint32_t *pressed, uint32_t lfo)
{
    *pressed &= ~(1u << panel.btn[B_LFO]);           /* (not the LFO page meanwhile) */
    if (lfo == BT_HOLD || (lfo == BT_TAP && rsm.st == RS_ARMED)) {
        rs_cancel();
        ui_message("RESAMPLE OFF");
        return 1;
    }
    if (lfo == BT_TAP && rsm.st == RS_REC)
        rsm.stop_req = 1;
    return 0;
}
/* the main loop: a finished take into its slot once the transport is stopped, then the editor on it */
static void rs_poll(void)
{
    int rc;
    char b[8];
    if (rsm.st != RS_DONE || song.playing || transport_req)
        return;
    str_cpy(b, "USR", sizeof b);
    fmt_int(b + 3, rsm.k + 1);
    if (rsm.out < 441u) {                             /* (under 10 ms: nothing to keep) */
        rs_cancel();
        ui_message("TAKE EMPTY");
        return;
    }
    rc = rs_write();
    if (rc) {
        ui_message(rc == -1 ? "FLASH ERROR" : "SLOT UNREADABLE");
        return;
    }
    se_k = rsm.k;
    se_on = 1;
    se_page = 0;
    se_zi = 0;
    se_load();
    ui_say("TAKE IN ", b);
    ui.force = 1;
}

/* ---- input */
static const char *const SE_PAGE_NAME[SE_PAGES] = {"TRIM", "LOOP", "KEYS", "FX", "RESAMPLE"};
static const char *const SE_LOOP_NAME[3] = {"OFF", "LOOP", "HELD"};
static void se_fix(se_zone_t *e)                     /* START <= L.START <= L.END < END, every one in the zone */
{
    uint32_t n = se_hdr()->zone[se_zi].n;
    e->en = (uint32_t)clamp((int32_t)e->en, 2, (int32_t)n);
    e->st = (uint32_t)clamp((int32_t)e->st, 0, (int32_t)e->en - 2);
    e->ls = (uint32_t)clamp((int32_t)e->ls, (int32_t)e->st, (int32_t)e->en - 1);
    e->le = (uint32_t)clamp((int32_t)e->le, (int32_t)e->ls, (int32_t)e->en - 1);
}
static void rs_knob(uint32_t kn, int32_t s)         /* the RESAMPLE page's knobs */
{
    int32_t d = s > 0 ? 1 : -1, b;
    switch (kn) {
    case 0: rs_cfg_rate2 = s < 0; break;            /* (left: 22 kHz, right: 44 kHz) */
    case 1:
        for (b = (int32_t)rs_cfg_bars + d; b >= 0 && b < 5 && !rs_fits((uint32_t)b); b += d)
            ;                                        /* (past the lengths the take cannot hold at this tempo) */
        if (b >= 0 && b < 5)
            rs_cfg_bars = (uint8_t)b;
        break;
    case 2: rs_cfg_start = (uint8_t)clamp(rs_cfg_start + d, 0, 2); break;
    default: rs_cfg_dry = s > 0; break;
    }
    if (!rs_fits(rs_cfg_bars))
        rs_cfg_bars = 0;
    se_arm = 0, se_draw_ok = 0;
}
static void se_knob(uint32_t kn, int32_t s)
{
    se_zone_t *e = &se_z[se_zi];
    uint32_t n = se_hdr()->zone[se_zi].n, unit = n / SE_W ? n / SE_W : 1u, a, b;
    int32_t acc = accel(EN_K1 + kn, s, 200), d = acc * (int32_t)unit;
    switch (se_page * 4u + kn) {
    case 0: e->st = (uint32_t)clamp((int32_t)e->st + d, 0, (int32_t)n); if (e->ls < e->st) e->ls = e->st; break;
    case 1: e->en = (uint32_t)clamp((int32_t)e->en + d, 0, (int32_t)n); if (e->le >= e->en) e->le = e->en - 1u; break;
    case 2: e->fin = (uint16_t)clamp(e->fin + acc * 10, 0, 3000); break;
    case 3: e->fout = (uint16_t)clamp(e->fout + acc * 10, 0, 3000); break;
    case 4: e->looped = (uint8_t)clamp(e->looped + (s > 0 ? 1 : -1), 0, 2); break;
    case 5: e->ls = (uint32_t)clamp((int32_t)e->ls + d, 0, (int32_t)n); break;
    case 6: e->le = (uint32_t)clamp((int32_t)e->le + d, 0, (int32_t)n); break;
    case 8: e->root16 = (int16_t)clamp(e->root16 + s * 16, 0, 127 * 16); break;
    case 9: e->lo = (uint8_t)clamp(e->lo + s, 0, e->hi); break;
    case 10: e->hi = (uint8_t)clamp(e->hi + s, e->lo, 127); break;
    case 12:                                         /* REVERSE: the marks follow the sound (mirrored) */
        if ((s > 0) == (e->rev != 0))
            return;
        e->rev = s > 0;
        a = e->st, b = e->ls;
        e->st = n - e->en, e->en = n - a;
        e->ls = n - 1u - e->le, e->le = n - 1u - b;
        se_env_ok = 0;
        break;
    case 13: e->norm = s > 0; break;
    default: return;
    }
    se_fix(e);
    se_dirty = 1, se_arm = 0, se_draw_ok = 0;
}
static void se_input(uint32_t pressed, uint32_t home, uint32_t lfo)
{
    uint32_t k;
    int32_t s;
    if (lfo == BT_TAP)
        se_page = (uint8_t)((se_page + 1u) % SE_PAGES), se_draw_ok = 0;
    if (pressed & (1u << panel.btn[B_OCTDN]))
        se_page = (uint8_t)((se_page + SE_PAGES - 1u) % SE_PAGES), se_draw_ok = 0;
    if (pressed & (1u << panel.btn[B_OCTUP]))
        se_page = (uint8_t)((se_page + 1u) % SE_PAGES), se_draw_ok = 0;
    if (home == BT_TAP || lfo == BT_HOLD) {          /* leave; with edits: a second time */
        if (se_dirty && se_arm != 1u) {
            se_arm = 1;
            ui_say("AGAIN: ", "DISCARD");
            ui.arm_t = 90;
        } else {
            se_pv_stop();
            se_on = 0;
            ui.force = 1;
            return;
        }
    }
    if (pressed & (1u << panel.btn[B_SAVE]))
        se_save();
    if ((pressed & (1u << panel.btn[B_REC])) && se_page == SE_PG_RS) {   /* (1.8) REC: arm the take; over a sample or
                                                     * edits: a second time */
        if ((se_nz || se_dirty) && se_arm != 3u) {
            se_arm = 3;
            ui_say("AGAIN: REPLACE ", se_nz ? "SLOT" : "EDITS");
            ui.arm_t = 90;
        } else {
            rs_arm();
            return;
        }
    }
    se_pv.held = (uint8_t)((fm1_in.buttons >> panel.btn[B_PLAY]) & 1u);
    if (pressed & (1u << panel.btn[B_PLAY])) {       /* (PHREAK 1.6) PLAY: the zone as edited; again: stop */
        if (se_pv.on)
            se_pv_stop();
        else if (se_nz)
            se_pv_start();
    } else if (se_dly_off && !se_pv.on) {
        se_pv_stop();                                /* (it played to its end: the delay line back) */
    }
    if ((s = panel_enc(EN_PRESET)) != 0) {           /* another slot; with edits: a second turn */
        if (se_dirty && se_arm != 2u) {
            se_arm = 2;
            ui_say("AGAIN: ", "DISCARD");
        } else {
            se_pv_stop();
            se_k = (uint8_t)((se_k + (s > 0 ? 1u : SMP_USER_SLOTS - 1u)) % SMP_USER_SLOTS);
            se_load();
        }
    }
    if ((s = panel_enc(EN_ALGO)) != 0 && se_nz) {    /* another zone (its edits stay) */
        se_pv_stop();
        se_zi = (uint8_t)clamp((int32_t)se_zi + (s > 0 ? 1 : -1), 0, (int32_t)se_nz - 1);
        se_env_ok = 0, se_draw_ok = 0;
    }
    panel_enc(EN_SELECT);
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0) {
            if (se_page == SE_PG_RS)
                rs_knob(k, s);
            else if (se_nz)
                se_knob(k, s);
        }
}

/* ---- the screen */
static void se_val(char *b, uint32_t n_samples, uint32_t hz)   /* samples at hz -> "1.234s" */
{
    uint32_t ms = n_samples / hz * 1000u + n_samples % hz * 1000u / hz, l;
    fmt_int(b, (int32_t)(ms / 1000u));
    l = str_len(b);
    b[l] = '.', b[l + 1] = (char)('0' + ms / 100u % 10u), b[l + 2] = (char)('0' + ms / 10u % 10u);
    b[l + 3] = (char)('0' + ms % 10u), b[l + 4] = 's', b[l + 5] = 0;
}
static void rs_secs(char *b, uint32_t tenths)       /* "5.9s" */
{
    uint32_t l;
    fmt_int(b, (int32_t)(tenths / 10u));
    l = str_len(b);
    b[l] = '.', b[l + 1] = (char)('0' + tenths % 10u), b[l + 2] = 's', b[l + 3] = 0;
}
static void se_note(char *b, int32_t n)
{
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    str_cpy(b, NN[n % 12], 3);
    fmt_int(b + str_len(b), n / 12 - 1);
}
static int32_t se_x(uint32_t p, uint32_t n) { return (int32_t)(p * (SE_W - 1u) / (n ? n : 1u)); }
static void se_draw(void)
{
    const smp_user_hdr_t *h = se_hdr();
    char t[24], v[4][12];
    const char *lab[4] = {"", "", "", ""};
    uint32_t pass, c, n, hz = se_nz ? se_hz_of(&h->zone[se_zi]) : 22050u;
    se_zone_t *e = &se_z[se_zi];
    if (!se_shown || ui.force) {
        se_shown = 1;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        se_draw_ok = 0;
    }
    if (se_nz && !se_env_ok)
        se_envelope(), se_draw_ok = 0;
    if (se_draw_ok && !(ui.msg_t && !(ui.frame & 7u)) && !(se_pv.on && !(ui.frame & 1u)))
        return;                                      /* (and every other frame while the preview plays) */                                      /* (drawn again after a change, and while a message shows) */
    se_draw_ok = 1;
    n = se_nz ? h->zone[se_zi].n : 0u;
    for (pass = 0; pass < 2u; pass++) {
        cv_begin(240, pass ? 60u : 120u, C_BLACK);  /* rows 0..179 (the knobs below) */
        cv_oy = -(int32_t)(pass * 120u);
        /* title: slot, name, zone, page */
        str_cpy(t, "USR", sizeof t);
        fmt_int(t + 3, se_k + 1);
        cv_text(4, 2, &FONT_S, t, TE_COL[0]);
        if (se_nz) {
            char nm[9];
            memcpy(nm, h->name, 8), nm[8] = 0;
            cv_text(48, 2, &FONT_S, nm, C_HI);
            str_cpy(t, "Z", sizeof t);
            fmt_int(t + 1, se_zi + 1);
            str_cpy(t + str_len(t), "/", 2);
            fmt_int(t + str_len(t), se_nz);
            cv_text(236 - text_w(&FONT_S, t), 2, &FONT_S, t, C_AMB);
        }
        cv_text(4, 22, &FONT_S, "SAMPLE EDIT", C_GRAY);
        cv_text(236 - text_w(&FONT_S, SE_PAGE_NAME[se_page]), 22, &FONT_S, SE_PAGE_NAME[se_page], C_WHITE);
        if (se_page == SE_PG_RS) {                   /* (1.8) RESAMPLE: what the take will be */
            uint32_t cap = rs_cap_frames(rs_cfg_rate2), bar = rs_bar_frames(1, (uint32_t)song.g[G_BPM]);
            char m[32];
            str_cpy(m, "RECORD THE MIX INTO USR", sizeof m);
            fmt_int(m + str_len(m), se_k + 1);
            cv_text(120 - text_w(&FONT_S, m) / 2, 62, &FONT_S, m, C_WHITE);
            str_cpy(m, "MAX ", sizeof m);
            rs_secs(m + 4, cap * 10u / 44100u);
            str_cpy(m + str_len(m), " = ", sizeof m - str_len(m));
            fmt_int(m + str_len(m), (int32_t)(cap / (bar ? bar : 1u)));
            str_cpy(m + str_len(m), " BARS AT ", sizeof m - str_len(m));
            fmt_int(m + str_len(m), song.g[G_BPM]);
            cv_text(120 - text_w(&FONT_S, m) / 2, 86, &FONT_S, m, C_GRAY);
            if (se_nz) {
                char nm[9];
                memcpy(nm, h->name, 8), nm[8] = 0;
                str_cpy(m, "REPLACES ", sizeof m);
                str_cpy(m + 9, nm, sizeof m - 9);
                cv_text(120 - text_w(&FONT_S, m) / 2, 110, &FONT_S, m, C_AMB);
            }
            cv_text(120 - text_w(&FONT_S, "THEN LFO: STOP  HOLD: CANCEL") / 2, 140, &FONT_S, "THEN LFO: STOP  HOLD: CANCEL",
                    C_DIM);
        } else if (!se_nz) {
            cv_text(120 - text_w(&FONT_S, "EMPTY SLOT") / 2, 100, &FONT_S, "EMPTY SLOT", C_DIM);
            cv_text(120 - text_w(&FONT_S, "PRESETS: ANOTHER") / 2, 124, &FONT_S, "PRESETS: ANOTHER", C_DIM);
        } else {
            int32_t ym = SE_WAVE_Y + SE_WAVE_H / 2, g = 1, xs = se_x(e->st, n), xe = se_x(e->en, n);
            uint32_t pk = 1;
            if (e->norm) {
                for (c = (uint32_t)xs; c <= (uint32_t)xe && c < SE_W; c++) {
                    uint32_t a = (uint32_t)(se_env[c][1] > -se_env[c][0] ? se_env[c][1] : -se_env[c][0]);
                    pk = a > pk ? a : pk;
                }
                g = 117 / (int32_t)pk;
                g = g < 1 ? 1 : g > 16 ? 16 : g;
            }
            cv_rect(0, ym, 240, 1, C_LINE);
            for (c = 0; c < SE_W; c++) {
                int32_t lo = se_env[c][0] * (SE_WAVE_H / 2) / 128, hi = se_env[c][1] * (SE_WAVE_H / 2) / 128;
                int in = (int32_t)c >= xs && (int32_t)c < xe;
                uint16_t col = in ? TE_COL[0] : C_DIM;
                if (in && e->norm)
                    lo = clamp(lo * g, -SE_WAVE_H / 2, SE_WAVE_H / 2), hi = clamp(hi * g, -SE_WAVE_H / 2, SE_WAVE_H / 2);
                if (in && e->fin && (int32_t)c - xs < se_x((uint32_t)e->fin * hz / 1000u, n)) {
                    int32_t f = se_x((uint32_t)e->fin * hz / 1000u, n);
                    lo = lo * ((int32_t)c - xs) / (f ? f : 1), hi = hi * ((int32_t)c - xs) / (f ? f : 1);
                }
                if (in && e->fout && xe - (int32_t)c < se_x((uint32_t)e->fout * hz / 1000u, n)) {
                    int32_t f = se_x((uint32_t)e->fout * hz / 1000u, n);
                    lo = lo * (xe - (int32_t)c) / (f ? f : 1), hi = hi * (xe - (int32_t)c) / (f ? f : 1);
                }
                cv_rect((int32_t)c, ym - hi, 1, hi - lo + 1, col);
            }
            cv_rect(xs, SE_WAVE_Y, 2, SE_WAVE_H, TE_COL[1]);                 /* START */
            cv_rect(xe - 2, SE_WAVE_Y, 2, SE_WAVE_H, TE_COL[2]);             /* END */
            if (se_pv.on) {                          /* the preview's playhead */
                int32_t xp = se_x(e->st + se_pv.pos, n);
                cv_rect(xp, SE_WAVE_Y, 1, SE_WAVE_H, C_WHITE);
            }
            if (e->looped) {
                int32_t a = se_x(e->ls, n), b = se_x(e->le, n);
                cv_rect(a, SE_WAVE_Y - 4, b - a + 1, 3, TE_COL[3]);
                cv_rect(a, SE_WAVE_Y, 1, SE_WAVE_H, TE_COL[3]);
                cv_rect(b, SE_WAVE_Y, 1, SE_WAVE_H, TE_COL[3]);
            }
        }
        cv_oy = 0;
        cv_blit(0, pass * 120u);
    }
    if (!se_nz && se_page != SE_PG_RS)
        return;
    /* the knobs: label, value */
    v[0][0] = v[1][0] = v[2][0] = v[3][0] = 0;
    switch (se_page) {
    case 0:
        lab[0] = "START", lab[1] = "END", lab[2] = "F.IN", lab[3] = "F.OUT";   /* (shown as F.IN, F.OUT: 7 characters a knob) */
        se_val(v[0], e->st, hz), se_val(v[1], e->en, hz);
        fmt_int(v[2], e->fin), str_cpy(v[2] + str_len(v[2]), "ms", 3);
        fmt_int(v[3], e->fout), str_cpy(v[3] + str_len(v[3]), "ms", 3);
        break;
    case 1:
        lab[0] = "MODE", lab[1] = "L.START", lab[2] = "L.END";
        str_cpy(v[0], SE_LOOP_NAME[e->looped % 3u], 12);
        se_val(v[1], e->ls, hz), se_val(v[2], e->le, hz);
        break;
    case 2:
        lab[0] = "ROOT", lab[1] = "LOW", lab[2] = "HIGH";
        se_note(v[0], e->root16 / 16), se_note(v[1], e->lo), se_note(v[2], e->hi);
        break;
    case SE_PG_RS:
        lab[0] = "RATE", lab[1] = "BARS", lab[2] = "START", lab[3] = "SOURCE";
        str_cpy(v[0], rs_cfg_rate2 ? "22k" : "44k", 12);
        if (RS_BARS[rs_cfg_bars % 5u])
            fmt_int(v[1], RS_BARS[rs_cfg_bars % 5u]);
        else
            str_cpy(v[1], "FREE", 12);
        str_cpy(v[2], RS_START_NAME[rs_cfg_start % 3u], 12);
        str_cpy(v[3], rs_cfg_dry ? "DRY" : "MIX", 12);
        break;
    default:
        lab[0] = "REVERSE", lab[1] = "NORMAL.";
        str_cpy(v[0], e->rev ? "ON" : "OFF", 12), str_cpy(v[1], e->norm ? "ON" : "OFF", 12);
        break;
    }
    cv_begin(240, 60, C_BLACK);
    for (c = 0; c < 4u; c++) {
        int32_t x = (int32_t)c * 60 + 30;
        if (!lab[c][0])
            continue;
        cv_text(x - text_w(&FONT_S, lab[c]) / 2, 4, &FONT_S, lab[c], C_GRAY);
        cv_text(x - text_w(&FONT_S, v[c]) / 2, 22, &FONT_S, v[c], VAL(c));
    }
    {
        const char *hint = ui.msg_t ? ui.msg : se_page == SE_PG_RS ? "REC: ARM" : se_pv.on ? "PLAY: STOP" : se_dirty ? "PLAY: HEAR  SAVE: WRITE" : "PLAY: HEAR  LFO: PAGE";
        cv_text(120 - text_w(&FONT_S, hint) / 2, 42, &FONT_S, hint, ui.msg_t ? C_WHITE : C_DIM);
    }
    cv_blit(0, 180);
}

/* (PHREAK 1.8) RESAMPLE's banner over the top rows of whatever screen shows, while a take is armed, recording or
 * waiting for the transport to stop: the slot, the state, the seconds and a meter. ui_draw calls it last */
static void rs_banner(void)
{
    char m[32], b[8];
    uint32_t st = rsm.st, w;
    if (!st || se_on) {
        if (rs_shown) {                              /* gone: the screen under it whole again */
            rs_shown = 0;
            lcd_fill(0, 0, 240, 240, C_BLACK);
            ui.force = 1;
        }
        return;
    }
    rs_shown = 1;
    str_cpy(m, "USR", sizeof m);
    fmt_int(m + 3, rsm.k + 1);
    str_cpy(m + str_len(m), st == RS_ARMED ? (rsm.start == RS_START_NOTE ? " WAITS FOR A NOTE" : " ARMED: PLAY")
                            : st == RS_REC ? " REC " : " STOP TO WRITE", sizeof m - str_len(m));
    if (st == RS_REC) {
        rs_secs(b, rsm.out * 10u / (rsm.rate2 ? 22050u : 44100u));
        str_cpy(m + str_len(m), b, sizeof m - str_len(m));
    }
    cv_begin(240, 20, st == RS_REC ? TE_RED : C_BLACK);
    cv_rect(0, 18, 240, 2, TE_RED);
    cv_text(4, 1, &FONT_S, m, st == RS_REC ? C_BLACK : TE_RED);
    if (st == RS_REC) {                              /* the level, and a full-scale mark once hit */
        w = (uint32_t)rsm.peak * 60u / 32768u;
        cv_rect(174, 6, 62, 7, C_BLACK);
        cv_rect(175, 7, (int32_t)w, 5, rsm.clip ? C_WHITE : TE_G3);
    }
    cv_blit(0, 0);
}
#endif /* SE_CORE_ONLY */
