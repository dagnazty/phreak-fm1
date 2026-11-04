/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* GM drum part (track 4): the SAMPLE engine's KIT set (General MIDI percussion map,
 * tools/gen_samples.py GM_KIT), played by its step pattern, the keys when track 4
 * is selected, and its own MIDI channel (GLO -> DRUMS, default 10). Its own voices
 * (outside the parts' voice budget). One-shots: note-offs are ignored; a closed or
 * pedal hi-hat chokes the open one. LEVEL / REV / DLY: GLO > DRUMS (G_DRLVL, G_DRREV, G_DRDLY);
 * PAN and MUTE: the drum track's P_PAN / P_MUTE. Rendered from the audio ISR. */
#ifndef NDRUM                    /* (a host may give the drums more voices: Kraken) */
#define NDRUM 6
#endif
/* a host's own treatment of each drum voice before the mix (Kraken: a level, a pan, a filter per sound);
 * on the FM-1, nothing */
#ifndef DRUM_VOICE_FX
#define DRUM_VOICE_FX(k, s) (s)
#define DRUM_VOICE_GL(k, g) (g)
#define DRUM_VOICE_GR(k, g) (g)
#endif
#include "drum_synth.c"       /* synthesised kits (DS_KITS) */
/* P_E0 was unused on the drum track: it holds the kit. 0..4: the GM sample kit and its four
 * treatments (as before: old projects keep their kit), 5..: the synthesised kits, then (2.4) USR1..USR4:
 * a user sample slot as a kit (the web editor's DRUM KIT: a sound per lane, zone lo..hi = the lane's note),
 * and USR3+4: one kit over the slots USR3 and USR4 (a lane's sound in either) */
#define DRUM_SAMPLED 5u
#define DRUM_USR (DRUM_SAMPLED + DS_NKITS)
#define DRUM_PAIR (DRUM_USR + SMP_USER_SLOTS)   /* USR3+4: one kit in two slots (the editor splits it), ~15 s */
#define DRUM_SYN (DRUM_PAIR + 1u)                /* SLOOP 2.5: SYN1..SYN4, your synthesised kits (drum_synth.c dsu) */
#define DRUM_KITS (DRUM_SYN + DSU_N)

/* the extra drum tracks D2, D3 (after SLOOP 2.8.4). The one shown (drum_focus: 0 D1, 1 D2, 2 D3) lives on the drum
 * track, trk[TRK_DRUM]: every screen, the keys, REC, MIDI and the sequencer see it as ever (ALGORITHM picks it,
 * xdrums.c drum_focus_set swaps them). The two others wait in xd[] (D1, D2, D3 in order, without the one shown) and
 * play from there (xdrums.c xd_tick), on the drum voices: each voice keeps the drum track it plays for (drums.src),
 * its LEVEL and PAN. A drum track's own values (XD_P): its kit, length, DIV, swing, mute, pan and level (D1: GLO >
 * DRUMS LEVEL, G_DRLVL, as ever; D2, D3: their P_LEVEL); the INSERT, the filter and the sends stay the drum track's,
 * for all three */
#define NXD 2u
#define XD_NP 7u
enum { XD_KIT, XD_LEN, XD_DIV, XD_SWG, XD_MUTE, XD_PAN, XD_LVL };
static const uint8_t XD_P[XD_NP] = {P_E0, P_SLEN, P_SDIV, P_SSWING, P_MUTE, P_PAN, P_LEVEL};

/* (PHREAK 1.1) the USR kits' sound: on a drum track whose kit is USR1..USR4 or USR3+4, the ENV, LFO and EDIT pages
 * (EDIT 1 SAMPLE: KIT TUNE BITS LOOP, EDIT 2 TONE: CUT RES DRV, FILTER: TYPE CUT RES) shape every hit, as the SAMPLE
 * engine and ANALOG's filter do a note: an ADSR held for the step's GATE (SEQ > PATTERN), a resonant LP / BP / HP
 * (dsp.c tsvf, eng_analog.c analog_tap) the envelope and the LFO move, the LFO on pitch, filter and level, TUNE, BITS,
 * DRV, and LOOP (the sample again from its start while the hit holds). The drum track's own slots hold them: ENV, LFO,
 * FTYPE and GATE as on a synth track; TUNE BITS LOOP CUT RES DRV in the VOICE page's slots, which a drum track never
 * used (DS_*; drum_snd_desc gives their ranges), and DS_MARK says they are set (a project from before: drum_snd_defaults).
 * Each drum track has its own (D2, D3 in xd[].q). With every value at its default a hit plays as before, bit for bit */
#define DS_TUNE P_GLIDE
#define DS_BITS P_GLMODE
#define DS_LOOP P_PRIO
#define DS_CUT P_ALLOC
#define DS_RES P_DETUNE
#define DS_DRV P_VOICE
#define DS_MARK P_VLEAD
#define XQ_N 25u
enum { XQ_ATK, XQ_DEC, XQ_SUS, XQ_REL, XQ_EFLT, XQ_EPIT, XQ_LRATE, XQ_LWAVE, XQ_LPHS, XQ_LFADE, XQ_LSYNC, XQ_LTRIG, XQ_LPOL,
       XQ_LPIT, XQ_LFLT, XQ_LAMP, XQ_FTYPE, XQ_TUNE, XQ_BITS, XQ_LOOP, XQ_CUT, XQ_RES, XQ_DRV, XQ_GATE, XQ_MARK };
static const uint8_t XQ_P[XQ_N] = {P_ATK, P_DEC, P_SUS, P_REL, P_ED_FLT, P_ED_PIT, P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
                                   P_LSYNC, P_LTRIG, P_LPOL, P_LD_PIT, P_LD_FLT, P_LD_AMP, P_FTYPE, DS_TUNE, DS_BITS,
                                   DS_LOOP, DS_CUT, DS_RES, DS_DRV, P_SGATE, DS_MARK};
static const char *const DS_ONOFF[] = {"OFF", "ON"};
static const param_desc_t DS_DESC[6] = {
    {"TUNE", F_SEMI, -24, 24, 0, 0, 0}, {"BITS", F_INT, 0, 127, 0, 0, 0}, {"LOOP", F_ENUM, 0, 1, 0, DS_ONOFF, 0},
    {"CUT", F_INT, 0, 127, 127, 0, 0},  {"RES", F_INT, 0, 127, 0, 0, 0},  {"DRV", F_PCT, 0, 127, 0, 0, 0}};
static const param_desc_t DS_MARK_DESC = {"-", F_INT, 0, 1, 1, 0, 0};
/* the range of a drum track's slot id that holds a USR kit value (DS_*), else 0 */
static const param_desc_t *drum_snd_desc(uint32_t id)
{
    switch (id) {
    case DS_TUNE: return &DS_DESC[0];
    case DS_BITS: return &DS_DESC[1];
    case DS_LOOP: return &DS_DESC[2];
    case DS_CUT: return &DS_DESC[3];
    case DS_RES: return &DS_DESC[4];
    case DS_DRV: return &DS_DESC[5];
    case DS_MARK: return &DS_MARK_DESC;
    default: return 0;
    }
}
/* a drum track's p[]: its USR kit sound as a new one has it (as before: no envelope, open, no LFO) */
static void drum_snd_defaults(int16_t *p)
{
    static const uint8_t Z[] = {P_ATK, P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ESYNC, P_LWAVE, P_LPHASE, P_LFADE, P_LSYNC, P_LTRIG,
                                P_LPOL, P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP, P_FTYPE, DS_TUNE, DS_BITS, DS_LOOP, DS_RES,
                                DS_DRV};
    uint32_t i;
    for (i = 0; i < sizeof Z; i++)
        p[Z[i]] = 0;
    p[P_DEC] = 70, p[P_SUS] = 127, p[P_REL] = 127, p[P_LRATE] = 60, p[DS_CUT] = 127, p[DS_MARK] = 1;
}
static int drum_usr(uint32_t kit) { return kit >= DRUM_USR && kit < DRUM_SYN; }
typedef struct {
    int16_t p[XD_NP];
    int16_t q[XQ_N];                    /* (PHREAK 1.1) its USR kit sound (XQ_P) */
    dstep_t dstep[NSTEP];
    int8_t micro[NSTEP];
    uint8_t fill[NSTEP / 4];
    uint32_t seq_abs, rat_lanes, pass;   /* (as the track's: seq.c seq_tick) */
    uint16_t seq_idx;
    uint8_t seq_den, seq_skip;
} xdrum_t;
static xdrum_t xd[NXD];
static uint8_t drum_focus;              /* the drum track shown: 0 D1, 1 D2, 2 D3 */
static uint8_t drum_src;                /* (audio ISR) the drum track the next voice plays for */
static uint32_t xd_slot(uint32_t d) { return d < drum_focus ? d : d - 1u; }   /* d != drum_focus: its place in xd[] */
static int16_t *xd_par(uint32_t d, uint32_t i)                  /* value i (XD_*) of drum track d, wherever it is */
{
    return d == drum_focus ? &trk[TRK_DRUM].p[XD_P[i]] : &xd[xd_slot(d) % NXD].p[i];
}
static int16_t *xq_par(uint32_t d, uint32_t i)                  /* its USR kit sound's value i (XQ_*) */
{
    return d == drum_focus ? &trk[TRK_DRUM].p[XQ_P[i]] : &xd[xd_slot(d) % NXD].q[i];
}
static int32_t drum_level_of(uint32_t d) { return d ? *xd_par(d, XD_LVL) : song.g[G_DRLVL]; }
/* the LEVEL of the drum track shown (the screens' drum level): D1 GLO > DRUMS LEVEL, D2 / D3 their own */
static int16_t *drum_lvl_ptr(void) { return drum_focus ? &trk[TRK_DRUM].p[P_LEVEL] : &song.g[G_DRLVL]; }
static void xd_tick(void);              /* (xdrums.c) */
static void xd_reset(void);
static const char *const DRUM_KIT_NAMES[] = {"ACOUSTIC", "DEEP", "TIGHT", "BRIGHT", "DUST", DS_KIT_NAME_LIST,
                                             "USR1", "USR2", "USR3", "USR4", "USR3+4",
                                             "SYN1", "SYN2", "SYN3", "SYN4"};
static const char *const DRUM_KIT_STYLES[] = {"STUDIO", "SOFT", "PUNCHY", "BRIGHT", "DUSTY", DS_KIT_STYLE_LIST,
                                              "YOUR KIT", "YOUR KIT", "YOUR KIT", "YOUR KIT", "BIG KIT",
                                              "YOUR SYNTH", "YOUR SYNTH", "YOUR SYNTH", "YOUR SYNTH"};
_Static_assert(sizeof(DRUM_KIT_NAMES) / sizeof(DRUM_KIT_NAMES[0]) == DRUM_KITS, "a name per kit");
static uint32_t drum_kit(void) { return (uint32_t)clamp(TDRUM->p[P_E0], 0, DRUM_KITS - 1); }
#define DRUM_DEFAULT_KIT DRUM_SAMPLED   /* power-on: 808 */

static struct {
    voice_t v[NDRUM];
    uint32_t age;
    int16_t set;                 /* SMP_SETS index of "PERC" (GM map), -1 = none */
    int32_t tail;                /* declick: the last output of cut voices, decaying */
    int32_t peak;                /* largest |output| since the UI last looked (TRACKS meter) */
    uint8_t kit[NDRUM];
    int32_t filter[NDRUM], env[NDRUM], denv[NDRUM];   /* denv (2.7): the DECAY's fade, Q15 */
    uint16_t dk[NDRUM];          /* (2.7) its factor a sample (DECAY_K), 0: none */
    uint8_t synth[NDRUM];        /* the voice plays a synthesised kit (ds[]) */
    dsv_t ds[NDRUM];
    volatile uint16_t hits;      /* bit per lane hit since the UI last looked (pads, key LEDs) */
    volatile uint8_t kick;       /* a kick was hit (fx.c DUCK) */
    int32_t a0, a1;              /* the drum track's mute / solo attenuation over this block (fx.c), Q15, 0 = heard */
    int32_t glv[3], ggl[3], ggr[3];   /* (2.7) LEVEL and PAN as last mixed: a change glides (~3 ms, fx.c mix_glide);
                                       * one each for D1, D2, D3 */
    uint8_t gon[3];
    uint8_t src[NDRUM];          /* the drum track the voice plays for: 0 D1, 1 D2, 2 D3 */
    uint8_t usr[NDRUM];          /* (PHREAK 1.1) a USR kit's hit: its drum track's sound shapes it (drum_usr_voice) */
    int32_t ic1[NDRUM], ic2[NDRUM];   /* its filter */
    uint32_t hold[NDRUM];        /* samples until its GATE ends (the release) */
    struct { uint32_t ph, rnd; int32_t fade; } lfo[3];   /* each drum track's LFO */
} drums = {.set = -2};

static int32_t ds_buf[CTL];

/* (2.7) LEVEL and PAN glide: a knob's detent (or a new sound's trim) moves a quarter of the way each block of CTL
 * samples (0.73 ms), ~3 ms in all, instead of a step a detent (after JIANT FM 0.6.8, github.com/juanjiant-bit/JIANT:
 * "LEVEL and PAN glide (~3 ms) instead of stepping at each knob move"). The last few units: at once */
static int32_t mix_glide(int32_t from, int32_t to)
{
    int32_t d = to - from;
    return d <= 8 && d >= -8 ? to : from + d / 4;
}

static int32_t drum_set(void)
{
    uint32_t i;
    if (drums.set == -2) {
        drums.set = -1;
        for (i = 0; i < SMP_NSETS; i++)
            if (str_eq(SMP_SETS[i].name, "PERC"))
                drums.set = (int16_t)i;
    }
    return drums.set;
}

/* ------------------------------------------------------------ lanes --- */
/* The drum track's 16 sounds, one per white key, F3 (kick) .. G5 (cowbell): kicks, snare and clap,
 * the hi-hats, rim and a second snare, the toms, the cymbals, the percussion. Each plays a GM note
 * (the sampled kits: their samples; the synthesised kits: drum_synth.c DS_MAP). A black key plays
 * the lane of the white key left of it (two fingers on one sound). */
static const uint8_t LANE_NOTE[DRUM_LANES] = {36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56};
static const char *const LANE_NAME[DRUM_LANES] = {
    "KICK", "KICK 2", "SNARE", "CLAP", "HAT", "OPEN HAT", "PEDAL", "RIM",
    "SNARE 2", "LOW TOM", "HI TOM", "CRASH", "RIDE", "SHAKER", "CONGA", "COWBELL"};
static const char *const LANE_SHORT[DRUM_LANES] = {           /* 5 characters: tiles, dials */
    "kick", "kick2", "snare", "clap", "hat", "open", "pedal", "rim",
    "snr 2", "tom l", "tom h", "crash", "ride", "shake", "conga", "bell"};
/* a GM note (MIDI in, old projects) -> its lane: the nearest sound of the 16 (35..81; below: kick, above: shaker) */
static const uint8_t LANE_OF_GM[81 - 35 + 1] = {
    /* 35 */ 1, 0, 7, 2, 3, 8, 9, 4, 9, 6,
    /* 45 */ 9, 5, 10, 10, 11, 10, 12, 11, 12, 13,
    /* 55 */ 11, 15, 11, 13, 12, 14, 14, 14, 14, 14,
    /* 65 */ 14, 14, 15, 15, 13, 13, 15, 15, 13, 13,
    /* 75 */ 7, 7, 7, 14, 14, 15, 15};
/* SLOOP 2.7, the DRUMS screen's SOUND page: each of the 16 sounds' TUNE (-8 .. +7 semitones) and DECAY (-4 .. +3,
 * 0 as the kit has it), on every kit; kept with the project and each section (project.c: in the drum track's free
 * engine values) */
#define DRUM_TUNE_MIN (-8)
#define DRUM_TUNE_MAX 7
#define DRUM_DEC_MIN (-4)
#define DRUM_DEC_MAX 3
static int8_t drum_tune[DRUM_LANES], drum_dec[DRUM_LANES];
static const uint8_t DK_SMP[4] = {47, 60, 74, 87};   /* a sample's DECAY -4 .. -1: -60 dB in 60, 120, 250, 500 ms */
#define DRUM_CLICK(n) ((n) == 76u || (n) == 77u)    /* (the click's wood block: no lane, never tuned) */
static uint32_t lane_of_note(uint32_t note);
static int drum_tweaked(void)
{
    uint32_t l;
    for (l = 0; l < DRUM_LANES; l++)
        if (drum_tune[l] || drum_dec[l])
            return 1;
    return 0;
}
static uint32_t lane_of_note(uint32_t note)
{
    return note < 35u ? 0u : note > 81u ? 13u : LANE_OF_GM[note - 35u];
}
/* the lane of key k (0 = F3 .. 26 = G5): white keys in order, a black key the white key left of it */
static uint32_t lane_of_key(uint32_t k)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* from F */
    int32_t i = W[k % 12u];
    if (i < 0)
        i = W[(k - 1u) % 12u];
    return (uint32_t)((int32_t)(k / 12u) * 7 + i) & 15u;
}
/* the white key of lane l (key index), for the key LEDs */
static uint32_t key_of_lane(uint32_t l)
{
    static const uint8_t K[7] = {0, 2, 4, 6, 7, 9, 11};      /* F G A B C D E */
    return (l / 7u) * 12u + K[l % 7u];
}

