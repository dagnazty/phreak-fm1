/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/storage.c against a simulated NOR flash:
 * erase -> 0xFF, program can only clear bits, page writes must not wrap. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];
static int fail_after = -1;            /* torn-write injection: stop after N programs */

static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static int side_tests(void)                          /* 2.7: the side record (project.c: the theme) */
{
    int bad = 0, n;
    char p1[300], p2[300], got[300], side[36], sg[36], ff[36];
    memset(p1, 'P', sizeof p1);
    memset(p2, 'Q', sizeof p2);
    memset(side, 'S', sizeof side);
    memset(ff, 0xFF, sizeof ff);
    st_side = side;
    st_side_len = sizeof side;
    bad += check("side: saved with a copy", st_save(OBJ_SETTINGS, p1, sizeof p1) == 0);
    st_side = 0;
    n = st_load(OBJ_SETTINGS, got, sizeof got);
    bad += check("side: the payload as without it", n == (int)sizeof p1 && !memcmp(got, p1, sizeof p1));
    bad += check("side: read back", st_side_load(OBJ_SETTINGS, sg, sizeof sg) == 0 && !memcmp(sg, side, sizeof sg));
    bad += check("side: an older save (none): erased bytes",
                 st_save(OBJ_SETTINGS, p2, sizeof p2) == 0 && st_side_load(OBJ_SETTINGS, sg, sizeof sg) == 0 &&
                 !memcmp(sg, ff, sizeof sg));
    st_side = side;
    fail_after = 3;                                  /* payload (2 pages) and side written, header torn */
    st_save(OBJ_SETTINGS, p1, sizeof p1);
    fail_after = -1;
    st_side = 0;
    n = st_load(OBJ_SETTINGS, got, sizeof got);
    bad += check("side: torn save keeps the last copy, its side",
                 n == (int)sizeof p2 && !memcmp(got, p2, sizeof p2) && st_side_load(OBJ_SETTINGS, sg, sizeof sg) == 0 &&
                 !memcmp(sg, ff, sizeof sg));
    bad += check("side: no more than the gap", st_side_load(OBJ_SETTINGS, sg, ST_SIDE_MAX + 1u) < 0);
    return bad;
}

/* sections E..H: four objects in five sectors (storage.c ST_POOL) */
static int pool_tests(void)
{
    static uint8_t v[4][3816], got[3816], before[0x100000];
    uint32_t i, k, o, step, ok = 1, torn_ok = 1, outside_ok = 1, saved = 0;
    int n, bad = 0;
    for (k = 0; k < 4u; k++)
        bad += check("pool: empty: nothing loads", st_load(OBJ_SECT4 + k, got, sizeof got) == -1);
    memcpy(before, nor, sizeof nor);
    srand(7);
    for (step = 0; step < 400u; step++) {             /* random saves of E..H: each loads its last one */
        o = (uint32_t)rand() % 4u;
        memset(v[o], (int)(step & 0xFF), sizeof v[o]);
        v[o][0] = (uint8_t)o;
        v[o][1] = (uint8_t)(step >> 8);
        if (rand() % 5 == 0) {                        /* a torn save: the one before stays */
            fail_after = rand() % 16;
            st_save(OBJ_SECT4 + o, v[o], sizeof v[o]);
            fail_after = -1;
            memset(v[o], 0, sizeof v[o]);
            n = st_load(OBJ_SECT4 + o, v[o], sizeof v[o]);   /* (the copy now in charge: an old one, or this) */
            torn_ok &= n == (int)sizeof v[o] || (n == -1 && !((saved >> o) & 1u));
            if (n < 0)
                saved &= ~(1u << o);
            continue;
        }
        if (st_save(OBJ_SECT4 + o, v[o], sizeof v[o]) != 0)
            ok = 0;
        saved |= 1u << o;
        for (k = 0; k < 4u; k++) {
            n = st_load(OBJ_SECT4 + k, got, sizeof got);
            if ((saved >> k) & 1u ? n != (int)sizeof got || memcmp(got, v[k], sizeof got) : n != -1)
                ok = 0;
        }
    }
    for (i = 0; i < sizeof nor; i++)                  /* only the five pool sectors were touched */
        if (nor[i] != before[i] && !((i >= 0x93000u && i < 0x97000u) || (i >= 0xFB000u && i < 0xFC000u)))
            outside_ok = 0;
    bad += check("pool: 400 random saves of E..H load back", ok);
    bad += check("pool: a torn save leaves a whole copy", torn_ok);
    bad += check("pool: nothing written outside its five sectors", outside_ok);
    bad += check("pool: A..D untouched (2.6 erase's project C as it was)",
                 st_load(OBJ_PROJECT0 + 2u, got, sizeof got) == (int)sizeof got && got[0] == 'P');
    return bad;
}

