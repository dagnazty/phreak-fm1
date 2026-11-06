/* SPDX-License-Identifier: GPL-3.0-only */
/* LOAD FROM USB: hold OCT+ alone while switching on. A USB stick (FAT32) on the USB-C port, through an
 * OTG adapter that also powers it; each folder in SAMPLES/ loads into a user sample slot (USR1..USR6), as the web
 * editor's upload does (eng_sample.c, web/EDITOR_PROTOCOL.md): its WAV files (8/16/24/32-bit PCM, mono or stereo,
 * any rate) mixed to mono, at 22050 Hz, IMA ADPCM, up to 16 files and the slot's 80 KiB (about 7 s).
 * The files sorted by name: one file plays over the whole keyboard; when every name has a note (PAD C3.WAV) they
 * split the keyboard between their notes; else they go to the drum lanes in order (KICK, KICK 2, SNARE, ...: a USR
 * kit), each also on its lane's key. (PHREAK 1.2) A folder also lists its WAV files: OCT+ on one, then a slot, loads
 * that file alone, over the whole keyboard (its note from its name, else C4): a sound for a synth track (SAMPLE,
 * SET USR1..4). PLAY on a file (or on its slot list) previews it through the FM-1's output, streamed from the
 * stick: any button or knob stops it (us_preview).
 * USB0 is a full-speed host (hal/fm1_usb.h fm1_usb_host_on; the transfers after the AC79 SDK's usb_ctlXfer,
 * usb_h_ep_write and usb0_h_ep_read, USB0 branches), the stick a bulk-only SCSI device, read a sector at a time.
 * Like USB RESCUE: no audio, no TIMER5, no settings. Switch off to leave; the next power-on plays the slots. */
enum { US_DEVCTL = 15, US_TXTYPE = 24, US_RXTYPE = 26, US_RXCOUNT2 = 23, US_EOUT = 1, US_EIN = 2 };
#define US_NAME 24u                       /* characters kept of a name */
#define US_DIRS 24u
static uint8_t us_ep0[64 + 4] __attribute__((aligned(4)));
static uint8_t us_txb[64] __attribute__((aligned(4)));
static uint8_t us_rxb[64 + 4] __attribute__((aligned(4)));
static uint8_t us_sec[512] __attribute__((aligned(4)));
static uint8_t us_buf[64], us_cbw[32];
static uint32_t us_mps, us_bin, us_bout, us_lba = ~0u, us_tag, us_root;
static uint32_t us_spc, us_fat, us_data;  /* FAT32: sectors a cluster, the FAT's and the data's first sector */
static const char *us_err;
#define US_DIM RGB(110, 110, 110)

UI_SMALL static void us_wait_ms(uint32_t ms)
{
    while (ms--) {
        fm1_wdt_feed();
        fm1_delay_us(1000u);
    }
}

UI_SMALL static void us_line(uint32_t row, const char *s, uint16_t c) { draw_text_box(0, 4 + row * 21, 240, &FONT_S, s, c, 1); }

UI_SMALL static uint32_t us_le(const uint8_t *p, uint32_t n)   /* little-endian, n bytes */
{
    uint32_t v = 0;
    while (n--)
        v = v << 8 | p[n];
    return v;
}