/* drum steps: a lane's bit, level, ratchet */
static int dstep_has(const dstep_t *s, uint32_t l) { return (s->on[(l >> 3) & 1u] >> (l & 7u)) & 1u; }
static uint32_t dstep_lvl(const dstep_t *s, uint32_t l) { return (s->lvl[(l >> 2) & 3u] >> ((l & 3u) * 2u)) & 3u; }
static uint32_t dstep_rat(const dstep_t *s, uint32_t l) { return (s->rat[(l >> 2) & 3u] >> ((l & 3u) * 2u)) & 3u; }
static uint32_t dstep_mask(const dstep_t *s) { return (uint32_t)s->on[0] | (uint32_t)s->on[1] << 8; }
static void dstep_set(dstep_t *s, uint32_t l, uint32_t lvl, uint32_t rat)   /* lane on, with its level and ratchet */
{
    uint32_t sh = (l & 3u) * 2u, b = (l >> 2) & 3u;
    s->on[(l >> 3) & 1u] |= (uint8_t)(1u << (l & 7u));
    s->lvl[b] = (uint8_t)((s->lvl[b] & ~(3u << sh)) | (lvl & 3u) << sh);
    s->rat[b] = (uint8_t)((s->rat[b] & ~(3u << sh)) | (rat & 3u) << sh);
}
static void dstep_clr(dstep_t *s, uint32_t l)                                /* lane off */
{
    uint32_t sh = (l & 3u) * 2u, b = (l >> 2) & 3u;
    s->on[(l >> 3) & 1u] &= (uint8_t)~(1u << (l & 7u));
    s->lvl[b] &= (uint8_t)~(3u << sh);
    s->rat[b] &= (uint8_t)~(3u << sh);
}

