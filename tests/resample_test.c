/* SPDX-License-Identifier: GPL-3.0-only */
/* PHREAK 1.8: RESAMPLE (firmware/src/resample.c, smpedit_save.c rs_write) on slots in a RAM image of the flash
 *   the take: NOW / NOTE / PLAY starts, BARS, FREE until full, the LFO stop, the transport stop, 22 / 44 kHz, DRY / MIX
 *   the real mix (a held sine through mix_block) into a slot: its pitch, its level; the delay on the upper half only
 *   the write: one zone over every key at C4, the slot reads back, only its sectors erased (the audio held off), the
 *   delay line whole and clear after */
#include <stdint.h>
static uint32_t host_slots[6u * 0x14000u / 4u];
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what) { printf("resample: %-92s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

/* ---- what smpedit_save.c calls (felucca.c's flash driver, the screen) */
static int irq_masked, irq_mask_n, irq_unmask_n, erased_while_unmasked;
#define FM1_IRQ_ALNK0 11u
static void fm1_irq_mask(uint32_t n) { (void)n; irq_masked = 1; irq_mask_n++; }
static void fm1_irq_unmask(uint32_t n) { (void)n; irq_masked = 0; irq_unmask_n++; }
static void fm1_delay_us(uint32_t us) { (void)us; }
#ifndef fm1_wdt_feed
static void se_wdt(void) {}
#define fm1_wdt_feed se_wdt
#endif
static uint8_t *flash_at(uint32_t off)              /* a flash offset in a slot -> the RAM image */
{
    uint32_t k;
    for (k = 0; k < SMP_USER_SLOTS; k++)
        if (off >= SMP_USER_OFF(k) && off < SMP_USER_OFF(k) + SMP_USER_SIZE)
            return (uint8_t *)host_slots + k * SMP_USER_SIZE + (off - SMP_USER_OFF(k));
    return 0;
}
static int fl_erase4k_quiet(uint32_t off, uint32_t *took)
{
    uint8_t *p = flash_at(off);
    (void)took;
    if (!p || (off & 0xFFFu))
        return -1;
    erased_while_unmasked += !irq_masked;
    memset(p, 0xFF, 0x1000u);
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *src, uint32_t n)
{
    uint8_t *p = flash_at(off);
    uint32_t i;
    if (!p || !flash_at(off + n - 1u))
        return -1;
    for (i = 0; i < n; i++)
        p[i] &= src[i];                              /* (NOR: bits only cleared) */
    return 0;
}
static void ed_smp_inval(uint32_t k) { (void)k; }
static uint32_t st_crc32(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t c = ~0u, i, j;
    for (i = 0; i < n; i++)
        for (c ^= b[i], j = 0; j < 8u; j++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    return ~c;
}
static char last_msg[32];
static void ui_message(const char *s) { strncpy(last_msg, s, sizeof last_msg - 1u); }
static struct { uint8_t force; } ui;
#define lcd_fill(...) ((void)0)
#define cv_begin(...) ((void)0)
#define cv_text(...) ((void)0)
#define cv_blit(...) ((void)0)
#define text_w(...) 0
#define SE_CORE_ONLY 1
#include "../firmware/src/smpedit.c"
#include "../firmware/src/smpedit_save.c"

/* ---- the audio side, fed directly: a sine of amp at hz (44.1 kHz frames), block by block */
static double ph;
static void feed(uint32_t frames, double hz, double amp, uint32_t dry)
{
    int32_t l[CTL], r[CTL];
    uint32_t i, n;
    while (frames) {
        n = frames > CTL ? CTL : frames;
        for (i = 0; i < n; i++) {
            l[i] = r[i] = (int32_t)(sin(ph) * amp);
            ph += hz * 2.0 * 3.14159265358979 / 44100.0;
        }
        rs_block_begin(n);
        rs_tap(l, r, n, dry);
        frames -= n;
    }
}
static void arm(uint32_t k, uint32_t rate2, uint32_t bars, uint32_t start, uint32_t dry)
{
    memset(&rsm, 0, sizeof rsm);
    memset(dly_buf, 0, sizeof dly_buf);
    rs_dly_half = 1;
    rsm.k = (uint8_t)k, rsm.rate2 = (uint8_t)rate2, rsm.start = (uint8_t)start, rsm.dry = (uint8_t)dry;
    rsm.want = bars ? rs_bar_frames(bars, (uint32_t)song.g[G_BPM]) : 0u;
    rsm.st = RS_ARMED;
    ph = 0;
}
/* the take decoded (from dly_buf) */
static int16_t take[RS_MAXN];
static uint32_t take_dec(void)
{
    int32_t pred = 0, idx = 0;
    uint32_t j;
    for (j = 0; j < rsm.out; j++)
        take[j] = (int16_t)se_dec(RS_DATA, j, &pred, &idx);
    return rsm.out;
}
static uint32_t crossings(const int16_t *x, uint32_t a, uint32_t b)   /* rising zero crossings in [a, b) */
{
    uint32_t i, c = 0;
    for (i = a + 1u; i < b; i++)
        c += x[i - 1u] < 0 && x[i] >= 0;
    return c;
}
static int32_t peak_of(const int16_t *x, uint32_t a, uint32_t b)
{
    int32_t p = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        p = abs(x[i]) > p ? abs(x[i]) : p;
    return p;
}

static void zone_dec_n(uint32_t k, int16_t *out)      /* slot k's zone 0, decoded from the flash image */
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)SMP_USER_XIP(k);
    const uint8_t *d = SMP_USER_XIP(k) + SMP_USER_DATA + h->zone[0].off;
    int32_t pred = 0, idx = 0;
    uint32_t j;
    for (j = 0; j < h->zone[0].n; j++)
        out[j] = (int16_t)se_dec(d, j, &pred, &idx);
}

int main(void)
{
    uint32_t n, i, k, c, bad;
    host_tracks_init();
    song.g[G_BPM] = 120;
    song.playing = 0;

    /* NOW, 22 kHz, 1 bar at 120: two seconds of 44.1 kHz in, 44100 samples kept, a 441 Hz sine at its pitch */
    arm(2, 1, 1, RS_START_NOW, 0);
    feed(88200u + 4000u, 441.0, 12000.0, 0);
    check(rsm.st == RS_DONE, "NOW, 1 bar at 120 BPM: done after the bar (more fed: nothing more taken)");
    check(rsm.out == 44100u, "22 kHz: a 2 s bar is 44100 samples");
    n = take_dec();
    c = crossings(take, 0, n);
    check(c >= 880u && c <= 884u, "22 kHz: the sine keeps its pitch (882 cycles in 2 s)");
    check(abs(peak_of(take, 1000, n) - 12000) < 600, "22 kHz: and its level");

    /* the write: one zone, C4, all keys, 22050 Hz; it reads back through smp_user_scan */
    memset((uint8_t *)host_slots + 1u * SMP_USER_SIZE, 0x5A, SMP_USER_SIZE);   /* (the slot after: untouched) */
    irq_mask_n = irq_unmask_n = erased_while_unmasked = 0;
    dly_buf[DLY_LEN - 1u] = 77;                      /* (the delay's half: cleared after) */
    check(rs_write() == 0, "the take written into USR3");
    {
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)SMP_USER_XIP(2);
        int16_t back[44100];
        check(h->magic == SMP_USER_MAGIC && h->nz == 1u && !memcmp(h->name, "RESAMPLE", 8), "the slot: RESAMPLE, one zone");
        check(h->zone[0].n == 44100u && h->zone[0].rate == 1u << 15 && h->zone[0].root16 == 60 * 16 &&
                  h->zone[0].lo == 0 && h->zone[0].hi == 127 && !h->zone[0].looped,
              "its zone: 44100 samples at 22050 Hz, root C4, every key, one-shot");
        check(h->crc == st_crc32(SMP_USER_XIP(2) + SMP_USER_DATA, h->data_len) && usr_nz[2] == 1u,
              "its CRC, and the engine reads it (smp_user_scan)");
        zone_dec_n(2, back);
        for (bad = 0, i = 0; i < 44100u; i++)
            bad += back[i] != take[i];
        check(!bad, "the slot plays what was recorded, sample for sample");
        check(se_hz_of(&h->zone[0]) == 22050u, "the editor sees 22050 Hz (its times, its fades)");
    }
    for (bad = 0, i = 0; i < SMP_USER_SIZE; i++)
        bad += ((uint8_t *)host_slots + 1u * SMP_USER_SIZE)[i] != 0x5A;
    check(!bad, "USR2 (the slot below) untouched");
    check(irq_mask_n == 1 && irq_unmask_n == 1 && !erased_while_unmasked, "the audio held off for the write, on after");
    check(rsm.st == RS_OFF && !rs_dly_half && !dly_buf[DLY_LEN - 1u] && !dly_buf[0], "after: no take, the delay line whole and clear");

    /* 44 kHz, FREE: until full (3 s) */
    arm(4, 0, 0, RS_START_NOW, 0);
    feed(RS_MAXN + 5000u, 1000.0, 8000.0, 0);
    check(rsm.st == RS_DONE && rsm.out == RS_MAXN, "44 kHz FREE: stops full, 131072 samples (2.97 s)");
    n = take_dec();
    c = crossings(take, 0, 44100u);
    check(c >= 999u && c <= 1001u, "44 kHz: 1000 Hz stays 1000 Hz");
    check(rs_write() == 0 && ((const smp_user_hdr_t *)SMP_USER_XIP(4))->zone[0].rate == 1u << 16 &&
              se_hz_of(&((const smp_user_hdr_t *)SMP_USER_XIP(4))->zone[0]) == 44100u,
          "written into USR5 (above the app) at 44100 Hz");

    /* the LFO stop: FREE until LFO */
    arm(0, 1, 0, RS_START_NOW, 0);
    feed(10000u, 300.0, 5000.0, 0);
    rsm.stop_req = 1;
    feed(CTL, 300.0, 5000.0, 0);
    check(rsm.st == RS_DONE && rsm.out == 5000u, "FREE: LFO stops it at the next block (10000 frames: 5000 samples)");

    /* NOTE: silence does not start it; the first sample over the gate does */
    arm(0, 0, 0, RS_START_NOTE, 0);
    feed(20000u, 300.0, 100.0, 0);                    /* (-50 dB: under the gate) */
    check(rsm.st == RS_ARMED && !rsm.out, "NOTE: nothing heard, nothing taken");
    feed(1000u, 300.0, 9000.0, 0);
    check(rsm.st == RS_REC && rsm.out > 950u && rsm.out <= 1000u, "NOTE: it starts on the sound (within a few samples)");

    /* PLAY: waits for the transport, starts on bar 1, stops with it */
    arm(0, 1, 0, RS_START_PLAY, 0);
    feed(5000u, 300.0, 9000.0, 0);
    check(rsm.st == RS_ARMED, "PLAY: stopped, it waits");
    song.playing = 1, clk_beat = 1, clk_pos = 0;
    feed(CTL, 300.0, 9000.0, 0);
    check(rsm.st == RS_ARMED, "PLAY: playing mid-bar (beat 2): it waits for the bar");
    clk_beat = 4, clk_pos = 0;
    feed(4410u, 300.0, 9000.0, 0);
    check(rsm.st == RS_REC && rsm.out == 2205u, "PLAY: starts on the bar");
    song.playing = 0;
    feed(CTL, 300.0, 9000.0, 0);
    check(rsm.st == RS_DONE, "PLAY: the transport stopped: the take ends");

    /* SOURCE: DRY takes only the dry tap */
    arm(0, 1, 0, RS_START_NOW, 1);
    feed(1000u, 300.0, 9000.0, 0);
    check(!rsm.out, "DRY: the after-effects tap is not taken");
    feed(1000u, 300.0, 9000.0, 1);
    check(rsm.out == 500u, "DRY: the dry tap is");

    /* the delay on the upper half: a held sine sent into the delay, the take's half left as it is */
    memset(&rsm, 0, sizeof rsm);
    rs_dly_half = 1;
    for (i = 0; i < DLY_LEN / 2u; i++)
        dly_buf[i] = (int16_t)(i * 7u);
    xfade_sine(&trk[0]);
    trk[0].p[P_DLY] = 127;
    song.g[G_DMIX] = 127, song.g[G_DFDBK] = 60;
    trk_note_on(&trk[0], 69, 110);
    {
        int32_t o[2u * CTL];
        for (k = 0; k < 3000u; k++)
            mix_block(o, CTL);
    }
    for (bad = 0, i = 0; i < DLY_LEN / 2u; i++)
        bad += dly_buf[i] != (int16_t)(i * 7u);
    for (c = 0, i = DLY_LEN / 2u; i < DLY_LEN; i++)
        c += dly_buf[i] != 0;
    check(!bad, "the delay runs on the upper half: the take's half untouched");
    check(c > 1000u, "... and it does run (its half written)");

    /* the real mix: that sine (A4) through mix_block into a take, 44 kHz NOW */
    arm(1, 0, 0, RS_START_NOW, 0);
    rs_dly_half = 1;
    {
        int32_t o[2u * CTL];
        for (k = 0; k < 2000u && rsm.st != RS_DONE; k++)
            mix_block(o, CTL);
        rsm.stop_req = 1;
        mix_block(o, CTL);
    }
    check(rsm.st == RS_DONE && rsm.out >= 1999u * CTL, "the mix: 2000 blocks taken");
    n = take_dec();
    c = crossings(take, n - 44100u, n);
    check(c >= 438u && c <= 442u, "the mix: A4 in the take (440 cycles a second)");
    check(peak_of(take, n - 44100u, n) > 3000, "the mix: at a real level (before MASTER)");
    trk_note_off(&trk[0], 69);

    printf(fails ? "resample test FAILED\n" : "resample test passed\n");
    return fails != 0;
}