UI_SMALL static char *us_cat(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

UI_SMALL static char *us_num(char *p, uint32_t v)
{
    fmt_int(p, (int32_t)v);
    return p + str_len(p);
}

UI_SMALL static int us_fail(const char *e)
{
    us_err = e;
    return -1;
}

/* wait for a set bit (or a cleared one, clr) of an SIE register; stall / error bits fail */
UI_SMALL static int us_poll(uint32_t reg, uint32_t set, uint32_t clr, uint32_t bad)
{
    uint32_t t = fm1_ticks(), r;
    for (;;) {
        fm1_wdt_feed();
        r = sie_rd(reg);
        if (r & bad) {
            sie_wr(reg, 0);
            return us_fail("NO ANSWER");
        }
        if ((r & set) || (clr && !(r & clr)))
            return 0;
        if (fm1_ticks() - t > 1000u * 1000u * FM1_TICKS_PER_US)
            return us_fail("TIMEOUT");
    }
}

UI_SMALL static int us_csr0(uint32_t go, uint32_t set, uint32_t clr)
{
    sie_wr(S_INDEX, 0);
    sie_wr(S_CSR0, go);
    return us_poll(S_CSR0, set, clr, 0x14u);              /* RxStall, Error */
}

/* a control request to addr, its data stage (if any) in: the bytes read into us_buf, or -1 */
UI_SMALL static int us_ctl(uint32_t addr, uint32_t rt, uint32_t req, uint32_t val, uint32_t idx, uint32_t len)
{
    uint32_t got = 0, n;
    uint8_t *s = us_ep0;
    sie_wr(S_FADDR, addr);
    s[0] = (uint8_t)rt, s[1] = (uint8_t)req, s[2] = (uint8_t)val, s[3] = (uint8_t)(val >> 8);
    s[4] = (uint8_t)idx, s[5] = (uint8_t)(idx >> 8), s[6] = (uint8_t)len, s[7] = (uint8_t)(len >> 8);
    fm1_usb_ep0_send(us_ep0, 8);
    if (us_csr0(0x0Au, 0, 0x02u))                          /* SetupPkt | TxPktRdy */
        return -1;
    while (got < len) {
        if (us_csr0(0x20u, 0x01u, 0))                      /* ReqPkt -> RxPktRdy */
            return -1;
        n = sie_rd(S_COUNT0);
        fm1_usb_rx_sync();
        if (n > len - got)
            n = len - got;
        memcpy(us_buf + got, us_ep0, n);
        sie_wr(S_CSR0, 0);
        got += n;
        if (n < us_mps)
            break;
    }
    if (len) {
        fm1_usb_ep0_send(us_ep0, 0);
        return us_csr0(0x42u, 0, 0x02u) ? -1 : (int)got;   /* status out */
    }
    if (us_csr0(0x60u, 0x01u, 0))                          /* status in */
        return -1;
    sie_wr(S_CSR0, 0);
    return 0;
}

UI_SMALL static int us_bulk_out(const uint8_t *p, uint32_t n)
{
    uint32_t k;
    for (; n; p += k, n -= k) {
        k = n < 64u ? n : 64u;
        memcpy(us_txb, p, k);
        sie_wr(S_INDEX, US_EOUT);
        sie_wr(US_TXTYPE, 0x20u | us_bout);                /* bulk, the stick's endpoint */
        fm1_usb_ep_send(US_EOUT, us_txb, k);
        sie_wr(S_TXCSR1, 1);
        if (us_poll(S_TXCSR1, 0, 1, 0x24u))                /* RxStall, Error */
            return -1;
    }
    return 0;
}

/* up to n bytes in: the count read, or -1 (a stall: the endpoint cleared, -2) */
UI_SMALL static int us_bulk_in(uint8_t *d, uint32_t n)
{
    uint32_t got = 0, k;
    while (got < n) {
        sie_wr(S_INDEX, US_EIN);
        sie_wr(US_RXTYPE, 0x20u | us_bin);
        sie_wr(S_RXCSR1, 0x20);                            /* ReqPkt */
        if (us_poll(S_RXCSR1, 1, 0, 0x44u)) {              /* RxStall, Error */
            us_ctl(1, 0x02u, 1, 0, us_bin | 0x80u, 0);     /* CLEAR_FEATURE ENDPOINT_HALT */
            sie_wr(S_INDEX, US_EIN);
            sie_wr(S_RXCSR1, 0x80);                        /* the toggle back to DATA0 */
            return -2;
        }
        k = sie_rd(S_RXCOUNT1) | sie_rd(US_RXCOUNT2) << 8;
        fm1_usb_rx_sync();
        if (k > n - got)
            k = n - got;
        memcpy(d + got, us_rxb, k);
        sie_wr(S_RXCSR1, 0x10);                            /* the packet taken */
        got += k;
        if (k < 64u)
            break;
    }
    return (int)got;
}

/* a SCSI command, its data (if any) in: bulk-only transport (CBW, data, CSW). 0, 1 the stick refused it, -1 */
UI_SMALL static int us_scsi(const uint8_t *cdb, uint32_t clen, uint8_t *d, uint32_t n)
{
    uint8_t *c = us_cbw;
    memset(c, 0, 31);
    memcpy(c, "USBC", 4);
    us_tag++;
    memcpy(c + 4, &us_tag, 4);
    memcpy(c + 8, &n, 4);
    c[12] = n ? 0x80u : 0;
    c[14] = (uint8_t)clen;
    memcpy(c + 15, cdb, clen);
    if (us_bulk_out(c, 31))
        return -1;
    if (n && us_bulk_in(d, n) == -1)
        return -1;
    if (us_bulk_in(c, 13) != 13 && us_bulk_in(c, 13) != 13)  /* the CSW (again after a stall) */
        return -1;
    if (c[3] == 'S' && !c[12])
        return 0;
    us_err = "STICK ERROR";
    return 1;                                              /* (the command went through; the stick said no) */
}

UI_SMALL static int us_read(uint32_t lba)                           /* a 512-byte sector into us_sec */
{
    uint8_t cdb[10] = {0x28, 0, (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
                       0, 0, 1, 0};
    if (lba == us_lba)
        return 0;
    us_lba = ~0u;
#ifdef US_TEST_READ                                        /* tests/usbstick_test.c: a disk image */
    if (US_TEST_READ(lba, us_sec))
        return us_fail("READ ERROR");
    us_lba = lba;
    return 0;
#endif
    if (us_scsi(cdb, 10, us_sec, 512))
        return -1;
    us_lba = lba;
    return 0;
}

UI_SMALL static uint32_t us_next(uint32_t clu)                      /* the cluster after clu (>= 0x0FFFFFF8: none) */
{
    if (us_read(us_fat + clu / 128u))
        return 0x0FFFFFFFu;
    return us_le(us_sec + clu % 128u * 4u, 4) & 0x0FFFFFFFu;
}

UI_SMALL static uint32_t us_lba_of(uint32_t clu) { return us_data + (clu - 2u) * us_spc; }

UI_SMALL static int us_volume(void);
/* the stick: host mode, reset, address 1, its bulk endpoints, ready, the FAT32 volume. 0 ok */
UI_SMALL static int us_mount(void)
{
    static const uint8_t INQ[6] = {0x12, 0, 0, 0, 36, 0}, TUR[6] = {0}, SENSE[6] = {0x03, 0, 0, 0, 18, 0};
    uint32_t t, i;
    uint8_t *e;
    us_err = "PLUG IN A USB STICK";
    us_line(3, "LOOKING FOR A STICK", US_DIM);
    us_lba = ~0u;
    fm1_usb_host_off();
    us_wait_ms(10);
    fm1_usb_host_on(us_ep0);
    us_wait_ms(10);
    fm1_usb_ep_rxbuf(US_EIN, us_rxb);
    sie_wr(US_DEVCTL, 3);                                  /* session + host request */
    for (t = 0; !(sie_rd(US_DEVCTL) & 0x40u); t++)         /* a full-speed device pulls DP up */
        if (t > 500u)
            return -1;
        else
            us_wait_ms(1);
    sie_wr(S_POWER, 8);                                    /* bus reset */
    us_wait_ms(20);
    sie_wr(S_POWER, 0);
    us_wait_ms(20);
    us_err = "NOT A STICK";
    us_mps = 8;
    if (us_ctl(0, 0x80u, 6, 0x100u, 0, 8) < 8)
        return -1;
    us_mps = us_buf[7] >= 8u ? us_buf[7] : 8u;
    if (us_ctl(0, 0, 5, 1, 0, 0) < 0)                      /* SET_ADDRESS 1 */
        return -1;
    us_wait_ms(5);
    if (us_ctl(1, 0x80u, 6, 0x200u, 0, 64) < 18 || us_buf[14] != 8u || us_buf[16] != 0x50u)
        return us_fail("NOT A STICK");                     /* the first interface: mass storage, bulk-only */
    us_bin = us_bout = 0;
    for (e = us_buf + 18; e + 7 <= us_buf + 64 && e[0]; e += e[0])
        if (e[1] == 5u && (e[3] & 3u) == 2u)               /* a bulk endpoint */
            *(e[2] & 0x80u ? &us_bin : &us_bout) = e[2] & 15u;
    if (!us_bin || !us_bout)
        return -1;
    if (us_ctl(1, 0, 9, us_buf[5], 0, 0) < 0)              /* SET_CONFIGURATION */
        return -1;
    sie_wr(S_INDEX, US_EOUT), sie_wr(S_TXMAXP, 0xFF), sie_wr(S_TXCSR1, 0x48);   /* (the SDK's usb_h_ep_config:
                                                           * MAXP 1023, the DMA count sizes the packets), toggles to DATA0 */
    sie_wr(S_INDEX, US_EIN), sie_wr(S_RXMAXP, 0xFF), sie_wr(S_RXCSR1, 0x90);
    us_line(3, "STICK FOUND, ASKING IT", US_DIM);
    if (us_scsi(INQ, 6, us_buf, 36) < 0)
        return us_fail(us_err[0] == 'T' ? "STICK: NO ANSWER (TIMEOUT)" : "STICK: NO ANSWER");
    for (i = 0; (t = (uint32_t)us_scsi(TUR, 6, 0, 0)) != 0; i++) {   /* ready (some sticks take a few seconds) */
        if ((int)t < 0 || i > 30u)
            return us_fail("STICK NOT READY");
        us_scsi(SENSE, 6, us_buf, 18);
        us_wait_ms(100);
    }
    us_line(3, "READING THE STICK", US_DIM);
    return us_volume();
}

/* the FAT32 volume: the whole stick, or its first partition. 0 ok */
UI_SMALL static int us_volume(void)
{
    uint32_t part = 0;
    us_lba = ~0u;
    if (us_read(0))
        return -1;
    if (memcmp(us_sec + 82, "FAT32", 5)) {                 /* a partition table: the first partition */
        part = us_le(us_sec + 0x1C6, 4);
        if (us_read(part) || memcmp(us_sec + 82, "FAT32", 5))
            return us_fail("FORMAT IT AS FAT32");
    }
    if (us_le(us_sec + 11, 2) != 512u)
        return us_fail("FORMAT IT AS FAT32");
    us_spc = us_sec[13];
    us_fat = part + us_le(us_sec + 14, 2);
    us_data = us_fat + us_sec[16] * us_le(us_sec + 36, 4);
    us_root = us_le(us_sec + 44, 4);
    return 0;
}

static uint32_t us_wavx;                  /* the entry's 8.3 extension is WAV (a long name may be cut) */
/* ---- directories: each entry to fn (name: the long name if any, upper case; attributes; cluster; size) */
typedef void (*us_ent_fn)(const char *name, uint32_t attr, uint32_t clu, uint32_t size);
UI_SMALL static int us_dir(uint32_t clu, us_ent_fn fn)
{
    static const uint8_t LFN_AT[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    char lfn[US_NAME + 1], sn[13];
    uint32_t s, j, i, c, has = 0;
    const uint8_t *e;
    for (; clu >= 2u && clu < 0x0FFFFFF8u; clu = us_next(clu))
        for (s = 0; s < us_spc; s++) {
            if (us_read(us_lba_of(clu) + s))
                return -1;
            for (e = us_sec; e < us_sec + 512; e += 32) {
                if (!e[0])
                    return 0;
                if (e[0] == 0xE5u) {
                    has = 0;
                } else if (e[11] == 0x0Fu) {                /* a long-name piece: 13 characters */
                    if (e[0] & 0x40u)
                        memset(lfn, 0, sizeof lfn), has = 1;
                    for (j = 0; j < 13u; j++) {
                        i = ((e[0] & 0x1Fu) - 1u) * 13u + j;
                        c = e[LFN_AT[j]];
                        if (i < US_NAME && (c || e[LFN_AT[j] + 1]) && c != 0xFFu)
                            lfn[i] = (char)(e[LFN_AT[j] + 1] || c > 0x7Eu ? '?' : c >= 'a' && c <= 'z' ? c - 32 : c);
                    }
                } else if (!(e[11] & 0x08u)) {             /* (not the volume label) */
                    for (i = j = 0; j < 8u && e[j] != ' '; j++)
                        sn[i++] = (char)e[j];
                    if (e[8] != ' ')
                        for (sn[i++] = '.', j = 8; j < 11u && e[j] != ' '; j++)
                            sn[i++] = (char)e[j];
                    sn[i] = 0;
                    us_wavx = !memcmp(e + 8, "WAV", 3);
                    fn(has ? lfn : sn, e[11], us_le(e + 20, 2) << 16 | us_le(e + 26, 2), us_le(e + 28, 4));
                    has = 0;
                }
            }
        }
    return 0;
}

static char us_dname[US_DIRS][US_NAME + 1], us_fname[16][US_NAME + 1];
static uint32_t us_dclu[US_DIRS], us_nd, us_nw, us_fclu[16], us_fsize[16], us_nf, us_found;
#define US_FILES 32u                      /* (PHREAK 1.2) a folder's WAV files listed, to load one alone */
static char us_lname[US_FILES][US_NAME + 1];
static uint32_t us_lclu[US_FILES], us_lsize[US_FILES], us_nl, us_pick;   /* us_pick: the file picked + 1, 0 none */
#define US_DEPTH 8u
static char us_pname[US_DEPTH][US_NAME + 1];   /* the folders opened, SAMPLES/ (or the stick) first */
static uint32_t us_pclu[US_DEPTH], us_dep;

UI_SMALL static int us_lt(const char *a, const char *b)            /* a before b */
{
    while (*a && *a == *b)
        a++, b++;
    return (uint8_t)*a < (uint8_t)*b;
}

UI_SMALL static void us_find_samples(const char *n, uint32_t a, uint32_t clu, uint32_t size)
{
    (void)size;
    if ((a & 0x10u) && !memcmp(n, "SAMPLES", 8))
        us_found = clu;
}

/* a folder's folders (sorted by name), how many WAVs it has, and (PHREAK 1.2) the first US_FILES of them by name */
UI_SMALL static void us_add_dir(const char *n, uint32_t a, uint32_t clu, uint32_t size)
{
    uint32_t i;
    if (!(a & 0x12u) && us_wavx && n[0] != '.') {
        us_nw++;
        if (us_nl < US_FILES || us_lt(n, us_lname[US_FILES - 1u])) {
            for (i = us_nl < US_FILES ? us_nl++ : US_FILES - 1u; i && us_lt(n, us_lname[i - 1u]); i--) {
                memcpy(us_lname[i], us_lname[i - 1u], US_NAME + 1);
                us_lclu[i] = us_lclu[i - 1u], us_lsize[i] = us_lsize[i - 1u];
            }
            str_cpy(us_lname[i], n, US_NAME + 1);
            us_lclu[i] = clu, us_lsize[i] = size;
        }
    }
    if ((a & 0x12u) != 0x10u || n[0] == '.' || us_nd >= US_DIRS)
        return;
    for (i = us_nd++; i && us_lt(n, us_dname[i - 1u]); i--) {
        memcpy(us_dname[i], us_dname[i - 1u], US_NAME + 1);
        us_dclu[i] = us_dclu[i - 1u];
    }
    str_cpy(us_dname[i], n, US_NAME + 1);
    us_dclu[i] = clu;
}

UI_SMALL static void us_add_wav(const char *n, uint32_t a, uint32_t clu, uint32_t size)
{
    uint32_t i;
    if ((a & 0x12u) || !us_wavx || n[0] == '.' || us_nf >= 16u)
        return;                                            /* (macOS's ._ files, hidden ones) */
    for (i = us_nf++; i && us_lt(n, us_fname[i - 1u]); i--) {   /* sorted by name */
        memcpy(us_fname[i], us_fname[i - 1u], US_NAME + 1);
        us_fclu[i] = us_fclu[i - 1u], us_fsize[i] = us_fsize[i - 1u];
    }
    str_cpy(us_fname[i], n, US_NAME + 1);
    us_fclu[i] = clu, us_fsize[i] = size;
}

/* ---- a file, read in order */
static uint32_t us_fc, us_fci, us_fpos, us_fend, us_bad;   /* its cluster and that cluster's index, position, end */
UI_SMALL static int us_getb(void)
{
    uint32_t cs = us_spc * 512u;
    if (us_fpos >= us_fend)
        return 0;
    for (; us_fci < us_fpos / cs; us_fci++)
        us_fc = us_next(us_fc);
    if (us_fc < 2u || us_fc >= 0x0FFFFFF8u || us_read(us_lba_of(us_fc) + us_fpos % cs / 512u)) {
        us_bad = 1;                                        /* (us_err says why) */
        us_fend = 0;
        return 0;
    }
    return us_sec[us_fpos++ & 511u];
}

UI_SMALL static uint32_t us_getn(uint32_t n)                        /* n bytes, little-endian */
{
    uint32_t v = 0, i;
    for (i = 0; i < n; i++)
        v |= (uint32_t)us_getb() << (8u * i);
    return v;
}

static uint32_t w_ch, w_bytes, w_rate;
static int32_t w_prev;
/* file i: its WAV header read, the position at its audio, us_fend at the audio's end. 0 ok */
UI_SMALL static int us_wav(uint32_t i)
{
    uint32_t id, len, fmt = 0;
    us_fc = us_fclu[i], us_fci = us_fpos = 0, us_fend = us_fsize[i];
    if (us_getn(4) != 0x46464952u || (us_getn(4), us_getn(4)) != 0x45564157u)   /* RIFF .... WAVE */
        return us_fail("NOT A WAV");
    while (us_fpos + 8u <= us_fend) {
        id = us_getn(4);
        len = us_getn(4);
        if (id == 0x61746164u) {                           /* data */
            if (us_fpos + len < us_fend)
                us_fend = us_fpos + len;
            if ((fmt != 1u && fmt != 0xFFFEu) || !w_ch || w_ch > 2u || !w_bytes || w_bytes > 4u || w_rate < 4000u)
                return us_fail("WAV: PCM ONLY");
            w_prev = 0;
            return 0;
        }
        if (id == 0x20746D66u && len >= 16u) {             /* "fmt " */
            fmt = us_getn(2);
            w_ch = us_getn(2);
            w_rate = us_getn(4);
            us_getn(4), us_getn(2);
            w_bytes = us_getn(2) / 8u;
            len -= 16u;
        }
        us_fpos += len + (len & 1u);
    }
    return us_fail("NOT A WAV");
}

UI_SMALL static int32_t us_src(void)                                /* the next frame: mono, 16 bit; a 2-tap low-pass above 30 kHz */
{
    int32_t s = 0, r;
    uint32_t c, v;
    for (c = 0; c < w_ch; c++) {
        v = us_getn(w_bytes);
        s += w_bytes == 1u ? ((int32_t)v - 128) << 8 : (int16_t)(v >> (8u * (w_bytes - 2u)));
    }
    r = s / (int32_t)w_ch;
    s = w_rate > 30000u ? (r + w_prev) >> 1 : r;
    w_prev = r;
    return s;
}

/* ---- the slot: IMA ADPCM (sampleio.ima_encode: low nibble first, from 0 / 0) through a page into the flash */
static uint8_t us_pg[256] __attribute__((aligned(4)));
static uint32_t us_slot, us_dpos, us_crc, a_half;
static int32_t a_pred, a_idx;

UI_SMALL static int us_put(uint32_t b)
{
    uint32_t i;
    us_pg[us_dpos & 255u] = (uint8_t)b;
    for (us_crc ^= b & 255u, i = 0; i < 8u; i++)          /* zlib CRC-32 (st_crc32), a bit at a time */
        us_crc = us_crc >> 1 ^ (0xEDB88320u & (0u - (us_crc & 1u)));
    if (++us_dpos & 255u)
        return 0;
    return fl_write(us_slot + SMP_USER_DATA + us_dpos - 256u, us_pg, 256);
}

UI_SMALL static int us_enc(int32_t x)
{
    int32_t step = IMA_STEP[a_idx], d = x - a_pred, vd = step >> 3;
    uint32_t code = 0;
    if (d < 0)
        code = 8, d = -d;
    if (d >= step)
        code |= 4, d -= step, vd += step;
    if (d >= step >> 1)
        code |= 2, d -= step >> 1, vd += step >> 1;
    if (d >= step >> 2)
        code |= 1, vd += step >> 2;
    a_pred = clamp(a_pred + ((code & 8u) ? -vd : vd), -32768, 32767);
    a_idx = clamp(a_idx + IMA_IDX[code & 7u], 0, 88);
    if (!a_half) {
        a_half = 0x10u | code;
        return 0;
    }
    x = us_put((a_half & 15u) | code << 4);
    a_half = 0;
    return x;
}

/* file i into the slot at us_dpos, 22050 Hz (linear interpolation), lim samples at most (cut: the last 512 fade
 * out): its zone. 0 ok, 1 left out, -1 */
UI_SMALL static int us_convert(uint32_t i, smp_zone_t *z, uint32_t lim, uint32_t cut)
{
    uint32_t step, acc = 0, n = 0;
    int32_t s0, s1, x;
    if (us_wav(i))
        return -1;
    step = (uint32_t)(((uint64_t)w_rate << 16) / 22050u);
    a_pred = a_idx = 0, a_half = 0;
    z->off = us_dpos;
    s0 = us_src();
    s1 = us_src();
    while (us_fpos < us_fend && n < lim) {
        x = s0 + (int32_t)(((int64_t)(s1 - s0) * acc) >> 16);
        if (cut && lim - n < 512u)
            x = x * (int32_t)(lim - n) / 512;
        if (us_enc(x))
            return us_fail("FLASH ERROR");
        n++;
        for (acc += step; acc >= 65536u; acc -= 65536u)
            s0 = s1, s1 = us_src();
    }
    if (a_half && us_put(a_half & 15u))
        return us_fail("FLASH ERROR");
    a_half = 0;
    if (us_bad)
        return -1;
    if (n < 2u)
        return 1;                                          /* (too short: left out) */
    z->n = n, z->ls = 0, z->le = n - 1u, z->rate = 1u << 15;   /* 22050 / 44100, Q16 */
    z->pred = 0, z->idx = 0, z->looped = 0;
    return 0;
}

/* the samples of all files kept at most cap long */
UI_SMALL static uint32_t us_sum(const uint32_t *len, uint32_t cap)
{
    uint32_t i, s = 0;
    for (i = 0; i < us_nf; i++)
        s += len[i] < cap ? len[i] : cap;
    return s;
}

UI_SMALL static int us_note(const char *s)                          /* "C3", "F#2", "EB4" in a name: the MIDI note (C4 = 60), else -1 */
{
    static const uint8_t PC[7] = {9, 11, 0, 2, 4, 5, 7};
    int32_t i, j, a;
    for (i = 0; s[i]; i++) {
        if (s[i] < 'A' || s[i] > 'G' || (i && s[i - 1] >= 'A' && s[i - 1] <= 'Z'))
            continue;
        j = i + 1, a = 0;
        if (s[j] == '#')
            a = 1, j++;
        else if (s[j] == 'B' && s[j + 1] >= '0' && s[j + 1] <= '9')
            a = -1, j++;
        if (s[j] >= '0' && s[j] <= '8' && !(s[j + 1] >= '0' && s[j + 1] <= '9'))
            return clamp((s[j] - '0' + 1) * 12 + PC[s[i] - 'A'] + a, 0, 127);
    }
    return -1;
}

/* the files us_fname[0 .. us_nf) into slot k, named after name (up to a '.', 8 characters). 0 ok */
UI_SMALL static int us_load_files(const char *dname, uint32_t k)
{
    static smp_user_hdr_t h;
    int8_t root[16];
    char l[40], *p;
    uint32_t i, j, nz = 0, all = 1, len[16], lo = 0, hi = 0, m, room = (SMP_USER_SIZE - SMP_USER_DATA) * 2u - 32u;
    int8_t q;
    int r;
    smp_zone_t t;
    if (!us_nf)
        return us_fail("NO WAV FILES THERE");
    if (!flash_ok)
        return us_fail("NO FLASH");
    for (i = 0; i < us_nf; i++) {                          /* each file's length at 22050 Hz */
        len[i] = us_wav(i) ? 0 : (uint32_t)((uint64_t)((us_fend - us_fpos) / (w_ch * w_bytes)) * 22050u / w_rate);
        if (us_bad)
            return -1;
        hi = len[i] > hi ? len[i] : hi;
    }
    while (lo < hi)                                        /* the longest shortened until all fit (the editor's kitFit) */
        if (us_sum(len, m = (lo + hi + 1u) / 2u) <= room)
            lo = m;
        else
            hi = m - 1u;
    us_line(10, "ERASING THE SLOT", C_WHITE);
    if (ed_smp_erase(k, 1))
        return us_fail("FLASH ERROR");
    memset(&h, 0, sizeof h);
    us_slot = SMP_USER_OFF(k), us_dpos = 0, us_crc = ~0u;
    for (i = 0; i < us_nf; i++) {
        p = us_num(us_cat(l, "LOADING "), i + 1u);
        us_num(us_cat(p, " OF "), us_nf);
        us_line(10, l, C_WHITE);
        if (!len[i])
            continue;                                      /* not a WAV this can read: skipped */
        if ((r = us_convert(i, &h.zone[nz], len[i] < lo ? len[i] : lo, len[i] > lo)) < 0)
            return -1;                                     /* the stick gone, the flash */
        if (!r)
            root[nz++] = (int8_t)us_note(us_fname[i]);
    }
    if (us_dpos & 255u && fl_write(us_slot + SMP_USER_DATA + (us_dpos & ~255u), us_pg, us_dpos & 255u))
        return us_fail("FLASH ERROR");
    if (!nz)
        return us_fail("NO USABLE WAV");
    for (i = 0; i < nz; i++)
        all &= root[i] >= 0;
    for (i = 1; all && i < nz; i++)                        /* by note */
        for (j = i; j && root[j] < root[j - 1u]; j--) {
            t = h.zone[j], h.zone[j] = h.zone[j - 1u], h.zone[j - 1u] = t;
            q = root[j], root[j] = root[j - 1u], root[j - 1u] = q;
        }
    for (i = 0; i < nz; i++) {
        smp_zone_t *z = &h.zone[i];
        if (nz == 1u)
            z->root16 = (int16_t)((root[0] < 0 ? 60 : root[0]) * 16), z->lo = 0, z->hi = 127;
        else if (all)
            z->root16 = (int16_t)(root[i] * 16), z->lo = (uint8_t)(i ? (root[i - 1u] + root[i]) / 2 + 1 : 0),
            z->hi = (uint8_t)(i + 1u < nz ? (root[i] + root[i + 1u]) / 2 : 127);
        else
            z->root16 = (int16_t)(LANE_NOTE[i] * 16), z->lo = z->hi = LANE_NOTE[i];
    }
    h.magic = SMP_USER_MAGIC, h.version = 1, h.nz = (uint8_t)nz;
    for (i = 0; i < 8u && dname[i] && dname[i] != '.'; i++)
        h.name[i] = dname[i];
    h.data_len = us_dpos, h.crc = ~us_crc;
    if (fl_write(us_slot, (const uint8_t *)&h, sizeof h))
        return us_fail("FLASH ERROR");
    ed_smp_inval(k);
    us_err = us_sum(len, ~0u) > room ? (nz == 1u ? "LOADED, SHORTENED" : "LOADED, LONG ONES SHORTENED") : "LOADED";
    return 0;
}

/* the WAVs of a folder (its first cluster, its name) into slot k. 0 ok */
UI_SMALL static int us_load(uint32_t dclu, const char *dname, uint32_t k)
{
    us_nf = us_bad = 0;
    if (us_dir(dclu, us_add_wav))
        return -1;
    return us_load_files(dname, k);
}

/* (PHREAK 1.2) listed file f alone into slot k: over the whole keyboard. 0 ok */
UI_SMALL static int us_load_one(uint32_t f, uint32_t k)
{
    us_bad = 0;
    us_nf = 1;
    str_cpy(us_fname[0], us_lname[f], US_NAME + 1);
    us_fclu[0] = us_lclu[f], us_fsize[0] = us_lsize[f];
    return us_load_files(us_lname[f], k);
}

#ifndef US_TEST_READ
/* ---- (PHREAK 1.2) PLAY: hear listed file f, streamed from the stick. The I2S DMA runs as at boot (audio.c abuf, its
 * IRQ masked: nothing renders here), polled: each free half is filled from a FIFO of mono frames at 44.1 kHz in the
 * pool (nothing uses it on this screen), the FIFO from the file between halves (us_src, linearly resampled, as
 * us_convert). A first part is read before it sounds; a stick slower than the file starves it (a gap, not a stop).
 * Any button or knob ends it, as the file's end or US_PV_MAX does. 0 ok, -1 the stick */
extern uint32_t _pool_start[], _pool_end[];
#define US_PV_N 65536u                                  /* FIFO frames (128 KiB of the pool, 1.5 s) */
#define US_PV_PRE 22050u                                /* read before it sounds: 0.5 s */
#define US_PV_MAX (30u * 44100u)                        /* 30 s at most */
static uint32_t us_audio_on;
UI_SMALL static void us_pv_silence(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
}
/* (PHREAK 1.7) the MASTER knob for the preview: its gain, Q12 (0 .. ~4096), the curve of play (main.c
 * song.master_q12 = k10^2 / 256), the knob smoothed as main.c does; prev < 0: read it fresh */
static int32_t us_knob;
UI_SMALL static int32_t us_pv_gain(int32_t prev)
{
    int32_t a = fm1_adc_read(FM1_ADC_MASTER), k10;
    if (a < 0)
        return prev < 0 ? 2048 : prev;
    if (prev < 0)
        us_knob = a * 16;
    us_knob += (a * 16 - us_knob) / 4;
    k10 = us_knob / 16;
    return (k10 * k10) >> 8;
}

UI_SMALL static int us_preview(uint32_t f)
{
    int16_t *q = (int16_t *)(void *)_pool_start;
    uint32_t w = 0, r = 0, step, acc = 0, made = 0, scan = 0, i, done = 0, quiet = 0;
    int32_t s0, s1, g;
    if ((uint32_t)((uint8_t *)_pool_end - (uint8_t *)_pool_start) < US_PV_N * 2u)
        return us_fail("NO ROOM TO PLAY");
    if (!us_audio_on) {                                 /* the codec and the DMA, once: silence until a preview */
        us_pv_silence();
        fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
        fm1_irq_mask(FM1_IRQ_ALNK0);
        us_audio_on = 1;
    }
    us_bad = 0;
    us_fclu[0] = us_lclu[f], us_fsize[0] = us_lsize[f];
    if (us_wav(0))
        return us_bad ? -1 : 0;
    us_line(10, "PLAYING: ANY KEY STOPS", C_WHITE);
    us_line(9, "MASTER KNOB: VOLUME", US_DIM);
    fm1_adc_init();                                     /* (PHREAK 1.7) the MASTER knob: fm1_main has not set it up here */
    g = us_pv_gain(-1);
    step = (uint32_t)(((uint64_t)w_rate << 16) / 44100u);
    s0 = us_src();
    s1 = us_src();
    fm1_input_edges(0);
    for (i = 0; i < FM1_NENC; i++)
        fm1_enc_take(i);
    while (!done) {
        while (w - r < US_PV_N && us_fpos < us_fend && made < US_PV_MAX) {   /* (between halves: one at a time) */
            q[w++ & (US_PV_N - 1u)] = (int16_t)(s0 + (int32_t)(((int64_t)(s1 - s0) * acc) >> 16));
            made++;
            for (acc += step; acc >= 65536u; acc -= 65536u)
                s0 = s1, s1 = us_src();
            if ((fm1_audio_pending() & FM1_AUDIO_HALF) && made >= US_PV_PRE)
                break;
        }
        if (us_bad)
            break;
        if (made < US_PV_PRE && us_fpos < us_fend)
            continue;                                   /* (still reading the first part) */
        if (fm1_audio_pending() & FM1_AUDIO_HALF) {
            int32_t *o = &abuf[fm1_audio_free_half() * HALF_WORDS], g1 = us_pv_gain(g);
            uint32_t k;
            for (k = 0; k < HALF_FRAMES; k++) {          /* MASTER as in play (song.master_q12), glided over the half */
                int32_t gk = g + (g1 - g) * (int32_t)k / (int32_t)HALF_FRAMES;
                int32_t v = r != w ? (q[r++ & (US_PV_N - 1u)] * gk) >> 5 : 0;   /* Q15 at -6 dB with MASTER full, 24-bit */
                o[2u * k] = o[2u * k + 1u] = v;
            }
            g = g1;
            fm1_audio_ack_half();
            if (r == w && (us_fpos >= us_fend || made >= US_PV_MAX) && ++quiet > 2u)
                done = 1;                               /* the end, and the halves after it silent */
        }
        fm1_wdt_feed();
        if (++scan >= 64u) {                            /* the panel, every so often (a scan is ~0.6 ms) */
            scan = 0;
            fm1_input_scan();
            for (i = 0; i < FM1_NENC; i++)
                if (fm1_enc_take(i))
                    done = 1;
            if (fm1_input_edges(0))
                done = 1;
        }
    }
    us_pv_silence();
    fm1_input_edges(0);
    us_err = us_bad ? us_err : "";
    return us_bad ? -1 : 0;
}
#endif

/* ---- the screen: KNOB picks, OCT+ opens a folder or loads, OCT- goes back up. A folder with WAVs in it lists
 * LOAD ... USR1..USR6 first, then its folders, then (PHREAK 1.2) its WAV files: OCT+ on one lists LOAD > USR1..6
 * for it alone */
UI_SMALL static int us_open(void)                          /* list the folder us_dep (0 ok) */
{
    us_nd = us_nw = us_nl = us_pick = 0;
    return us_dir(us_pclu[us_dep], us_add_dir);
}

UI_SMALL static void usbstick_main(void)
{
    uint32_t sel = 0, top = 0, i, e, nl, pr, draw;
    int32_t st;
    char l[40], *p;
    bootguard.pending = 2;                   /* WDT/exception here falls back to ROM, as in USB RESCUE */
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    lcd_init();
    for (;;) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        us_line(0, "LOAD FROM USB", C_WHITE);
        us_line(8, "A FAT32 STICK, YOUR WAVS", US_DIM);
        us_line(9, "IN SAMPLES/ (OR ANYWHERE)", US_DIM);
        for (;;) {                                         /* a stick; SAMPLES/ if it has one, else its top */
            const char *m = 0;
            us_dep = 0;
            if (us_mount() || (us_found = 0, us_dir(us_root, us_find_samples)))
                m = us_err;
            else {
                us_pclu[0] = us_found ? us_found : us_root;
                str_cpy(us_pname[0], us_found ? "SAMPLES" : "USB STICK", US_NAME + 1);
                if (us_open())
                    m = us_err;
            }
            if (!m)
                break;
            us_line(4, m, C_WHITE);
            us_wait_ms(1000);
        }
        us_err = "", sel = top = 0;
        for (draw = 1;;) {
            uint32_t nd = us_pick ? 0u : us_nd, nf = us_pick ? 0u : us_nl, all;
            nl = us_nw ? SMP_USER_SLOTS : 0u;              /* the LOAD lines (USR1..USR6) */
            all = nl + nd + nf;
            if (draw) {
                draw = 0;
                lcd_fill(0, 0, 240, 240, C_BLACK);
                us_line(0, us_pick ? us_lname[us_pick - 1u] : us_pname[us_dep], C_WHITE);
                if (!all)
                    us_line(3, "NO WAVS OR FOLDERS HERE", US_DIM);
                for (i = 0; i < 7u && top + i < all; i++) {
                    e = top + i;
                    if (us_pick) {                         /* one file: its slot */
                        us_cat(l, "LOAD THIS > USR1");
                        l[15] = (char)('1' + e);
                    } else if (e < nl) {
                        p = us_num(us_cat(l, "LOAD "), us_nw < 16u ? us_nw : 16u);
                        if (us_nw > 16u)
                            p = us_num(us_cat(p, " OF "), us_nw);
                        p = us_cat(p, " WAVS > USR1");
                        p[-1] = (char)('1' + e);
                    } else if (e < nl + nd) {
                        us_cat(us_cat(l, us_dname[e - nl]), "/");
                    } else {
                        us_cat(us_cat(l, "  "), us_lname[e - nl - nd]);   /* (a file: indented) */
                    }
                    us_line(1 + i, l, e == sel ? C_WHITE : US_DIM);
                }
                us_line(8, us_pick ? "OCT+ LOAD IT, PLAY HEAR IT" : "KNOB PICK, OCT+ OPEN / LOAD", US_DIM);
                us_line(9, !us_pick && sel >= nl + nd && sel < all ? (us_dep ? "PLAY HEAR IT, OCT- BACK" : "PLAY HEAR IT")
                                                                   : us_dep || us_pick ? "OCT- BACK" : "", US_DIM);
                us_line(10, us_err, C_WHITE);
            }
            fm1_wdt_feed();
            fm1_input_scan();
            fm1_delay_us(500u);
            for (st = 0, i = 0; i < FM1_NENC; i++)
                st += fm1_enc_take(i);
            pr = fm1_input_edges(0);
            if (st && all) {
                sel = (uint32_t)clamp((int32_t)sel + st, 0, (int32_t)all - 1);
                top = sel < top ? sel : sel >= top + 7u ? sel - 6u : top;
                us_err = "", draw = 1;
            }
#ifndef US_TEST_READ
            if ((pr & (1u << 12)) && (us_pick || (sel >= nl + nd && sel < all))) {   /* PLAY: hear the file */
                if (us_preview(us_pick ? us_pick - 1u : sel - nl - nd) && us_bad)
                    break;
                draw = 1;
                continue;
            }
#endif
            if (!(pr & 3u))
                continue;
            draw = 1, us_err = "";
            if ((pr & 1u) && us_pick) {                    /* OCT-: back to the folder, on its file */
                sel = nl + us_nd + us_pick - 1u, us_pick = 0;
                top = sel > 6u ? sel - 6u : 0u;
                continue;
            } else if ((pr & 2u) && us_pick) {             /* OCT+: the file alone into USR1..6 */
                if (us_load_one(us_pick - 1u, sel) && us_bad)
                    break;
                e = us_pick;
                us_pick = 0;
                sel = nl + us_nd + e - 1u;
                top = sel > 6u ? sel - 6u : 0u;
                continue;
            } else if ((pr & 2u) && sel >= nl + us_nd && sel < all) {   /* OCT+ on a file: its slots */
                us_pick = sel - nl - us_nd + 1u, sel = top = 0;
                continue;
            } else if ((pr & 1u) && us_dep)                /* OCT-: up */
                us_dep--, sel = top = 0;
            else if ((pr & 2u) && sel < nl) {              /* OCT+ on LOAD: the WAVs here into USR1..6 */
                if (us_load(us_pclu[us_dep], us_pname[us_dep], sel) && us_bad)
                    break;                                 /* (the stick pulled out: wait for one again) */
                us_open();                                 /* (us_load used the lists) */
                continue;
            } else if ((pr & 2u) && sel < nl + nd && us_dep + 1u < US_DEPTH) {   /* OCT+ on a folder: in */
                us_pclu[us_dep + 1u] = us_dclu[sel - nl];
                str_cpy(us_pname[us_dep + 1u], us_dname[sel - nl], US_NAME + 1);
                us_dep++, sel = top = 0;
            } else {
                continue;
            }
            if (us_open())
                break;
        }
        us_line(10, us_err, C_WHITE);
        us_wait_ms(2000);
    }
}