/* the zone a sampled drum voice plays (drum_on: v->s[4]): SMP_ZONES[i], or a user kit's, DZ_USR + slot * 16 + zone */
#define DZ_USR (1 << 16)                 /* (above every SMP_ZONES index) */
static const smp_zone_t *drum_zone_of(const voice_t *v)
{
    int32_t z = v->s[4];
    return z >= DZ_USR ? &usr_zone[((uint32_t)(z - DZ_USR) >> 4) % SMP_USER_SLOTS][(uint32_t)z & 15u] : &SMP_ZONES[(uint32_t)z];
}
/* a user kit (KIT USR1..3): the zone of slot k for the lane note ln (the lane's GM note), -1 = none */
static int32_t drum_usr_zone(uint32_t k, uint32_t ln)
{
    uint32_t j;
    for (j = 0; j < usr_nz[k] && j < 16u; j++)
        if (ln >= usr_zone[k][j].lo && ln <= usr_zone[k][j].hi)
            return DZ_USR + (int32_t)(k * 16u + j);
    return -1;
}

/* the velocity of a level: as played (LV_NORM: vel), ghost, soft, hard */
static uint32_t lvl_vel(uint32_t lvl, uint32_t vel)
{
    static const uint8_t V[4] = {0, 42, 72, 127};
    return lvl & 3u ? V[lvl & 3u] : vel;
}
/* a MIDI velocity -> the level it records as (keys play 100: LV_NORM) */
static uint32_t vel_lvl(uint32_t vel)
{
    return vel < 56u ? LV_GHOST : vel < 88u ? LV_SOFT : vel < 116u ? LV_NORM : LV_HARD;
}

