/* SPDX-License-Identifier: GPL-3.0-only */
/* PHREAK 1.1: a USR kit's sound (drums.c XQ_P, drum_usr_voice): the ENV, LFO and EDIT pages of a drum track whose kit
 * is USR1..4 shape its hits. Through hostsim.c, a slot in a RAM image of the flash (as tests/userkit_test.c):
 *   as new, nothing is used: a hit plays as before (drums_mix's own loop)
 *   SUS 0 and a short DECAY end a hit early; GATE then a short release end it at the step's GATE
 *   CUT low takes the edges off; TUNE +12 plays an octave up; LOOP plays a short sample while the hit holds
 *   D2 has its own sound; ALGORITHM (drum_focus_set) shows it and puts D1's back
 *   the pages open with a USR kit only; a project keeps D1's sound, one saved before gets it as new;
 *   a section's record keeps D2's and D3's, PHREAK 1.0's record (XDR1) gives them as new */
#include <stdint.h>
static uint32_t host_slots[6u * 0x14000u / 4u];
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? i : 0; }
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;
#include "../firmware/src/arranger_scene.c"

static int fails;
static void check(int ok, const char *what) { printf("usrsound: %-84s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

static void slot_build(uint32_t k, const uint8_t *notes, const uint32_t *lens, uint32_t nz)
{
    uint8_t *img = (uint8_t *)host_slots + k * SMP_USER_SIZE, *d = img + SMP_USER_DATA;
    smp_user_hdr_t *h = (smp_user_hdr_t *)img;
    uint32_t i, off = 0, j;
    memset(img, 0xFF, SMP_USER_SIZE);
    memset(h, 0, sizeof *h);
    h->magic = SMP_USER_MAGIC;
    h->version = 1;
    h->nz = (uint8_t)nz;
    memcpy(h->name, "KIT", 4);
    for (i = 0; i < nz; i++) {
        smp_zone_t *z = &h->zone[i];
        uint32_t bytes = (lens[i] + 1u) / 2u;
        for (j = 0; j < bytes; j++)
            d[off + j] = (j / 8u) & 1u ? 0xFFu : 0x77u;  /* a square-ish wave: edges a low-pass rounds */
        z->off = off; z->n = lens[i]; z->ls = 0; z->le = lens[i] - 1u;
        z->rate = 0x8000u;
        z->root16 = (int16_t)(notes[i] * 16);
        z->pred = 0; z->idx = 0; z->lo = notes[i]; z->hi = notes[i]; z->looped = 0;
        off += bytes;
    }
    h->data_len = off;
    smp_user_scan(k);
}
static void quiet(void)
{
    static int32_t o[CTL * 2];
    uint32_t i;
    for (i = 0; i < NDRUM; i++)
        drums.v[i].active = 0;
    drums.tail = 0;
    for (i = 0; i < FS / CTL; i++)
        mix_block(o, CTL);
}
/* ms of the mix: its peak; *alive: a drum voice still sounding at the end */
static int32_t run(uint32_t ms, int *alive)
{
    static int32_t o[CTL * 2];
    int32_t pk = 0;
    uint32_t b, i;
    for (b = 0; b < ms * (FS / 1000u) / CTL; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL * 2u; i++) {
            int32_t a = o[i] < 0 ? -o[i] : o[i];
            if (a > pk) pk = a;
        }
    }
    if (alive) {
        *alive = 0;
        for (i = 0; i < NDRUM; i++)
            *alive |= drums.v[i].active;
    }
    return pk;
}
/* a hit of the open hat (16000 samples, ~0.73 s) on the drum track shown, then ms: the peak */
static int32_t hit(uint32_t note, uint32_t ms, int *alive)
{
    quiet();
    fm1_irq_off();
    drum_on(note, 100);
    fm1_irq_on();
    return run(ms, alive);
}
static void snd_new(void) { drum_snd_defaults(TDRUM->p); TDRUM->p[P_SGATE] = TP[P_SGATE].def; }

int main(void)
{
    static const uint8_t notes[3] = {36, 42, 46};       /* KICK, HAT, OPEN HAT */
    static const uint32_t lens[3] = {8000, 2000, 16000};
    int32_t open, low, pk;
    int alive;
    host_tracks_init();
    song.g[G_DRREV] = 0;
    song.g[G_DRDLY] = 0;
    slot_build(0, notes, lens, 3);
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    xd_defaults();
    TDRUM->p[P_E0] = (int16_t)DRUM_USR;                 /* USR1 */
    TDRUM->p[P_SDIV] = 2;                               /* 1/16: a step 125 ms at 120 */
    {
        dusr_t s;
        drum_snd_block(0, &s);
        check(!s.env && !s.flt && !s.pit && !s.col && TDRUM->p[DS_MARK] == 1 && TDRUM->p[DS_CUT] == 127,
              "as new: no envelope, filter, pitch or colour: a hit plays as any sampled hit");
    }
    open = hit(46, 100, &alive);
    check(open > 1000 && alive, "as new: the open hat sounds and still rings after 100 ms");
    TDRUM->p[P_SUS] = 0, TDRUM->p[P_DEC] = 20;
    pk = hit(46, 300, &alive);
    check(!alive, "SUS 0, a short DECAY: the hit is over within 300 ms");
    snd_new();
    TDRUM->p[P_REL] = 0, TDRUM->p[P_SGATE] = 32;        /* GATE a quarter step (31 ms), a short release */
    hit(46, 15, &alive);
    check(alive, "GATE 32, REL 0: still sounding inside its gate (15 ms)");
    run(85, &alive);
    check(!alive, "GATE 32, REL 0: over after its gate and release (100 ms)");
    snd_new();
    TDRUM->p[DS_CUT] = 10;
    low = hit(46, 100, 0);
    printf("usrsound: open %d, CUT 10 %d\n", (int)open, (int)low);
    check(low < open * 6 / 10, "CUT 10 (LP): the square's edges rounded, its peak well down");
    TDRUM->p[P_FTYPE] = 2;
    pk = hit(46, 100, 0);
    check(pk > 0 && pk != low, "TYPE HP: another sound");
    snd_new();
    TDRUM->p[DS_CUT] = 40, TDRUM->p[P_LD_FLT] = 63, TDRUM->p[P_LRATE] = 100;
    pk = hit(46, 100, 0);
    TDRUM->p[P_LD_FLT] = 0;
    check(pk != hit(46, 100, 0), "LFO > FLT: the filter moves");
    snd_new();
    TDRUM->p[DS_TUNE] = 12;
    quiet();
    drum_on(46, 100);
    {
        uint32_t i, st = 0;
        for (i = 0; i < NDRUM; i++)
            if (drums.v[i].active)
                st = (uint32_t)drums.v[i].s[5];
        TDRUM->p[DS_TUNE] = 0;
        quiet();
        drum_on(46, 100);
        for (i = 0; i < NDRUM; i++)
            if (drums.v[i].active)
                check(st > (uint32_t)drums.v[i].s[5] * 19u / 10u && st < (uint32_t)drums.v[i].s[5] * 21u / 10u,
                      "TUNE +12: twice the speed");
    }
    snd_new();
    TDRUM->p[P_SDIV] = 7;                               /* 1BAR: a long gate */
    hit(42, 200, &alive);
    check(!alive, "LOOP OFF: the hat (90 ms) is over at 200 ms");
    TDRUM->p[DS_LOOP] = 1;
    hit(42, 200, &alive);
    check(alive, "LOOP ON: the hat plays again while the hit holds (200 ms)");
    TDRUM->p[P_SDIV] = 2;
    snd_new();
    {   /* D2: its own sound, shown with ALGORITHM, D1's back after */
        dusr_t s1, s2;
        xd[0].q[XQ_CUT] = 30;
        xd[0].p[XD_KIT] = (int16_t)DRUM_USR;
        drum_snd_block(0, &s1);
        drum_snd_block(1, &s2);
        check(!s1.flt && s2.flt, "D2's own CUT: its hits filtered, D1's not");
        drum_focus_set(1);
        check(TDRUM->p[DS_CUT] == 30 && xd[0].q[XQ_CUT] == 127, "D2 shown: its CUT on the drum track, D1's in xd[]");
        drum_focus_set(0);
        check(TDRUM->p[DS_CUT] == 127 && xd[0].q[XQ_CUT] == 30, "D1 again: its own back");
    }
    {   /* the pages */
        const page_t *env = 0, *ed1 = 0, *flt = 0, *voi = 0;
        uint32_t i;
        int16_t *vp;
        for (i = 0; i < NPAGES; i++) {
            if (str_eq(PAGES[i].title, "ENV")) env = &PAGES[i];
            if (str_eq(PAGES[i].title, "EDIT 1")) ed1 = &PAGES[i];
            if (PAGES[i].id[0] == P_FTYPE) flt = &PAGES[i];
            if (str_eq(PAGES[i].title, "VOICE")) voi = &PAGES[i];
        }
        song.sel = TRK_DRUM;
        check(page_for_drum(env) && page_for_drum(ed1) && page_shown(flt) && !page_shown(voi),
              "USR kit: ENV, EDIT 1 and FILTER open on the drum track, VOICE does not");
        check(page_desc(ed1, 1, &vp) == &DS_DESC[0] && vp == &TDRUM->p[DS_TUNE], "EDIT 1 KNOB 2: TUNE, in its own slot");
        TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
        check(!page_for_drum(env) && !page_shown(flt), "the 808: ENV and FILTER stay closed (DRUM TRACK)");
        TDRUM->p[P_E0] = (int16_t)DRUM_USR;
        song.sel = 0;
    }
    {   /* a project keeps D1's sound; one from before gets it as new */
        static project_t p;
        TDRUM->p[DS_CUT] = 44, TDRUM->p[DS_TUNE] = -5, TDRUM->p[P_ATK] = 9;
        proj_capture(&p);
        snd_new();
        fm1_irq_off();
        proj_apply(&p, 1);
        fm1_irq_on();
        check(TDRUM->p[DS_CUT] == 44 && TDRUM->p[DS_TUNE] == -5 && TDRUM->p[P_ATK] == 9, "a project keeps D1's sound");
        pt_set(&p.t[TRK_DRUM], DS_MARK, 0);             /* as saved before PHREAK 1.1 */
        pt_set(&p.t[TRK_DRUM], DS_CUT, 0);
        fm1_irq_off();
        proj_apply(&p, 1);
        fm1_irq_on();
        check(TDRUM->p[DS_CUT] == 127 && TDRUM->p[P_ATK] == 0 && TDRUM->p[DS_MARK] == 1, "saved before: D1's sound as new");
    }
    {   /* a section's record: D2's sound kept; PHREAK 1.0's record: as new */
        static xd_ext_t x;
        xd[0].q[XQ_CUT] = 30;
        xd_capture(&x);
        xd[0].q[XQ_CUT] = 127;
        fm1_irq_off();
        xd_apply(&x);
        fm1_irq_on();
        check(x.magic == XD_MAGIC2 && xd[0].q[XQ_CUT] == 30, "a section keeps D2's sound");
        x.magic = XD_MAGIC;
        fm1_irq_off();
        xd_apply(&x);
        fm1_irq_on();
        check(xd[0].q[XQ_CUT] == 127 && xd[0].q[XQ_MARK] == 1 && dstep_mask(&xd[0].dstep[0]) == dstep_mask(&x.d[0].dstep[0]),
              "PHREAK 1.0's record: its patterns, the sound as new");
    }
    (void)pk;
    printf("usrsound: %s\n", fails ? "FAILED" : "all ok");
    return fails;
}
