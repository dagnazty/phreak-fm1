/* SPDX-License-Identifier: GPL-3.0-only */
/* PHREAK 1.5: the SAMPLE EDITOR's SAVE (firmware/src/smpedit_save.c) on a slot in a RAM image of the flash:
 *   LOOP / KEYS only: the ADPCM bytes kept as they were, the header's loop, its state at L.START, root and keys new
 *   TRIM: the zone shorter (the slot's data too), what stays sounds as before (re-encoded: a small error)
 *   REVERSE: the zone backwards; with a trim, the trim as shown
 *   NORMALIZE: the peak at 30000; FADE IN / OUT: silent at the ends, as before inside
 *   a zone not edited keeps its bytes; the slot reads back (smp_user_scan), its CRC right
 *   the audio interrupt held off while the delay line is the stage, back on after, the line cleared */
#include <stdint.h>
static uint32_t host_slots[6u * 0x14000u / 4u];
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what) { printf("smpedit: %-92s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

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

/* ---- a slot: zone 0 a sine that grows (so backwards is not forwards), zone 1 a short click */
#define N0 22050u
#define N1 3000u
static int16_t ref0[N0], ref1[N1];
static uint32_t enc_zone(uint8_t *d, const int16_t *x, uint32_t n)   /* IMA from 0 / 0 (sampleio.ima_encode) */
{
    int32_t pred = 0, idx = 0;
    uint32_t i, half = 0, o = 0;
    for (i = 0; i < n; i++) {
        int32_t step = IMA_STEP[idx], df = x[i] - pred, vd = step >> 3;
        uint32_t code = 0;
        if (df < 0) code = 8, df = -df;
        if (df >= step) code |= 4, df -= step, vd += step;
        if (df >= step >> 1) code |= 2, df -= step >> 1, vd += step >> 1;
        if (df >= step >> 2) code |= 1, vd += step >> 2;
        pred = clamp(pred + ((code & 8u) ? -vd : vd), -32768, 32767);
        idx = clamp(idx + IMA_IDX[code & 7u], 0, 88);
        if (!half) half = 0x10u | code;
        else d[o++] = (uint8_t)((half & 15u) | code << 4), half = 0;
    }
    if (half)
        d[o++] = (uint8_t)(half & 15u);
    return o;
}
static void slot_make(uint32_t k)
{
    uint8_t *img = (uint8_t *)host_slots + k * SMP_USER_SIZE, *d = img + SMP_USER_DATA;
    smp_user_hdr_t *h = (smp_user_hdr_t *)img;
    uint32_t i, o0, o1;
    for (i = 0; i < N0; i++)
        ref0[i] = (int16_t)(sin(i * 2.0 * 3.14159265 / 50.0) * 2000.0 * (0.2 + 0.8 * i / (double)N0));
    for (i = 0; i < N1; i++)
        ref1[i] = (int16_t)(i < 40u ? (i & 1u ? 9000 : -9000) : 0);
    memset(img, 0xFF, SMP_USER_SIZE);
    memset(h, 0, sizeof *h);
    o0 = enc_zone(d, ref0, N0);
    o1 = enc_zone(d + o0, ref1, N1);
    h->magic = SMP_USER_MAGIC, h->version = 1, h->nz = 2;
    memcpy(h->name, "TESTSLOT", 8);
    h->zone[0] = (smp_zone_t){0, N0, 0, N0 - 1u, 1u << 15, 60 * 16, 0, 0, 0, 64, 0};
    h->zone[1] = (smp_zone_t){o0, N1, 0, N1 - 1u, 1u << 15, 72 * 16, 0, 0, 65, 127, 0};
    h->data_len = o0 + o1;
    h->crc = st_crc32(d, o0 + o1);
    smp_user_scan(k);
}
/* zone i of slot k decoded */
static uint32_t zone_dec(uint32_t k, uint32_t i, int16_t *out)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)SMP_USER_XIP(k);
    const uint8_t *d = SMP_USER_XIP(k) + SMP_USER_DATA + h->zone[i].off;
    int32_t pred = 0, idx = 0;
    uint32_t j;
    for (j = 0; j < h->zone[i].n; j++)
        out[j] = (int16_t)se_dec(d, j, &pred, &idx);
    return h->zone[i].n;
}
/* rms of a - b over n, relative to the rms of b */
static double rel_err(const int16_t *a, const int16_t *b, uint32_t n)
{
    double e = 0, s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        e += (double)(a[i] - b[i]) * (a[i] - b[i]), s += (double)b[i] * b[i];
    return s > 0 ? sqrt(e / s) : 1.0;
}
static int16_t dec0[N0], dec1[N1], orig0[N0];