/* the metronome (GLO > GLOBAL > CLICK, and the REC count-in), 2.4.1: a voice of its own, mixed after the drum
 * track (fx.c mix_block), so it is heard with every kit (a user kit has no wood block: it was silent), whatever
 * the drum track's mute, a solo, its filter or slicer. A short sine tick: 1568 Hz on a bar's first beat, 1047 Hz
 * on the others, about 12 ms to fade by 1/e; its level follows GLO > DRUMS > LVL (100: as loud as the old
 * wood block). It costs a few hundred cycles a block while it sounds, nothing after */
static struct { uint32_t ph, inc; int32_t env, gain; } click;   /* env: Q30, 0 = silent */
static uint32_t click_n;                         /* clicks so far, and the last one's accent (the host tests count them) */
static uint8_t click_acc;
static void click_on(int accent)
{
    click.ph = 0;
    click.inc = accent ? (uint32_t)(((uint64_t)1568u << 32) / FS) : (uint32_t)(((uint64_t)1047u << 32) / FS);
    click.gain = accent ? 20000 : 12000;
    click.env = 1 << 30;
    click_acc = (uint8_t)(accent != 0);
    click_n++;
}
static void click_render(int32_t *ml, int32_t *mr, uint32_t n)
{
    uint32_t i;
    int32_t g;
    if (!click.env)
        return;
    g = (int32_t)((int64_t)click.gain * song.g[G_DRLVL] / 100);
    for (i = 0; i < n; i++) {
        int32_t v = (int32_t)(((int64_t)sine_i(click.ph) * (click.env >> 15) >> 15) * g >> 15);
        ml[i] += v;
        mr[i] += v;
        click.ph += click.inc;
        click.env -= click.env >> 9;                 /* about 12 ms to 1/e */
    }
    if (click.env < (1 << 18))
        click.env = 0;                                /* (-72 dB: done, about 100 ms) */
}

/* (2.8.3) the voice for a drum hit: a free voice; all busy, the sound's own voice if it still sounds (it starts over,
 * as on a drum machine: a 16th hi-hat no longer takes every voice with its own tails); else the oldest, a kick last.
 * Up to 2.8.2 the oldest went whatever it was: on a busy beat (16th hats, ride, crash, snare and clap) the kick lost its
 * voice after a step or two and was cut, or not heard at all (a tester's report, "Heads Will Roll") */
