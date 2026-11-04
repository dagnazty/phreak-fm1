#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""checks the slots tests/usbstick_test.c wrote from a disk image against the WAVs that went on it:
the header (magic, zones well formed as eng_sample.c smp_zone_ok wants them, CRC), the key mapping, and each
zone decoded (IMA ADPCM, as eng_sample.c sample_next) against sampleio's own conversion of the file.
  usbstick_check.py SRC_SAMPLES_DIR OUTDIR"""
import math
import re
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import sampleio as sio  # noqa: E402

STEP, IDX = sio.IMA_STEP, sio.IMA_IDX
LANE_NOTE = [36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56]
fails = 0


def check(ok, what):
    global fails
    print(("ok    " if ok else "FAIL  ") + what)
    fails += not ok


def decode(data, off, n):
    pred, idx, out = 0, 0, []
    for i in range(n):
        b = data[off + (i >> 1)]
        code = b >> 4 if i & 1 else b & 15
        step = STEP[idx]
        vd = step >> 3
        if code & 4:
            vd += step
        if code & 2:
            vd += step >> 1
        if code & 1:
            vd += step >> 2
        pred = max(-32768, min(32767, pred - vd if code & 8 else pred + vd))
        idx = max(0, min(88, idx + IDX[code & 7]))
        out.append(pred)
    return out


def model(path, n, cut):
    """usbstick.c's own conversion, integer for integer: frames to mono int16 (us_src: the top 16 bits, the mean
    of the channels, a 2-tap low-pass above 30 kHz), 22050 Hz by linear interpolation in Q16, the cut fade"""
    d = path.read_bytes()
    ch = sio.wav_chunks(d)
    _, nch, sr, bits = sio.wav_format(d, ch)
    o, size = ch[b"data"]
    bps = bits // 8
    raw = d[o:o + size]
    frames = len(raw) // (bps * nch)

    def frame(i):
        s = 0
        for c in range(nch):
            b = raw[(i * nch + c) * bps:(i * nch + c + 1) * bps]
            v = int.from_bytes(b, "little")
            s += (v - 128) << 8 if bps == 1 else struct.unpack("<h", struct.pack("<H", v >> (8 * (bps - 2)) & 0xFFFF))[0]
        return int(s / nch)

    src, prev = [], 0
    for i in range(frames + 2):
        r = frame(i) if i < frames else -1
        src.append((r + prev) >> 1 if sr > 30000 else r)
        prev = r
    step, acc, k, out = (sr << 16) // 22050, 0, 1, []
    s0, s1 = src[0], src[1]
    while k < frames and len(out) < n:
        x = s0 + (((s1 - s0) * acc) >> 16)
        if cut and n - len(out) < 512:
            x = int(x * (n - len(out)) / 512)
        out.append(x)
        acc += step
        while acc >= 65536:
            acc -= 65536
            k += 1
            s0, s1 = s1, src[k]
    return out


def note(name):                                       # usbstick.c us_note, on the upper-case name
    for i, c in enumerate(name):
        if c < "A" or c > "G" or (i and "A" <= name[i - 1] <= "Z"):
            continue
        j, a = i + 1, 0
        if name[j:j + 1] == "#":
            a, j = 1, j + 1
        elif name[j:j + 1] == "B" and name[j + 1:j + 2].isdigit():
            a, j = -1, j + 1
        if name[j:j + 1] and "0" <= name[j] <= "8" and not name[j + 1:j + 2].isdigit():
            return max(0, min(127, (int(name[j]) + 1) * 12 + [9, 11, 0, 2, 4, 5, 7][ord(c) - 65] + a))
    return -1


def main(src, out):
    for slot in sorted(Path(out).glob("slot_*.bin")):
        folder = slot.stem[5:]
        d = slot.read_bytes()
        magic, ver, nz, _, name, dlen, crc = struct.unpack_from("<IHBB8sII", d)
        data = d[512:512 + dlen]
        print(f"-- {folder}: {nz} zones, {dlen} B, name {name.rstrip(bytes(1)).decode()}")
        check(magic == 0x504D5346 and ver == 1 and 1 <= nz <= 16, "header")
        check(dlen <= 68 * 1024 - 512 and zlib.crc32(data) == crc, "data length and CRC-32")
        srcdir = next(p for p in [Path(src), *Path(src).rglob("*")] if p.is_dir() and
                      (p.name.upper() == folder or (folder in ("SAMPLES", "USB STICK") and p == Path(src))))
        wavs = sorted((p for p in srcdir.iterdir() if p.suffix.lower() == ".wav" and not p.name.startswith(".")),
                      key=lambda p: p.name.upper().encode())
        roots = [note(p.name.upper()) for p in wavs]
        allnamed = all(r >= 0 for r in roots)
        if allnamed:
            order = sorted(range(len(wavs)), key=lambda i: roots[i])
        else:
            order = list(range(len(wavs)))
        check(nz == len(wavs), f"one zone a file ({len(wavs)})")
        for j in range(nz):
            off, n, ls, le, rate, root16, pr, ix, lo, hi, looped = struct.unpack_from("<5I2h4B", d, 32 + 28 * j)
            ok = (off <= dlen and off + (n + 1) // 2 <= dlen and ix <= 88 and 0 < rate <= 4 << 16 and ls <= le < n
                  and lo <= hi)
            f = wavs[order[j]]
            if nz == 1:
                want = ((roots[0] if roots[0] >= 0 else 60), 0, 127)
            elif allnamed:
                r = [roots[i] for i in order]
                want = (r[j], 0 if j == 0 else (r[j - 1] + r[j]) // 2 + 1, 127 if j == nz - 1 else (r[j] + r[j + 1]) // 2)
            else:
                want = (LANE_NOTE[j], LANE_NOTE[j], LANE_NOTE[j])
            sr, x = sio.read_any_wav(f)
            ref = sio.resample(x, sr, 22050)
            y = decode(data, off, n)
            m = min(n, len(ref)) - 600                    # (the cut ones fade out over their last 512)
            a, b = [v * 32768 for v in ref[:m]], y[:m]
            best = max(range(-2, 3), key=lambda s: corr(a, b, s))
            c = corr(a, b, best)
            g = math.sqrt(sum(v * v for v in b) / max(1e-9, sum(v * v for v in a)))
            short = n < len(ref) - 2
            check(ok and (root16 // 16, lo, hi) == want and rate == 32768 and not looped,
                  f"zone {j} {f.name}: root {root16 // 16} keys {lo}..{hi} (want {want}), {n} samples"
                  f"{' (shortened from %d)' % len(ref) if short else ''}")
            mo = model(f, n, short)
            cm = corr(mo, y, 0)
            gm = math.sqrt(sum(v * v for v in y) / max(1e-9, sum(v * v for v in mo)))
            check(len(mo) == n and cm > 0.97 and 0.9 < gm < 1.1 and c > 0.6,
                  f"zone {j} decoded: vs the exact model {cm:.4f} (gain {gm:.3f}); vs sampleio {c:.4f} (gain {g:.3f})")
    print("usbstick: " + ("FAILED" if fails else "all ok"))
    return 1 if fails else 0


def corr(a, b, s):
    if s >= 0:
        a, b = a[s:], b[:len(b) - s]
    else:
        a, b = a[:len(a) + s], b[-s:]
    n = min(len(a), len(b))
    sab = sum(a[i] * b[i] for i in range(n))
    return sab / math.sqrt(max(1e-9, sum(v * v for v in a[:n]) * sum(v * v for v in b[:n])))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