static int slot_crc_ok(uint32_t k)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)SMP_USER_XIP(k);
    return st_crc32(SMP_USER_XIP(k) + SMP_USER_DATA, h->data_len) == h->crc;
}

int main(void)
{
    const uint32_t K = 4;                            /* USR5 */
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)SMP_USER_XIP(K);
    uint8_t before[N0];
    uint32_t n, i, b1;
    int32_t pk;
    host_tracks_init();
    song.playing = 0, transport_req = 0;

    slot_make(K);
    zone_dec(K, 0, orig0);
    se_k = K, se_zi = 0;
    se_load();
    check(se_nz == 2u && se_z[0].en == N0 && se_z[1].en == N1 && !se_z[0].rev, "the edits start as the slot is: 2 zones, whole");

    /* 1: LOOP and KEYS only */
    memcpy(before, SMP_USER_XIP(K) + SMP_USER_DATA, h->data_len);
    b1 = h->data_len;
    se_z[0].looped = SMP_LOOP_HELD, se_z[0].ls = 6000, se_z[0].le = 9000;
    se_z[0].root16 = 62 * 16, se_z[0].lo = 10, se_z[0].hi = 70;
    se_dirty = 1;
    se_save();
    {
        int32_t pred = 0, idx = 0;
        for (i = 0; i < 6000u; i++)
            se_dec(SMP_USER_XIP(K) + SMP_USER_DATA, i, &pred, &idx);
        check(h->data_len == b1 && !memcmp(before, SMP_USER_XIP(K) + SMP_USER_DATA, b1),
              "LOOP / KEYS only: the ADPCM bytes as they were (no second encode)");
        check(h->zone[0].looped == SMP_LOOP_HELD && h->zone[0].ls == 6000u && h->zone[0].le == 9000u &&
              h->zone[0].pred == pred && h->zone[0].idx == idx,
              "... the loop HELD 6000..9000, its decoder state at L.START as playback restarts it");
        check(h->zone[0].root16 == 62 * 16 && h->zone[0].lo == 10u && h->zone[0].hi == 70u, "... ROOT D4, keys 10..70");
        check(usr_nz[K] == 2u && slot_crc_ok(K) && !strcmp(last_msg, "SAVED"), "... the slot reads back, its CRC right, SAVED");
    }

    /* 2: TRIM 5000..15000 */
    zone_dec(K, 1, dec1);
    se_load();
    se_z[0].st = 5000, se_z[0].en = 15000, se_z[0].looped = 0;
    se_z[0].ls = 5000, se_z[0].le = 14999;
    se_dirty = 1;
    se_save();
    n = zone_dec(K, 0, dec0);
    check(n == 10000u && h->data_len == b1 - (N0 + 1u) / 2u + 5000u,
          "TRIM 5000..15000: 10000 samples, the slot 12050 samples (6025 B) smaller: room back");
    check(rel_err(dec0, orig0 + 5000, 10000) < 0.15, "... it sounds as that part did (encoded again: < 15 % rms error)");
    {
        int16_t z1[N1];
        zone_dec(K, 1, z1);
        check(!memcmp(z1, dec1, sizeof z1) && usr_nz[K] == 2u && slot_crc_ok(K), "... zone 2, not edited, as it was; the CRC right");
    }

    /* 3: REVERSE (the whole of what is left: 10000) */
    slot_make(K);
    zone_dec(K, 0, orig0);
    se_load();
    se_z[0].rev = 1, se_z[0].st = 100, se_z[0].en = N0 - 200;   /* as shown: source 200 .. N0 - 101, backwards */
    se_z[0].ls = 100, se_z[0].le = N0 - 201;
    se_dirty = 1;
    se_save();
    n = zone_dec(K, 0, dec0);
    {
        static int16_t want[N0];
        for (i = 0; i < n; i++)
            want[i] = orig0[N0 - 1u - (100u + i)];
        check(n == N0 - 300u && rel_err(dec0, want, n) < 0.15, "REVERSE (and a trim as shown): the zone backwards (< 15 % rms error)");
    }

    /* 4: NORMALIZE */
    slot_make(K);
    se_load();
    se_z[0].norm = 1;
    se_dirty = 1;
    se_save();
    n = zone_dec(K, 0, dec0);
    for (pk = 0, i = 0; i < n; i++)
        pk = abs(dec0[i]) > pk ? abs(dec0[i]) : pk;
    check(pk > 28000 && pk <= 32767, "NORMALIZE: the peak about 30000 (from 2000)");

    /* 5: FADE IN, FADE OUT */
    slot_make(K);
    zone_dec(K, 0, orig0);
    se_load();
    se_z[0].fin = 200, se_z[0].fout = 300;           /* ms: 4410 and 6615 samples */
    se_dirty = 1;
    se_save();
    n = zone_dec(K, 0, dec0);
    {
        int32_t a = 0, b = 0, m = 0;
        for (i = 0; i < 200u; i++)
            a = abs(dec0[i]) > a ? abs(dec0[i]) : a, b = abs(dec0[n - 1u - i]) > b ? abs(dec0[n - 1u - i]) : b;
        for (i = 0; i < 200u; i++)
            m = abs(orig0[i]) > m ? abs(orig0[i]) : m;
        check(n == N0 && a < m / 10 + 64 && b < 200, "FADE IN 200 ms, FADE OUT 300 ms: the ends near silent");
        check(rel_err(dec0 + 5000, orig0 + 5000, 8000) < 0.15, "... between the fades as before");
    }

    check(irq_mask_n == 5 && irq_unmask_n == 5 && !irq_masked && !erased_while_unmasked,
          "every SAVE: the audio interrupt off while the delay line is the stage, back on after");
    {
        uint32_t z = 0;
        for (i = 0; i < DLY_LEN; i++)
            z |= (uint16_t)dly_buf[i];
        check(!z, "... the delay line cleared after (no burst of the stage through the delay)");
    }

    {   /* PHREAK 1.6: the preview, the zone as edited before SAVE */
        static int32_t L[64], R[64];
        uint32_t outs = 0, nz_out = 0, j, saves = irq_mask_n;
        int64_t e1 = 0;
        slot_make(K);
        se_load();
        se_z[0].st = 2000, se_z[0].en = 6000, se_z[0].rev = 0, se_z[0].looped = 0;
        se_z[0].ls = 2000, se_z[0].le = 5999;
        memcpy(before, SMP_USER_XIP(K), 512);
        se_pv_start();
        check(se_pv.on && se_dly_off && se_pv.z.n == 4000u && !memcmp(before, SMP_USER_XIP(K), 512) && irq_mask_n == saves,
              "PREVIEW: PLAY builds the zone as edited (4000 samples) in the delay line; the flash untouched");
        while (se_pv.on && outs < 20000u) {
            memset(L, 0, sizeof L), memset(R, 0, sizeof R);
            se_pv_render(L, R, 64);
            for (j = 0; j < 64u; j++)
                nz_out += L[j] != 0, e1 += (int64_t)L[j] * L[j];
            outs += 64;
        }
        check(!se_pv.on && outs >= 7990u && outs <= 8064u && nz_out > 6000u && e1 > 0,
              "PREVIEW: plays the trimmed 4000 samples (8000 at 44.1 kHz) and ends");
        check(se_dly_off, "... the delay line still the editor's until the editor gives it back");
        se_pv_stop();
        {
            uint32_t z = 0;
            for (j = 0; j < DLY_LEN; j++)
                z |= (uint16_t)dly_buf[j];
            check(!se_dly_off && !z, "... given back: the delay on again, its line cleared");
        }
        se_z[0].looped = 1;                          /* LOOP: it keeps going until stopped */
        se_pv_start();
        for (outs = 0; outs < 30000u; outs += 64)
            se_pv_render(L, R, 64);
        check(se_pv.on, "PREVIEW with LOOP: still playing after 3x its length");
        se_z[0].looped = 0;
        se_pv_start();                               /* (a running preview: rebuilt) */
        se_dirty = 1;
        se_save();
        check(!se_pv.on && !se_dly_off && h->zone[0].n == 4000u, "SAVE while it previews: the preview stops, the slot written");
    }

    song.playing = 1;
    se_load();
    se_z[0].norm = 1, se_dirty = 1;
    b1 = irq_mask_n;
    se_save();
    check(irq_mask_n == b1 && !strcmp(last_msg, "STOP THE SONG FIRST"), "the song playing: nothing written, said so");
    song.playing = 0;

    printf("smpedit: %s\n", fails ? "FAILED" : "PASS");
    return fails != 0;
}
