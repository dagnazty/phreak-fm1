/* SPDX-License-Identifier: GPL-3.0-only */
/* LOAD FROM USB (firmware/src/usbstick.c) on the host: a FAT32 disk image instead of the stick (its
 * sectors through US_TEST_READ), the user slots in a RAM flash. Finds SAMPLES/, lists its folders, loads each into
 * a slot (opening folders as the screen does: every folder with WAVs, at any depth) and writes the slot to
 * OUTDIR/slot_<folder>.bin for tests/usbstick_check.py. The USB transfers themselves
 * are not run here (no SIE); they were checked on an FM-1 with the probe that came before this (test build 0.4).
 *   usbstick_test IMAGE OUTDIR */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_SMALL
static FILE *img;
static int test_read(uint32_t lba, uint8_t *d)
{
    return fseek(img, (long)lba * 512, SEEK_SET) || fread(d, 1, 512, img) != 512;
}
#define US_TEST_READ test_read

/* ---- what usbstick.c takes from the firmware */
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i + 1 < max)
        d[i] = s[i], i++;
    d[i] = 0;
}
static unsigned str_len(const char *s) { return (unsigned)strlen(s); }
static void fmt_int(char *b, int32_t v) { sprintf(b, "%d", (int)v); }
typedef struct {
    uint32_t off, n, ls, le, rate;
    int16_t root16, pred;
    uint8_t idx, lo, hi, looped;
} smp_zone_t;
#define SMP_USER_SLOTS 4
#define SMP_USER_SIZE (68u * 1024u)
#define SMP_USER_OFF(k) ((uint32_t)(k) * SMP_USER_SIZE)
#define SMP_USER_DATA 512u
#define SMP_USER_MAGIC 0x504D5346u
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t nz, rsv;
    char name[8];
    uint32_t data_len, crc, rsv2[2];
    smp_zone_t zone[16];
} smp_user_hdr_t;
static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
static const int8_t IMA_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const uint8_t LANE_NOTE[16] = {36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56};
static uint8_t flash[SMP_USER_SLOTS * SMP_USER_SIZE];
static int flash_ok = 1;
static int fl_write(uint32_t off, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        flash[off + i] &= s[i];                             /* (a NOR flash: bits only cleared) */
    return 0;
}
static int ed_smp_erase(uint32_t k, uint32_t all)
{
    (void)all;
    memset(flash + SMP_USER_OFF(k), 0xFF, SMP_USER_SIZE);
    return 0;
}
static void ed_smp_inval(uint32_t k) { (void)k; }
/* the hardware: never reached by these tests */
enum { S_FADDR = 0, S_POWER = 1, S_INDEX = 14, S_TXMAXP = 16, S_RXMAXP = 19, S_CSR0 = 17, S_TXCSR1 = 17, S_RXCSR1 = 20, S_COUNT0 = 22,
       S_RXCOUNT1 = 22 };
static void sie_wr(uint32_t r, uint32_t v) { (void)r, (void)v; }
static uint32_t sie_rd(uint32_t r) { return (void)r, 0; }
#define FM1_TICKS_PER_US 1u
#define FM1_NENC 7u
static uint32_t fm1_ticks(void) { return 0; }
static void fm1_wdt_feed(void) {}
static void fm1_delay_us(uint32_t us) { (void)us; }
static void fm1_usb_host_off(void) {}
static void fm1_usb_host_on(void *p) { (void)p; }
static void fm1_usb_ep0_send(void *p, uint32_t n) { (void)p, (void)n; }
static void fm1_usb_ep_send(uint32_t e, void *p, uint32_t n) { (void)e, (void)p, (void)n; }
static void fm1_usb_ep_rxbuf(uint32_t e, void *p) { (void)e, (void)p; }
static void fm1_usb_rx_sync(void) {}
static void fm1_input_scan(void) {}
static int32_t fm1_enc_take(uint32_t e) { return (void)e, 0; }
static uint32_t fm1_input_edges(uint32_t *r) { return (void)r, 0; }
static struct { uint32_t pending; } bootguard;
#define FL_FAR(f) f
static uint32_t fl_jedec_ram(void) { return 0x856014u; }
static void lcd_init(void) {}
static void lcd_fill(int x, int y, int w, int h, int c) { (void)x, (void)y, (void)w, (void)h, (void)c; }
static int FONT_S;
#define C_WHITE 1
#define C_BLACK 0
#define RGB(r, g, b) 2
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const int *f, const char *s, uint16_t c, int a)
{
    (void)x, (void)y, (void)w, (void)f, (void)c, (void)a;
    if (s && *s)
        fprintf(stderr, "  screen: %s\n", s);
}

