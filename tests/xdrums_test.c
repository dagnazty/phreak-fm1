/* The extra drum tracks D2, D3 (firmware/src/xdrums.c, drums.c xd[], project.c xd_ext_t): played while not shown,
 * on their own kit, muted, shown with ALGORITHM (drum_focus_set: the patterns and D1's locks kept apart and back),
 * swapped while the pattern plays, and kept with the sections (D1 stays the project's drum track whatever is shown).
 * Exit status: the failures */
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
static void check(int ok, const char *what)
{
    printf("xdrums: %-86s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t seen[NDRUM];
/* blocks for n ms; the hits of each drum track (a voice taken anew: its age moved) */
static void run(uint32_t ms, uint32_t hits[3])
{
    uint32_t b, i;
    int32_t out[CTL * 2];
    for (b = 0; b < ms * (FS / 1000u) / CTL; b++) {
        mix_block(out, CTL);
        for (i = 0; i < NDRUM; i++)
            if (drums.v[i].active && drums.v[i].age != seen[i]) {
                seen[i] = drums.v[i].age;
                hits[drums.src[i] % 3u]++;
            }
    }
}
static void pattern(dstep_t *d, uint32_t lane, uint32_t every)   /* lane on every n-th step of 16 */
{
    uint32_t k;
    memset(d, 0, sizeof(dstep_t) * NSTEP);
    for (k = 0; k < 16u; k += every)
        dstep_set(&d[k], lane, LV_NORM, 0);
}

int main(void)
{
    uint32_t h[3], bar_ms, i;
    host_tracks_init();
    xd_defaults();
    song.g[G_BPM] = 120;
    bar_ms = 2000;                                     /* 4 beats at 120 */
    TDRUM->p[P_SLEN] = 16, TDRUM->p[P_SDIV] = 2, TDRUM->p[P_MUTE] = 0;   /* 1/16 */
    pattern(TDRUM->dstep, 0, 4);                       /* D1: KICK on the beats */
    check(drum_focus == 0u && xd[0].p[XD_KIT] == (int16_t)DRUM_USR && xd[1].p[XD_KIT] == (int16_t)(DRUM_USR + 1u),
          "power-on: D1 shown, D2 on USR1, D3 on USR2");
    xd[0].p[XD_KIT] = (int16_t)DRUM_SAMPLED, xd[0].p[XD_LEN] = 16, xd[0].p[XD_DIV] = 2;   /* D2: the 808, snare 8ths */
    pattern(xd[0].dstep, 2, 2);
    xd[1].p[XD_KIT] = (int16_t)DRUM_SAMPLED, xd[1].p[XD_LEN] = 16, xd[1].p[XD_DIV] = 2;   /* D3: hat 16ths */
    pattern(xd[1].dstep, 4, 1);
    transport_req = 1;
    memset(h, 0, sizeof h);
    run(bar_ms * 2u, h);
    printf("xdrums: 2 bars: D1 %u, D2 %u, D3 %u hits\n", (unsigned)h[0], (unsigned)h[1], (unsigned)h[2]);
    check(h[0] >= 7u && h[0] <= 9u && h[1] >= 15u && h[1] <= 17u && h[2] >= 30u && h[2] <= 33u,
          "playing: D1 (shown) 4 a bar, D2 8, D3 16, each on the voices as its own");
    xd[0].p[XD_MUTE] = 1;
    memset(h, 0, sizeof h);
    run(bar_ms, h);
    check(h[1] == 0u && h[0] >= 3u && h[2] >= 15u, "D2 muted: none of its hits, the others as before");
    xd[0].p[XD_MUTE] = 0;
    {   /* show D2 while it plays: its pattern on the drum track, D1's in xd[], the hits go on */
        dstep_t d1 = TDRUM->dstep[0];
        TDRUM->lock[0].step = 3, TDRUM->lock[0].param = P_PAN, TDRUM->lock[0].val = 20;   /* a lock of D1 */
        drum_focus_set(1);
        check(drum_focus == 1u && dstep_has(&TDRUM->dstep[0], 2) && !dstep_has(&TDRUM->dstep[0], 0) &&
              dstep_has(&xd[0].dstep[0], 0) && !memcmp(&xd[0].dstep[0], &d1, sizeof d1) && dstep_has(&xd[1].dstep[1], 4) &&
              TDRUM->lock[0].step == LOCK_FREE, "show D2: its pattern on the drum track, D1 and D3 in xd[], D1's lock kept apart");
        memset(h, 0, sizeof h);
        run(bar_ms, h);
        check(h[0] >= 3u && h[0] <= 5u && h[1] >= 7u && h[1] <= 9u && h[2] >= 15u && h[2] <= 17u,
              "D2 shown while playing: every drum track goes on as before");
        drum_focus_set(2);
        check(drum_focus == 2u && dstep_has(&TDRUM->dstep[1], 4) && dstep_has(&xd[0].dstep[0], 0) && dstep_has(&xd[1].dstep[0], 2),
              "show D3: D1, D2 in xd[] in order");
        drum_focus_set(0);
        check(!memcmp(&TDRUM->dstep[0], &d1, sizeof d1) && TDRUM->lock[0].step == 3u && TDRUM->lock[0].val == 20 &&
              dstep_has(&xd[0].dstep[0], 2) && dstep_has(&xd[1].dstep[1], 4), "D1 again: its pattern and its lock back");
        TDRUM->lock[0].step = LOCK_FREE;
    }
    {   /* a section keeps D2, D3; the project's drum track is D1, whatever is shown */
        static project_t p;
        static xd_ext_t x;
        drum_focus_set(2);                              /* D3 shown */
        proj_capture(&p);
        xd_capture(&x);
        check(dstep_has((const dstep_t *)&p.t[TRK_DRUM].step[0], 0) && !dstep_has((const dstep_t *)&p.t[TRK_DRUM].step[1], 4) &&
              x.magic == XD_MAGIC2 && dstep_has(&x.d[0].dstep[0], 2) && dstep_has(&x.d[1].dstep[3], 4) &&
              x.d[0].p[XD_KIT] == (int16_t)DRUM_SAMPLED, "capture with D3 shown: D1 in the project, D2 and D3 in its record");
        for (i = 0; i < NXD; i++)
            memset(xd[i].dstep, 0, sizeof xd[i].dstep);
        memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
        fm1_irq_off();
        proj_apply(&p, 1);
        xd_apply(&x);
        fm1_irq_on();
        check(drum_focus == 2u && dstep_has(&TDRUM->dstep[1], 4) && dstep_has(&xd[0].dstep[0], 0) && dstep_has(&xd[1].dstep[0], 2),
              "apply with D3 shown: D3 still shown, D1 and D2 back in place");
        xd_apply(0);
        check(!dstep_mask(&TDRUM->dstep[1]) && !dstep_mask(&xd[1].dstep[0]) && TDRUM->p[P_E0] == (int16_t)(DRUM_USR + 1u) &&
              dstep_has(&xd[0].dstep[0], 0), "a section without them: D2, D3 empty (USR1, USR2), D1 kept");
        drum_focus_set(0);
    }
    printf("xdrums: %s\n", fails ? "FAILED" : "all ok");
    return fails;
}
