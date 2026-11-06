/* SPDX-License-Identifier: GPL-3.0-only */
/* The extra drum tracks D2, D3 (after SLOOP 2.8.4; drums.c xd[], drum_focus). The drum track shown is trk[TRK_DRUM]:
 * the screens, the keys, REC, MIDI and the sequencer (seq.c seq_tick) work on it as ever. ALGORITHM past track 4
 * picks D2, D3 (ui_input.c): drum_focus_set swaps the drum track's own part (drums.c XD_P, the pattern, the nudges and
 * conditions, where it stands in it) with the one asked for. The two others play from xd[] (xd_tick): their hits,
 * levels, ratchets, nudges and fill conditions as seq_tick plays them, on the drum voices; no MIDI out, no parameter
 * locks (those are D1's: kept apart while another one is shown) */
static plock_t d1_lock[NLOCK];                  /* D1's locks while D2 or D3 is shown */
static xdrum_t xd_tmp;

static void xd_from(xdrum_t *x, const track_t *t)   /* the drum track's own part of t -> x */
{
    uint32_t i;
    for (i = 0; i < XD_NP; i++)
        x->p[i] = t->p[XD_P[i]];
    for (i = 0; i < XQ_N; i++)                      /* (PHREAK 1.1) its USR kit sound */
        x->q[i] = t->p[XQ_P[i]];
    memcpy(x->dstep, t->dstep, sizeof x->dstep);
    memcpy(x->micro, t->micro, sizeof x->micro);
    memcpy(x->fill, t->fill, sizeof x->fill);
    x->seq_abs = t->seq_abs, x->rat_lanes = t->rat_lanes, x->pass = t->pass;
    x->seq_idx = t->seq_idx, x->seq_den = t->seq_den, x->seq_skip = t->seq_skip;
}

static void xd_to(track_t *t, const xdrum_t *x)
{
    uint32_t i;
    for (i = 0; i < XD_NP; i++)
        t->p[XD_P[i]] = x->p[i];
    for (i = 0; i < XQ_N; i++)
        t->p[XQ_P[i]] = x->q[i];
    memcpy(t->dstep, x->dstep, sizeof x->dstep);
    memcpy(t->micro, x->micro, sizeof x->micro);
    memcpy(t->fill, x->fill, sizeof x->fill);
    t->seq_abs = x->seq_abs, t->rat_lanes = x->rat_lanes, t->pass = x->pass;
    t->seq_idx = x->seq_idx, t->seq_den = x->seq_den, t->seq_skip = x->seq_skip;
}

/* show drum track f (0 D1, 1 D2, 2 D3) on the drum track; the one shown goes to xd[] */
UI_SMALL static void drum_focus_set(uint32_t f)
{
    uint32_t o = drum_focus, r, i;
    f %= 3u;
    if (f == o)
        return;
    r = 3u - o - f;                                 /* the third one: stays in xd[], maybe at another place */
    fm1_irq_off();
    lock_step(TDRUM, NSTEP);                        /* the locks in force: their values back */
    if (!o)
        memcpy(d1_lock, TDRUM->lock, sizeof d1_lock);
    xd_from(&xd_tmp, TDRUM);                        /* the one shown, out */
    xd_to(TDRUM, &xd[xd_slot(f)]);                  /* f, in */
    {
        xdrum_t keep = xd[xd_slot(r)];
        drum_focus = (uint8_t)f;
        xd[xd_slot(o)] = xd_tmp;
        xd[xd_slot(r)] = keep;
    }
    if (!f)
        memcpy(TDRUM->lock, d1_lock, sizeof d1_lock);
    else
        for (i = 0; i < NLOCK; i++)
            TDRUM->lock[i].step = LOCK_FREE, TDRUM->lock[i].param = 0, TDRUM->lock[i].val = 0;
    TDRUM->lk_n = 0;
    undo.valid = 0;                                 /* (its undo was the other drum track's pattern) */
    drums.hits = 0;
    fm1_irq_on();                                   /* (the caller redraws: ui_input.c) */
}

/* D2 and D3 as at power-on: empty, the drum track's values (its defaults, just set), USR1 and USR2 for kits;
 * D1 shown (its data is on the drum track); the three USR kit sounds as new */
UI_SMALL static void xd_defaults(void)
{
    uint32_t k;
    drum_focus = 0;
    drum_snd_defaults(TDRUM->p);                    /* (PHREAK 1.1) D1's USR kit sound too (then D2's, D3's from it) */
    for (k = 0; k < NXD; k++) {
        xdrum_t *x = &xd[k];
        memset(x, 0, sizeof *x);
        xd_from(x, TDRUM);
        memset(x->dstep, 0, sizeof x->dstep);
        memset(x->micro, 0, sizeof x->micro);
        memset(x->fill, 0, sizeof x->fill);
        x->p[XD_KIT] = (int16_t)(DRUM_USR + k);     /* (your samples: LOAD FROM USB, the web editor) */
        x->p[XD_MUTE] = 0;
        x->p[XD_LVL] = GP[G_DRLVL].def;
        x->seq_abs = SEQ_NONE;
        x->seq_idx = 0;
    }
    for (k = 0; k < NLOCK; k++)
        d1_lock[k].step = LOCK_FREE, d1_lock[k].param = 0, d1_lock[k].val = 0;
}