#include "../firmware/src/usbstick.c"

static int slots;
/* every folder under folder us_dep, depth first, as OCT+ opens them; a folder with WAVs loads into the next slot */
static int walk(const char *outdir)
{
    uint32_t i, nd, clu[US_DIRS];
    char name[US_DIRS][US_NAME + 1], path[600];
    FILE *o;
    if (us_open())
        return printf("FAIL list %s: %s\n", us_pname[us_dep], us_err), 1;
    if (us_nw) {
        uint32_t k = (uint32_t)slots++ % SMP_USER_SLOTS;
        printf("%*s%s/ (%u WAVs) -> USR%u: ", (int)us_dep * 2, "", us_pname[us_dep], (unsigned)us_nw, (unsigned)k + 1);
        if (us_load(us_pclu[us_dep], us_pname[us_dep], k))
            return printf("FAIL %s\n", us_err), 1;
        printf("%s (%u files, %u bytes)\n", us_err, (unsigned)us_nf, (unsigned)us_dpos);
        snprintf(path, sizeof path, "%s/slot_%s.bin", outdir, us_pname[us_dep]);
        if (!(o = fopen(path, "wb")))
            return 1;
        fwrite(flash + SMP_USER_OFF(k), 1, SMP_USER_SIZE, o);
        fclose(o);
        if (us_open())                                     /* (as the screen: the lists again after a load) */
            return 1;
        {   /* (PHREAK 1.2) its last file alone (OCT+ on it, then USR k): one zone over the whole keyboard */
            const smp_user_hdr_t *h = (const smp_user_hdr_t *)(flash + SMP_USER_OFF(k));
            uint32_t f = us_nl - 1u, ok = 1, j;
            int note;
            for (j = 1; j < us_nl; j++)
                ok &= !us_lt(us_lname[j], us_lname[j - 1u]);
            if (us_nl != (us_nw < US_FILES ? us_nw : US_FILES) || !ok)
                return printf("FAIL the folder's files listed: %u of %u\n", (unsigned)us_nl, (unsigned)us_nw), 1;
            note = us_note(us_lname[f]);
            if (us_load_one(f, k))
                return printf("FAIL one file %s: %s\n", us_lname[f], us_err), 1;
            ok = h->magic == SMP_USER_MAGIC && h->nz == 1u && h->zone[0].lo == 0u && h->zone[0].hi == 127u &&
                 h->zone[0].root16 == (note < 0 ? 60 : note) * 16 && h->zone[0].n > 1u && h->name[0] == us_lname[f][0];
            for (j = 0; j < 8u && us_lname[f][j] && us_lname[f][j] != '.'; j++)
                ok &= h->name[j] == us_lname[f][j];
            printf("%*s  %s alone -> USR%u: %s (%u samples)\n", (int)us_dep * 2, "", us_lname[f], (unsigned)k + 1,
                   ok ? "ok" : "FAIL", (unsigned)h->zone[0].n);
            if (!ok)
                return 1;
            if (us_open())
                return 1;
        }
    } else {
        printf("%*s%s/\n", (int)us_dep * 2, "", us_pname[us_dep]);
    }
    nd = us_nd;
    memcpy(clu, us_dclu, sizeof clu);
    memcpy(name, us_dname, sizeof name);
    for (i = 0; i < nd; i++) {
        if (us_dep + 1u >= US_DEPTH)
            break;
        us_dep++;
        us_pclu[us_dep] = clu[i];
        str_cpy(us_pname[us_dep], name[i], US_NAME + 1);
        if (walk(outdir))
            return 1;
        us_dep--;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3 || !(img = fopen(argv[1], "rb")))
        return fprintf(stderr, "usbstick_test IMAGE OUTDIR\n"), 2;
    if (us_volume())
        return printf("FAIL volume: %s\n", us_err), 1;
    us_found = 0;
    if (us_dir(us_root, us_find_samples))
        return printf("FAIL root: %s\n", us_err), 1;
    us_dep = 0;
    us_pclu[0] = us_found ? us_found : us_root;           /* (usbstick_main: SAMPLES/, else the stick's top) */
    str_cpy(us_pname[0], us_found ? "SAMPLES" : "USB STICK", US_NAME + 1);
    return walk(argv[2]);
}