static voice_t *drum_pick(uint32_t note)
{
    voice_t *v = 0;
    uint32_t i, best = 0xFFFFFFFFu;
    for (i = 0; i < NDRUM; i++)
        if (!drums.v[i].active)
            return &drums.v[i];
    for (i = 0; i < NDRUM; i++)
        if (drums.v[i].note == note)
            return &drums.v[i];
    for (i = 0; i < NDRUM; i++) {                   /* the oldest that is not a kick; all kicks: the oldest */
        uint32_t kick = drums.v[i].note == 35u || drums.v[i].note == 36u;
        uint32_t key = (kick ? 0x80000000u : 0u) | (drums.v[i].age & 0x7FFFFFFFu);
        if (key < best) {
            best = key;
            v = &drums.v[i];
        }
    }
    return v ? v : &drums.v[0];
}
/* a synthesised voice: note on the 16 sounds snd (a factory kit, SYN1..4), the oldest voice stolen */
static void __attribute__((noinline)) drum_synth_on(uint32_t note, uint32_t vel, uint32_t kit, const dsnd_t *snd, uint32_t crush,
                                                    int tweak)
{
    voice_t *v = &drums.v[0];
    uint32_t i;
    if (note == 42u || note == 44u)                 /* hi-hat choke */
        for (i = 0; i < NDRUM; i++)
            if (drums.v[i].active && drums.v[i].note == 46u) {
                drums.v[i].active = 0;
                drums.tail += drums.v[i].s[7];
            }
    v = drum_pick(note);
    if (v->active)
        drums.tail += v->s[7];
    i = (uint32_t)(v - drums.v);
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->active = 1;
    v->s[7] = 0;
    v->age = ++drums.age;
    drums.synth[i] = 1;
    drums.src[i] = drum_src;
    drums.kit[i] = (uint8_t)kit;
    {
        uint32_t ln = lane_of_note(note);
        tweak = tweak && !DRUM_CLICK(note);
        ds_on(&drums.ds[i], snd, crush, note, vel, tweak ? drum_tune[ln] * 16 : 0, tweak ? (uint32_t)(drum_dec[ln] + 4) : 4u);
    }
}
/* the editor's audition (editor_dsyn.c DSYN_PLAY): queued, played by the audio ISR (drum_audition_poll) */
static volatile uint8_t dsu_aud_k, dsu_aud_lane, dsu_aud_vel;
static void drum_audition_poll(void)
{
    uint32_t vel = dsu_aud_vel;
    if (!vel)
        return;
    dsu_aud_vel = 0;
    {
        const dsu_kit_t *u = dsu_kit(dsu_aud_k);
        drum_src = drum_focus;
        drum_synth_on(DS_LANE_NOTE[dsu_aud_lane % DS_LANES], vel, DRUM_SYN + dsu_aud_k % DSU_N, u->s, u->crush, 0);   /* (as designed) */
    }
}

/* a hit of drum track src (0 D1, 1 D2, 2 D3) on its kit */
static void drum_on_k(uint32_t note, uint32_t vel, uint32_t kit, uint32_t src)
{
    int32_t si = drum_set();
    const smp_set_t *set;
    voice_t *v = &drums.v[0];
    uint32_t i, zi = 0xFFFFu;
    int32_t zid = -1;
    kit = kit < DRUM_KITS ? kit : 0u;
    drum_src = (uint8_t)(src % 3u);
    if (note != 76u && note != 77u && src == drum_focus)   /* the pads and key LEDs (not the click's wood block): the
                                                         * drum track shown */
        drums.hits |= (uint16_t)(1u << lane_of_note(note));
    if (note == 35u || note == 36u)
        drums.kick = 1;                             /* (DUCK) */
    if (kit >= DRUM_SYN) {                          /* SYN1..SYN4: your synthesised kit */
        const dsu_kit_t *u = dsu_kit(kit - DRUM_SYN);
        drum_synth_on(note, vel, kit, u->s, u->crush, 1);
        return;
    }
    if (kit >= DRUM_USR) {                          /* a user kit: the lane's sound, as its note (the hat choke) */
        if (note == 76u || note == 77u)
            return;                                 /* (the click's wood block: not a lane) */
        note = LANE_NOTE[lane_of_note(note)];
        if (kit == DRUM_PAIR) {                     /* USR3+4: the lane's sound in USR3, else in USR4 */
            zid = drum_usr_zone(2, note);
            if (zid < 0)
                zid = drum_usr_zone(3, note);
        } else {
            zid = drum_usr_zone(kit - DRUM_USR, note);
        }
        if (zid < 0)
            return;                                 /* (an empty slot, or no sound on this lane) */
    } else if (kit >= DRUM_SAMPLED) {               /* synthesised kit */
        drum_synth_on(note, vel, kit, DS_KITS[kit - DRUM_SAMPLED].s, DS_KITS[kit - DRUM_SAMPLED].crush, 1);
        return;
    }
    if (zid < 0) {                                  /* the GM sample kit */
        if (si < 0)
            return;
        set = &SMP_SETS[si];
        for (i = 0; i < set->nz; i++)
            if (note >= SMP_ZONES[set->z0 + i].lo && note <= SMP_ZONES[set->z0 + i].hi)
                zi = set->z0 + i;
        if (zi == 0xFFFFu)
            return;
        zid = (int32_t)zi;
    }
    if (note == 42u || note == 44u)                 /* hi-hat choke */
        for (i = 0; i < NDRUM; i++)
            if (drums.v[i].active && drums.v[i].note == 46u) {
                drums.v[i].active = 0;
                drums.tail += drums.v[i].s[7];          /* fade what it was playing, not a step */
            }
    v = drum_pick(note);                            /* (2.8.3: a free one, its own, else the oldest but a kick) */
    if (v->active)
        drums.tail += v->s[7];                      /* stolen voice: fade its last value */
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->active = 1;
    v->s[7] = 0;
    v->age = ++drums.age;
    v->s[4] = zid;
    v->ph[0] = v->ph[1] = 0;
    v->s[0] = v->s[1] = v->s[2] = 0;
    {
        const smp_zone_t *z = drum_zone_of(v);
        v->s[3] = sample_next(z, v, 0);
        /* (after 2.8.3) clamped: a user kit zone's root is not checked (a typed root of 300 shifted by 2^20) */
        v->s[5] = (int32_t)((pow2_q16(clamp((int32_t)note * 16 - z->root16, -1536, 2047)) >> 8) * (z->rate >> 8));
    }
    {
        uint32_t vi = (uint32_t)(v - drums.v);
        uint32_t ln = lane_of_note(v->note), tw = !DRUM_CLICK(v->note);
        int32_t shift = kit == 1u ? (note <= 36u ? -5 : -2) : kit == 3u ? 2 : kit == 4u ? -1 : 0;
        drums.kit[vi] = (uint8_t)kit;
        drums.synth[vi] = 0;
        drums.src[vi] = drum_src;
        drums.filter[vi] = 0;
        drums.env[vi] = 32767;
        drums.denv[vi] = 32767;                     /* (2.7) DECAY below 0: a fade to -60 dB in 60 .. 500 ms (DK_SMP) */
        drums.dk[vi] = tw && drum_dec[ln] < 0 ? DECAY_K[DK_SMP[drum_dec[ln] + 4]] : 0u;
        shift = shift * 16 + (tw ? drum_tune[ln] * 16 : 0);    /* (2.7) TUNE: the sound's semitones */
        drums.usr[vi] = (uint8_t)drum_usr(kit);
        if (drums.usr[vi]) {                        /* (PHREAK 1.1) a USR kit: its drum track's TUNE, ENV, GATE, LFO */
            uint32_t gate = div_samples((uint32_t)*xd_par(drum_src, XD_DIV) % NDIV_STEP) *
                            (uint32_t)clamp(*xq_par(drum_src, XQ_GATE), 1, 127) / 128u;
            shift += *xq_par(drum_src, XQ_TUNE) * 16;
            v->stage = *xq_par(drum_src, XQ_ATK) ? 1u : 2u;
            v->env = v->stage == 1u ? 0 : 1 << 24;
            v->env_out = v->env >> 9;
            v->gate = 1;
            v->penv = 32767;
            drums.hold[vi] = gate ? gate : 1u;
            drums.ic1[vi] = drums.ic2[vi] = 0;
            if (!*xq_par(drum_src, XQ_LTRIG)) {     /* LFO TRIG NOTE: from its PHASE, faded in again (voice.c) */
                drums.lfo[drum_src].ph = (uint32_t)*xq_par(drum_src, XQ_LPHS) << 25;
                drums.lfo[drum_src].fade = 0;
            }
        }
        if (shift) {
            const smp_zone_t *z = drum_zone_of(v);
            v->s[5] = (int32_t)((pow2_q16(clamp((int32_t)note * 16 + shift - z->root16, -1536, 2047)) >> 8) * (z->rate >> 8));
        }
    }
}