static void xd_reset(void)                          /* (seq.c seq_reset_tracks) from step 0, with the others */
{
    uint32_t k;
    for (k = 0; k < NXD; k++)
        xd[k].seq_abs = SEQ_NONE, xd[k].seq_idx = 0;
}

static uint32_t xd_len(const xdrum_t *x) { return x->p[XD_LEN] > 0 ? (uint32_t)x->p[XD_LEN] : 1u; }
static int32_t xd_micro(const xdrum_t *x, uint32_t abs, uint32_t slen)   /* seq.c micro_units */
{
    return (int32_t)(slen / 64u) * x->micro[abs % xd_len(x) % NSTEP];
}

/* (audio ISR, after seq_tick) the drum tracks not shown, as seq_tick plays the drum track */
static void xd_tick(void)
{
    uint32_t d, l;
    for (d = 0; d < 3u; d++) {
        xdrum_t *x;
        uint32_t len, div, into, slen, abs, nabs, idx, fire = 0, kit, m, quiet;
        int32_t rel;
        const dstep_t *s;
        if (d == drum_focus)
            continue;
        x = &xd[xd_slot(d) % NXD];
        if (!song.playing) {
            x->seq_abs = SEQ_NONE;
            continue;
        }
        len = xd_len(x);
        div = (uint32_t)x->p[XD_DIV] % NDIV_STEP;
        abs = grid_at(div, swings(div) ? swing_units(x->p[XD_SWG] + song.g[G_SWING], div_units(div)) : 0u, &into, &slen);
        if (x->seq_abs != SEQ_NONE && div != x->seq_den)
            x->seq_abs = abs;                       /* DIV changed: the next step of the new grid plays */
        x->seq_den = (uint8_t)div;
        if (x->seq_abs == SEQ_NONE) {
            nabs = abs;
            fire = xd_micro(x, nabs, slen) <= (int32_t)into;
        } else {
            int32_t mu;
            nabs = x->seq_abs + 1u;
            mu = xd_micro(x, nabs, slen);
            if (abs == nabs)
                fire = mu <= (int32_t)into;
            else if (abs + 1u == nabs)
                fire = mu < 0 && (int32_t)slen + mu <= (int32_t)into;
            else if ((int32_t)(abs - nabs) > 0)
                fire = 1;
        }
        kit = (uint32_t)drum_kit_ok(x->p[XD_KIT]);
        quiet = x->p[XD_MUTE] || (song.solo && !((song.solo >> TRK_DRUM) & 1u));
        if (fire) {
            uint32_t c;
            x->seq_abs = nabs;
            idx = nabs % len;
            x->seq_idx = (uint16_t)idx;
            x->rat_lanes = 0;
            c = ((uint32_t)x->fill[(idx % NSTEP) / 4u] >> (2u * (idx % 4u))) & 3u;
            x->seq_skip = (uint8_t)!(c == FC_FILL ? fill_now : c == FC_NOFILL ? !fill_now : 1u);
            s = &x->dstep[idx % NSTEP];
            m = x->seq_skip || quiet ? 0u : dstep_mask(s) & ~(uint32_t)drum_mute;
            for (l = 0; m; l++, m >>= 1)
                if (m & 1u)
                    drum_on_k(LANE_NOTE[l], lvl_vel(dstep_lvl(s, l), 100), kit, d);
        }
        if (x->seq_abs == abs)                      /* the ratchets of the step playing (seq.c seq_ratchets) */
            rel = (int32_t)into - xd_micro(x, abs, slen);
        else if (x->seq_abs == abs + 1u)
            rel = (int32_t)into - ((int32_t)slen + xd_micro(x, abs + 1u, slen));
        else
            rel = (int32_t)into;
        if (x->seq_abs == SEQ_NONE || x->seq_skip || quiet)
            continue;
        s = &x->dstep[x->seq_idx % NSTEP];
        m = dstep_mask(s) & ~(uint32_t)drum_mute;
        for (l = 0; m; l++, m >>= 1) {
            uint32_t hits = 1u + dstep_rat(s, l), h, done;
            if (!(m & 1u) || hits == 1u)
                continue;
            h = (rel < 0 ? 0u : (uint32_t)rel) * hits / slen;
            done = (x->rat_lanes >> (2u * l)) & 3u;
            if (h > done && h < hits) {
                x->rat_lanes = (x->rat_lanes & ~(3u << (2u * l))) | h << (2u * l);
                drum_on_k(LANE_NOTE[l], lvl_vel(dstep_lvl(s, l), 100), kit, d);
            }
        }
    }
}

/* where drum track d's own part is now: on the drum track (the one shown) or in xd[] (project.c: the sections) */
typedef struct {
    int16_t *p[XD_NP];
    dstep_t *dstep;
    int8_t *micro;
    uint8_t *fill;
} xd_ref_t;
static void xd_ref(uint32_t d, xd_ref_t *r)
{
    uint32_t i;
    for (i = 0; i < XD_NP; i++)
        r->p[i] = xd_par(d, i);
    if (d == drum_focus) {
        r->dstep = TDRUM->dstep, r->micro = TDRUM->micro, r->fill = TDRUM->fill;
    } else {
        xdrum_t *x = &xd[xd_slot(d) % NXD];
        r->dstep = x->dstep, r->micro = x->micro, r->fill = x->fill;
    }
}