int main(void)
{
    char a[600], b[600], got[600];
    int bad = 0, n;
    memset(nor, 0xFF, sizeof nor);
    memset(a, 'A', sizeof a);
    memset(b, 'B', sizeof b);
    bad += check("empty flash loads nothing", st_load(OBJ_PROJECT0, got, sizeof got) < 0);
    bad += check("save A", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save B", st_save(OBJ_PROJECT0, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns B (newer seq)", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 1;                                  /* payload page 1 written, the rest torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("torn save keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 3;                                  /* all payload pages, header torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("header not written keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    bad += check("save A again", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    nor[st_sector(OBJ_PROJECT0, 0) + ST_PAYLOAD_OFF + 10] ^= 0x01;   /* bit rot in one copy */
    nor[st_sector(OBJ_PROJECT0, 1) + ST_PAYLOAD_OFF + 10] ^= 0x01;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("both copies corrupt -> nothing", n < 0);
    bad += check("other objects untouched", st_load(OBJ_PROJECT0 + 1, got, sizeof got) < 0);
    bad += check("settings save/load",
                 st_save(OBJ_SETTINGS, "hello", 5) == 0 && st_load(OBJ_SETTINGS, got, 5) == 5 && !memcmp(got, "hello", 5));
    bad += check("CRC-32 is zlib's (check value 0xCBF43926)", st_crc32("123456789", 9) == 0xCBF43926u);
    memset(nor, 0xFF, sizeof nor);
    st_save(OBJ_PROJECT0 + 2, a, sizeof a);
    st_save(OBJ_PROJECT0 + 2, b, sizeof b);              /* B is newer, in the other copy */
    {
        st_hdr_t h;
        int cur = st_current(OBJ_PROJECT0 + 2, &h);
        nor[st_sector(OBJ_PROJECT0 + 2, (uint32_t)cur) + ST_PAYLOAD_OFF + 300] ^= 0x10;   /* rot in the newer copy */
    }
    n = st_load(OBJ_PROJECT0 + 2, got, sizeof got);
    bad += check("newer copy rotten -> the older one loads", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save over the rotten copy", st_save(OBJ_PROJECT0 + 2, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0 + 2, got, sizeof got);
    bad += check("... loads the new data", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    nor[st_sector(OBJ_PROJECT0 + 2, 0) + 8] ^= 0x01;    /* both headers broken */
    nor[st_sector(OBJ_PROJECT0 + 2, 1) + 8] ^= 0x01;
    bad += check("both headers broken -> nothing", st_load(OBJ_PROJECT0 + 2, got, sizeof got) < 0);
    {   /* every copy of every object in the Felucca regions, off the sample slots (0xA0000..0xDBFFF) and
         * the update staging (0xE0000..0xE4FFF; the FM6 bank's two sectors follow it), and no two sectors shared */
        uint32_t o, c, o2, c2, inside = 1, apart = 1;
        for (o = 0; o < OBJ_COUNT; o++)
            for (c = 0; c < ST_COPIES(o); c++) {
                uint32_t a = st_sector(o, c);
                int data = a >= 0x97000u && a + 4096u <= 0xA0000u, ups = a >= 0xDC000u && a + 4096u <= 0xE0000u;
                int glob = a >= 0xFC000u && a + 4096u <= 0xFF000u, fm6 = a >= 0xE5000u && a + 4096u <= 0xE7000u;
                int pool = ST_POOLED(o) && ((a >= 0x93000u && a + 4096u <= 0x97000u) || a == 0xFB000u);  /* E..H */
                /* D2, D3 (OBJ_XD0..XDW): the last 12 KiB of each user sample slot, past its 68 KiB of samples */
                int xdt = o >= OBJ_XD0 && o <= OBJ_XDW &&
                          ((a >= 0xA0000u + 0x11000u && a + 4096u <= 0xB4000u) || (a >= 0xB4000u + 0x11000u && a + 4096u <= 0xC8000u) ||
                           (a >= 0xC8000u + 0x11000u && a + 4096u <= 0xDC000u) || (a >= 0xE7000u + 0x11000u && a + 4096u <= 0xFB000u));
                inside &= (ST_POOLED(o) ? pool : o >= OBJ_XD0 && o <= OBJ_XDW ? xdt : data || ups || glob || fm6) && !(a & 0xFFFu);
                for (o2 = 0; o2 < OBJ_COUNT; o2++)       /* (the pool's sectors: shared by E..H only, by design) */
                    for (c2 = 0; c2 < ST_COPIES(o2); c2++)
                        if ((o2 != o || c2 != c) && st_sector(o2, c2) == a && !(ST_POOLED(o) && ST_POOLED(o2)))
                            apart = 0;
            }
        bad += check("data stays in the Felucca regions (autosave too)", inside);
        bad += check("every object copy has its own sector", apart);
    }
    {   /* SLOOP 2.5: the settings in two parts (persist_t, then the SYN kits): one record, A/B as any other */
        static char p1[88], p2[1464], all[88 + 1464], got2[88 + 1464];
        memset(p1, 'P', sizeof p1);
        memset(p2, 'K', sizeof p2);
        memcpy(all, p1, sizeof p1);
        memcpy(all + sizeof p1, p2, sizeof p2);
        bad += check("2.5: save in two parts", st_save2(OBJ_SETTINGS, p1, sizeof p1, p2, sizeof p2) == 0);
        n = st_load(OBJ_SETTINGS, got2, sizeof got2);
        bad += check("2.5: ... loads as one object", n == (int)sizeof all && !memcmp(got2, all, sizeof all));
        n = st_load(OBJ_SETTINGS, got2, sizeof p1);
        bad += check("2.5: ... a 2.4 load takes its part only", n == (int)sizeof p1 && !memcmp(got2, p1, sizeof p1));
        memset(p2, 'L', sizeof p2);
        fail_after = 4;                              /* a torn second save: the first copy stays */
        st_save2(OBJ_SETTINGS, p1, sizeof p1, p2, sizeof p2);
        fail_after = -1;
        n = st_load(OBJ_SETTINGS, got2, sizeof got2);
        bad += check("2.5: ... a torn save keeps the last one", n == (int)sizeof all && !memcmp(got2, all, sizeof all));
        bad += check("2.5: ... too big for a sector: refused", st_save2(OBJ_SETTINGS, p2, 3000, p2, 1000) == -1);
    }
    {   /* 2.6: SAVE > PROJECT > ERASE (project.c project_erase), as the editor's restore of an empty slot: an object
         * of no bytes; a load reads 0 bytes (proj_fetch: no project), the next save works as ever */
        static uint8_t a2[3816], got3[3816];
        memset(a2, 'P', sizeof a2);
        bad += check("2.6 erase: a project saved", st_save(OBJ_PROJECT0 + 2u, a2, sizeof a2) == 0);
        bad += check("2.6 erase: the empty object written", st_save(OBJ_PROJECT0 + 2u, a2, 0) == 0);
        n = st_load(OBJ_PROJECT0 + 2u, got3, sizeof got3);
        bad += check("2.6 erase: loads 0 bytes (an empty slot)", n == 0);
        bad += check("2.6 erase: saved again", st_save(OBJ_PROJECT0 + 2u, a2, sizeof a2) == 0 &&
                     st_load(OBJ_PROJECT0 + 2u, got3, sizeof got3) == (int)sizeof a2 && !memcmp(got3, a2, sizeof a2));
    }
    bad += side_tests();
    bad += pool_tests();
    printf("%s\n", bad ? "STORAGE TEST FAILED" : "storage test passed");
    return bad != 0;
}