static void drum_on(uint32_t note, uint32_t vel) { drum_on_k(note, vel, drum_kit(), drum_focus); }   /* the drum track shown */

/* (PHREAK 1.1) a drum track's USR kit sound for this block (drum_usr_voice): its values, its LFO's (lfo, Q15 with the
 * fade), and what it uses (none: the hit plays as any sampled hit, drums_mix's own loop) */
typedef struct {
    int16_t q[XQ_N];
    int32_t lfo;
    uint8_t env, flt, pit, col;                         /* ADSR, filter, pitch moves, BITS / DRV / LOOP / tremolo */
} dusr_t;
static void __attribute__((noinline)) drum_snd_block(uint32_t d, dusr_t *s)
{
    uint32_t i, old;
    const int16_t *q = s->q;
    int32_t w;
    for (i = 0; i < XQ_N; i++)
        s->q[i] = *xq_par(d, i);
    s->env = q[XQ_ATK] || q[XQ_SUS] < 127 || q[XQ_REL] < 127;
    s->flt = q[XQ_CUT] < 127 || q[XQ_RES] || q[XQ_FTYPE] || q[XQ_LFLT] || (q[XQ_EFLT] && s->env);
    s->pit = q[XQ_EPIT] || q[XQ_LPIT];
    s->col = q[XQ_BITS] || q[XQ_DRV] || q[XQ_LOOP] || q[XQ_LAMP];
    if (!q[XQ_LPIT] && !q[XQ_LFLT] && !q[XQ_LAMP]) {
        s->lfo = 0;
        return;
    }
    old = drums.lfo[d].ph;                              /* the LFO, as voice.c track_lfo_tick */
    if (q[XQ_LSYNC]) {                                  /* SYNC: a cycle the note value (voice.c lfo_sync_inc) */
        uint32_t beat = (uint32_t)FS * 60u / (uint32_t)clamp(song.g[G_BPM], 20, 300), cyc;
        static const uint16_t PUL[11] = {0, 384, 192, 96, 48, 24, 12, 8, 6, 4, 3};
        cyc = beat * PUL[(uint32_t)q[XQ_LSYNC] % 11u] / 24u;
        cyc = cyc ? cyc : 1u;
        drums.lfo[d].ph += 0xFFFFFFFFu / cyc * CTL + ((0xFFFFFFFFu % cyc) * CTL + CTL) / cyc;
    } else {
        drums.lfo[d].ph += LFO_INC[q[XQ_LRATE] & 127];
    }
    if (drums.lfo[d].ph < old)                          /* S&H: its own numbers (not rng(): the drums may render on
                                                         * the second core, fx.c units_split) */
        drums.lfo[d].rnd = drums.lfo[d].rnd * 1664525u + 1013904223u;
    switch (q[XQ_LWAVE]) {
    case 1: w = osc_tri(drums.lfo[d].ph); break;
    case 2: w = (int32_t)(drums.lfo[d].ph >> 16) - 32768; break;
    case 3: w = drums.lfo[d].ph < 0x80000000u ? 32767 : -32767; break;
    case 4: w = (int32_t)(drums.lfo[d].rnd >> 16) - 32768; break;
    default: w = osc_sine(drums.lfo[d].ph); break;
    }
    if (q[XQ_LPOL])
        w = (w + 32768) >> 1;
    if (drums.lfo[d].fade < 32767) {
        int32_t st = (int32_t)(ENV_LIN[q[XQ_LFADE] & 127] >> 9);
        drums.lfo[d].fade = q[XQ_LFADE] ? clamp(drums.lfo[d].fade + (st ? st : 1), 0, 32767) : 32767;
    }
    s->lfo = mulq15(w, drums.lfo[d].fade);
}

/* a USR kit's hit through its drum track's sound (drum_snd_block): n samples into the mix, as drums_mix's loop (g its
 * level, mf the shown track's mute fade or 32767 for the others); its envelope, pitch and filter move once a block */
