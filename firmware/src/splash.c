/* SPDX-License-Identifier: GPL-3.0-only */
/* PHREAK boot splash: the logo (tools/gen_phreak_logo.py -> phreak_logo.h: 16 colours, RLE runs of
 * (run - 1) << 4 | colour), drawn into the canvas in bands of at most 120 rows. */
#ifndef UI_SMALL                 /* (2.7) compiled for size: not the audio path (gfx.c UI_SMALL); the host tests: nothing */
#define UI_SMALL
#endif
#ifndef FELUCCA_LOGO_TEXT       /* (2.7) 1: the boot logo as text, "P" and "HREAK" in the track colours (4.6 KB of flash
                                 * less: tools/build.py builds so by itself when the flash is full) */
#define FELUCCA_LOGO_TEXT 0
#endif
#if FELUCCA_LOGO_TEXT
UI_SMALL static void sloop_logo_draw(uint32_t y0)
{
    static const char L[6] = {'P', 'H', 'R', 'E', 'A', 'K'};
    uint32_t i, w = text_w(&FONT_L, "PHREAK"), k = w * 3u <= 232u ? 3u : 2u, h = 32u, off, y, x;
    int32_t px = 0;
    char c[2] = {0, 0};
    cv_begin(240, h * k, C_BLACK);
    for (i = 0; i < 6u; i++) {                          /* small, at the top left: P white, H R E A K the tracks' colours */
        c[0] = L[i];
        px = cv_text(px, 0, &FONT_L, c, i ? TE_COL_DEF[(i - 1u) % 4u] : C_WHITE);
    }
    off = (240u - w * k) / 2u;
    for (y = h * k; y-- > 0;)                           /* then k times as big, in place (from the end: nothing read is
                                                         * overwritten before) */
        for (x = w * k; x-- > 0;)
            cv_px[y * 240u + off + x] = cv_px[(y / k) * 240u + x / k];
    for (y = 0; y < h * k; y++)                         /* (the small one's left-over columns) */
        for (x = 0; x < off; x++)
            cv_px[y * 240u + x] = swap16(C_BLACK);
    cv_blit(0, y0 + 60u);
}
#else
#include "phreak_logo.h"
UI_SMALL static void sloop_logo_draw(uint32_t y0)
{
    uint32_t r0;
    for (r0 = 0; r0 < PHREAK_SPLASH_H; r0 += 120u) {
        uint32_t rows = PHREAK_SPLASH_H - r0 > 120u ? 120u : PHREAK_SPLASH_H - r0;
        uint32_t lo = r0 * PHREAK_SPLASH_W, hi = (r0 + rows) * PHREAK_SPLASH_W, pos = 0, i;
        cv_begin(PHREAK_SPLASH_W, rows, C_BLACK);
        for (i = 0; i < sizeof PHREAK_SPLASH_RLE && pos < hi; i++) {
            uint32_t run = (PHREAK_SPLASH_RLE[i] >> 4) + 1u, c = PHREAK_SPLASH_RLE[i] & 15u, p;
            if (c && pos + run > lo)
                for (p = pos < lo ? lo : pos; p < pos + run && p < hi; p++)
                    cv_px[p - lo] = swap16(PHREAK_SPLASH_PAL[c]);
            pos += run;
        }
        cv_blit((240u - PHREAK_SPLASH_W) / 2u, y0 + r0);
    }
}
#endif

/* the boot screen: the logo, PHREAK's number and its author (dag, dagnazty.dev), where it comes from */
UI_SMALL static void sloop_splash(void)
{
    static const char *const v = PHREAK_RELEASE, *const by = "  by dag", *const from = "based on sloop + felucca";
    int32_t x;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sloop_logo_draw(10);
    cv_begin(240, 36, C_BLACK);
    x = 120 - (int32_t)(text_w(&FONT_S, v) + text_w(&FONT_S, by)) / 2;   /* "1.1  by dag": the number, its author */
    x = cv_text(x, 0, &FONT_S, v, RGB(196, 196, 204));
    cv_text(x, 0, &FONT_S, by, RGB(255, 98, 26));
    cv_text(120 - text_w(&FONT_S, from) / 2, 18, &FONT_S, from, RGB(96, 96, 104));
    cv_blit(0, 202);
}