static void __attribute__((noinline)) drum_usr_voice(uint32_t k, const dusr_t *ds, int32_t g, int32_t gl, int32_t gr,
                                                     int shown, int32_t *ml, int32_t *mr, int32_t *rev, int32_t *dly,
                                                     int32_t *mono, int32_t send, int32_t dsend, uint32_t n, int32_t *pk)
{
    voice_t *v = &drums.v[k];
    const smp_zone_t *z = drum_zone_of(v);
    const int16_t *q = ds->q;
    uint32_t i, frac = v->ph[1], stepq = (uint32_t)v->s[5], loop = (uint32_t)q[XQ_LOOP];
    int32_t a0 = v->env_out, a1 = 32767, envq = 32767, sh = q[XQ_BITS] / 10, drv = q[XQ_DRV], ft = q[XQ_FTYPE];
    int32_t ic1 = drums.ic1[k], ic2 = drums.ic2[k], fk = ANALOG_K(q[XQ_RES]);
    tsvf_t flt;
    if (drums.hold[k] > n) {                            /* the GATE: then the release */
        drums.hold[k] -= n;
    } else if (v->gate) {
        drums.hold[k] = 0;
        v->gate = 0;
        if (v->stage == 1u || v->stage == 2u)
            v->stage = 3;
    }
    if (ds->env) {                                      /* the ADSR, as voice.c env_tick (ENV SYNC: not here) */
        if (v->stage == 1u) {
            v->env += (int32_t)ENV_LIN[q[XQ_ATK] & 127];
            if (v->env >= (1 << 24))
                v->env = 1 << 24, v->stage = 2;
        } else if (v->stage == 2u) {
            v->env += mulq16(((int32_t)q[XQ_SUS] << 17) - v->env, ENV_EXP[q[XQ_DEC] & 127]);
        } else if (v->stage == 3u) {
            v->env -= mulq16(v->env, ENV_EXP[q[XQ_REL] & 127]);
            if (v->env < (1 << 12)) {
                v->env = 0;
                v->active = 0;
            }
        }
        envq = v->env >> 9;
        a1 = envq;
    }
    if (q[XQ_LAMP])                                     /* tremolo (LFO > AMP) */
        a1 = mulq15(a1, 32767 - mulq15(q[XQ_LPOL] ? ds->lfo : (ds->lfo + 32768) >> 1, q[XQ_LAMP] * 258));
    v->env_out = a1;
    if (!ds->env && !q[XQ_LAMP])
        a0 = a1;
    if (ds->pit) {                                      /* ENV > PIT (~40 ms) and LFO > PIT, in 1/4096 semitone */
        int32_t d = ((ds->lfo * q[XQ_LPIT] * 3) >> 7) + ((v->penv * q[XQ_EPIT] * 3) >> 7);
        v->penv = (int16_t)(v->penv - ((v->penv * 590 + 32767) >> 15));
        if (d >> 8)
            stepq = (uint32_t)(((uint64_t)stepq * pow2_q16(clamp(d >> 8, -1536, 1536))) >> 16);
    }
    if (ds->flt)
        tsvf_coef(&flt, (q[XQ_CUT] << 8) + ((ds->lfo * q[XQ_LFLT]) >> 7) + ((envq * q[XQ_EFLT]) >> 7), q[XQ_RES]);
    if (!v->active)
        n = 0;
    for (i = 0; i < n; i++) {
        int32_t s;
        frac += stepq;
        while (frac >= 65536u) {
            frac -= 65536u;
            v->s[2] = v->s[3];
            if (v->ph[0] >= z->n) {
                if (!(loop && v->gate)) {
                    v->active = 0;
                    break;
                }
                v->ph[0] = 0;                           /* LOOP: from the start again while the hit holds */
                v->s[0] = v->s[1] = 0;
            }
            v->s[3] = sample_next(z, v, (int)loop);
        }
        if (!v->active)
            break;
        s = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
        if (sh)                                         /* BITS, DRV: as the SAMPLE engine */
            s = (s >> sh) << sh;
        if (drv)
            s = softclip(s + (((s >> 2) * (drv * 150)) >> 11));
        if (ds->flt) {                                  /* the filter, as ANALOG's (its soft knee) */
            int32_t y = ft ? analog_tap(&flt, fk, s >> 1, &ic1, &ic2, (uint32_t)ft) : tsvf_lp(&flt, s >> 1, &ic1, &ic2),
                    a = y < 0 ? -y : y;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                y = y < 0 ? -a : a;
            }
            s = y << 1;
        }
        s = mulq15(s, a0 + (((a1 - a0) * (int32_t)i) >> CTL_LOG2));
        s = mulq15(s, !shown ? g : mulq15(g, 32767 - drums.a0 - (((drums.a1 - drums.a0) * (int32_t)i) >> CTL_LOG2)));
        if (drums.dk[k]) {                              /* (2.7) the sound's shorter DECAY */
            s = mulq15(s, drums.denv[k]);
            drums.denv[k] = (int32_t)(((uint32_t)drums.denv[k] * drums.dk[k]) >> 16);
            if (drums.denv[k] < 8) v->active = 0;
        }
        s = DRUM_VOICE_FX(k, s);
        v->s[7] = s;
        if (s > *pk || -s > *pk)
            *pk = s < 0 ? -s : s;
        if (mono) {
            mono[i] += s;
            continue;
        }
        ml[i] += (s * DRUM_VOICE_GL(k, gl)) >> 12;
        mr[i] += (s * DRUM_VOICE_GR(k, gr)) >> 12;
        if (send)
            rev[i] += mulq15(s, send);
        if (dsend)
            dly[i] += mulq15(s, dsend);
    }
    if (!v->active) {
        drums.tail += v->s[7];                          /* ended (its release, a fade): no step at the end */
        v->s[7] = 0;
    }
    v->ph[1] = frac;
    drums.ic1[k] = ic1, drums.ic2[k] = ic2;
}

/* adds the drums into the dry mix and the reverb and delay sends; mono != 0: into mono instead, before
 * the pan and the sends (the SLICER, slicer.c slicer_drums, does those after it) */
static inline void drums_mix(int32_t *ml, int32_t *mr, int32_t *rev, int32_t *dly, int32_t *mono, uint32_t n)
{
    uint32_t k, i, d;
    int32_t send = send_thr(song.g[G_DRREV] * 258, thr_rev), dsend = send_thr(song.g[G_DRDLY] * 258, thr_dly),
            pk = drums.peak;   /* (2.6: the THROWs) */
    int32_t lvl, gl, gr, dl[3], dgl[3], dgr[3], don[3];
    uint32_t solo_out = song.solo && !((song.solo >> TRK_DRUM) & 1u);   /* another track soloed */
    for (d = 0; d < 3u; d++) {                      /* each drum track's LEVEL and PAN (2.7: they glide, mix_glide) */
        int32_t pan = *xd_par(d, XD_PAN), l = expr_gain(drum_level_of(d) * 200, TRK_DRUM);
        int32_t a = 4096 - (pan > 0 ? pan * 64 : 0), b = 4096 + (pan < 0 ? pan * 64 : 0);
        if (!drums.gon[d])
            drums.glv[d] = l, drums.ggl[d] = a, drums.ggr[d] = b, drums.gon[d] = 1;
        dl[d] = drums.glv[d] = mix_glide(drums.glv[d], l);
        dgl[d] = drums.ggl[d] = mix_glide(drums.ggl[d], a);
        dgr[d] = drums.ggr[d] = mix_glide(drums.ggr[d], b);
        don[d] = d == drum_focus || (!*xd_par(d, XD_MUTE) && !solo_out);   /* (the one shown: the a0 / a1 fade) */
    }
    for (i = 0; i < n && drums.tail; i++) {         /* declick tail, ~0.4 ms */
        if (mono) {
            mono[i] += drums.tail;
        } else {
            ml[i] += drums.tail;
            mr[i] += drums.tail;
        }
        drums.tail -= drums.tail / 16 + (drums.tail > 0 ? 1 : drums.tail < 0 ? -1 : 0);
    }
    for (k = 0; k < NDRUM; k++) {               /* synthesised voices: render, then as below */
        voice_t *v = &drums.v[k];
        uint32_t m = n < CTL ? n : CTL;             /* (the mix runs in blocks of CTL) */
        if (!v->active || !drums.synth[k])
            continue;
        d = drums.src[k] % 3u;
        lvl = don[d] ? dl[d] : 0, gl = dgl[d], gr = dgr[d];
        if (!ds_render(&drums.ds[k], ds_buf, m))
            v->active = 0;
        for (i = 0; i < m; i++) {
            int32_t s = mulq15(ds_buf[i], mulq15(lvl, d != drum_focus ? 32767 :
                                                     32767 - drums.a0 - (((drums.a1 - drums.a0) * (int32_t)i) >> CTL_LOG2)));
            s = DRUM_VOICE_FX(k, s);
            v->s[7] = s;
            if (s > pk || -s > pk)
                pk = s < 0 ? -s : s;
            if (mono) {
                mono[i] += s;
                continue;
            }
            ml[i] += (s * DRUM_VOICE_GL(k, gl)) >> 12;
            mr[i] += (s * DRUM_VOICE_GR(k, gr)) >> 12;
            if (send)
                rev[i] += mulq15(s, send);
            if (dsend)
                dly[i] += mulq15(s, dsend);
        }
        if (!v->active) {
            drums.tail += v->s[7];                  /* ended: no step at the end */
            v->s[7] = 0;
        }
    }
    static dusr_t dsb[3];                               /* (PHREAK 1.1) each drum track's USR kit sound, this block */
    uint32_t dsn = 0;
    for (k = 0; k < NDRUM; k++) {
        voice_t *v = &drums.v[k];
        const smp_zone_t *z;
        if (drums.synth[k])
            continue;
        z = drum_zone_of(v);
        uint32_t frac = v->ph[1], stepq = (uint32_t)v->s[5];   /* Q16 source samples per output (drum_on) */
        int32_t g;
        if (!v->active)
            continue;
        d = drums.src[k] % 3u;
        lvl = don[d] ? dl[d] : 0, gl = dgl[d], gr = dgr[d];
        g = mulq15(lvl, v->vel * 258);
        g += g * 3 >> 2;                           /* x1.75 (+5 dB): as loud as the synthesised kits */
        if (drums.usr[k]) {                         /* a USR kit: its drum track's sound, if it uses any of it */
            if (!((dsn >> d) & 1u)) {
                drum_snd_block(d, &dsb[d]);
                dsn |= 1u << d;
            }
            if (dsb[d].env | dsb[d].flt | dsb[d].pit | dsb[d].col) {
                drum_usr_voice(k, &dsb[d], g, gl, gr, d == drum_focus, ml, mr, rev, dly, mono, send, dsend, n, &pk);
                continue;
            }
        }
        for (i = 0; i < n; i++) {
            int32_t s;
            frac += stepq;
            while (frac >= 65536u) {
                frac -= 65536u;
                v->s[2] = v->s[3];
                if (v->ph[0] >= z->n) {
                    v->active = 0;
                    break;
                }
                v->s[3] = sample_next(z, v, 0);
            }
            if (!v->active)
                break;
            s = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
            s = mulq15(s, d != drum_focus ? g : mulq15(g, 32767 - drums.a0 - (((drums.a1 - drums.a0) * (int32_t)i) >> CTL_LOG2)));
            if (drums.kit[k] == 1u || drums.kit[k] == 4u) {
                drums.filter[k] += (s - drums.filter[k]) >> (drums.kit[k] == 1u ? 2 : 1);
                s = drums.filter[k];
                if (drums.kit[k] == 4u) s = (s >> 8) * 256;
            } else if (drums.kit[k] == 2u) {
                s = mulq15(s, drums.env[k]);
                drums.env[k] -= (drums.env[k] >> 11) + 1;
                if (drums.env[k] <= 0) v->active = 0;
            }
            if (drums.dk[k]) {                      /* (2.7) a shorter DECAY: the sample fades out */
                s = mulq15(s, drums.denv[k]);
                drums.denv[k] = (int32_t)(((uint32_t)drums.denv[k] * drums.dk[k]) >> 16);
                if (drums.denv[k] < 8) v->active = 0;
            }
            s = DRUM_VOICE_FX(k, s);
            v->s[7] = s;
            if (s > pk || -s > pk)
                pk = s < 0 ? -s : s;
            if (mono) {
                mono[i] += s;
                continue;
            }
            ml[i] += (s * DRUM_VOICE_GL(k, gl)) >> 12;
            mr[i] += (s * DRUM_VOICE_GR(k, gr)) >> 12;
            if (send)
                rev[i] += mulq15(s, send);
            if (dsend)
                dly[i] += mulq15(s, dsend);
        }
        v->ph[1] = frac;
    }
    drums.peak = pk;
}
static void drums_render(int32_t *ml, int32_t *mr, int32_t *rev, int32_t *dly, uint32_t n) { drums_mix(ml, mr, rev, dly, 0, n); }
static void drums_render_mono(int32_t *mono, uint32_t n) { drums_mix(0, 0, 0, 0, mono, n); }
