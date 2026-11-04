# PHREAK

**PHREAK, by [dag](https://dagnazty.dev/): [SLOOP](https://github.com/isod89/sloop-fm1) 2.8.4 for the M-VAVE FM-1, with three drum tracks, samples from a USB stick that you can shape like a synth, eight song sections, and more trap sounds.**

Everything in SLOOP works as before: the four tracks, song mode, punch-in FX, MIDI, USB audio, the web editor. The boot screen and ABOUT say PHREAK and its number (1.2) and *by dag*; the version string the web editor reads stays `SLOOP 2.8.4+`.

## What it adds to SLOOP

| | |
|---|---|
| **LOAD FROM USB** (hold **OCT+** while switching on, [usbstick.c](firmware/src/usbstick.c)) | Samples from a USB stick into the user sample slots USR1–USR4. Browse the folders of a FAT32 stick, starting in `SAMPLES/` (or the stick's top level when it has none), at any depth; any folder with WAVs in it can go into a slot: its WAV files (8/16/24/32-bit PCM, mono or stereo, any rate) are mixed to mono, converted to 22050 Hz IMA ADPCM and written to the slot, up to 16 files and about 7 s. When they're too long, the longest are shortened (with a fade) until all fit. One file plays over the whole keyboard; when every file name has a note (`PAD C3.wav`) they split the keyboard between their notes; otherwise they go to the drum lanes in order (KICK, KICK 2, SNARE, CLAP, HAT…), so the folder plays as a USR kit. KNOB picks a line, OCT+ opens a folder or loads (a folder with WAVs lists *LOAD … > USR1* to *USR4* first), OCT− goes back up. **(1.2) One file alone:** the folder's WAV files are listed too (indented, the first 32 by name); OCT+ on one, then *LOAD THIS > USR1* to *USR4*, loads it alone across the whole keyboard (its note from its name, else C4): a sample for a synth or bass track (a SAMPLE sound from PRESETS, then EDIT → SET, KNOB 1: USR1–4). Switch off to leave. |
| **Sections A–H** | The song has eight sections instead of four. **SAVE + keys 1–4** play A–D and **SAVE + keys 9–12** play E–H (chains mix them freely); **SAVE + keys 5–8** store into A–D, or into E–H after **SAVE + key 15** switches them over (the tiles say which). SAVE → SECTION lists all eight in two columns, the SONG screen picks any of them, and the web editor's sections, Song, LIVE, backup and song-file pages carry E–H (INFO ends with the section count, 8; backup objects 12–15; LIVE op 13). E–H are saved in five flash sectors the four of them share: 0x93000–0x96FFF, above the app area an update writes, and 0xFB000; a save always goes to a sector that holds no section's current copy, so a power cut while saving keeps the previous one, as A–D's two-sector pairs do. |
| **Drum tracks D2, D3** ([xdrums.c](firmware/src/xdrums.c)) | Two more drum tracks next to the drum track (D1). **ALGORITHM** turns through tracks 1, 2, 3, then **D1, D2, D3** (*DRUM TRACK D2*; the TRACKS strip and the DRUMS screen say *d2* / *d3*). The one you pick is the drum track for everything: the DRUMS screen, the keys, REC, step entry, the kit (PRESETS), its LENGTH, DIV and swing, MUTE, PAN and LEVEL (D1's LEVEL is GLO > DRUMS LEVEL as ever; D2 and D3 have their own). All three play together, on the drum track's 6 voices (the oldest hit gives way, the kick last), through its INSERT, filter and sends. At power-on (and after NEW) D2 is on the **USR1** kit and D3 on **USR2**, empty: load a folder of drums into USR1 / USR2 (LOAD FROM USB) and program them. They're kept with the working project and with every section A–H (two sections to a flash object, in the last 12 KiB of each user sample slot), and they follow song mode. While another one is shown, a drum track plays its hits, levels, ratchets, nudges and fill conditions; it sends no MIDI, and only D1 has parameter locks. The web editor sees the drum track shown, and its backups hold D1 only. |
| **USR kit sound** (PHREAK 1.1) | When a drum track's kit is **USR1–USR4** or **USR3+4**, the **ENV**, **LFO** and **EDIT** pages open on it (with any other kit they still say *DRUM TRACK*), and shape every hit as the SAMPLE engine shapes a note. **ENV**: ATK DEC SUS REL; each hit holds for the step's **GATE** (SEQ → PATTERN, a share of a step at the track's DIV), then its release. **ENV DEST**: FLT (the envelope opens the filter), PIT (a pitch drop at the start). **LFO / LFO 2 / LFO DEST**: RATE WAVE PHASE FADE, SYNC TRIG POL, to PIT (vibrato), FLT (wobble), AMP (tremolo). **EDIT 1 (SAMPLE)**: KIT, **TUNE** ±24 semitones, **BITS**, **LOOP** (the sample again from its start while the hit holds: a short sample as a pad). **EDIT 2 (TONE)**: **CUT**, **RES**, **DRV**. **FILTER**: TYPE LP / BP / HP, CUT, RES (ANALOG's resonant filter). D1, D2 and D3 each have their own, kept with the project and with each section. At their defaults (no attack, SUS and REL full, CUT open, nothing else) a hit plays exactly as before, and costs nothing more; projects from before open that way. The values sit in drum-track slots a drum track never used (the VOICE page's: [drums.c](firmware/src/drums.c) `DS_*`), so the project format is unchanged. |
| **Trap** | Four more 808 basses on ANALOG in the BASS group (808 LONG, 808 PUNCH, 808 GRIT, 808 GLIDE), and three drum kits after JAZZ: **RAGE**, **PLUGG** and **MEMPHIS**. |

## Build and install

The same as SLOOP (see [BUILDING.md](BUILDING.md)):

```
sh build.sh --release 1.2          # build/phreak-1.2.fwsc, identity FM-1_912
sh tests/run_tests.sh              # SLOOP's host and web tests, with these additions
python3 tools/fm1_install.py build/phreak-1.2.fwsc
```

The JieLi clang sometimes segfaults under Docker on Apple silicon. Run the build again and it goes through.

## Notes

- **Tests:** all of SLOOP's host and web tests pass. The exception is *target cost of the render loops*, which fails identically on plain SLOOP 2.8.4 because its budget file is out of date upstream.
- **Tests:** [usrsound_test.c](tests/usrsound_test.c) shapes USR kit hits with each control, checks the defaults change nothing, and saves and loads them with projects and sections (and PHREAK 1.0's sections).
- **Tests:** [xdrums_test.c](tests/xdrums_test.c) plays D2 and D3 while D1 is shown, mutes one, swaps them while playing, and saves and loads them with a section.
- **Kit positions:** RAGE, PLUGG and MEMPHIS come after JAZZ, so SLOOP's factory kits keep their numbers, but the user kits (USR1–4, SYN1–4) each move up three compared to SLOOP.
- **LOAD FROM USB, the hardware:** the FM-1 can't power a stick, and a stick (or a hub) only shows up when it sees 5 V from the host. Use a USB-C OTG adapter with a charging port (USB-C plug, USB-A socket, USB-C power in) and a phone charger in it: the charger powers the stick and charges the FM-1. A plain OTG adapter, or a hub whose power only feeds its own ports, shows *PLUG IN A USB STICK*. Sticks over 32 GB usually come as exFAT: format them as FAT32. The FM-1's USB to the computer is off on this screen. The USB host mode follows JieLi's AC79 SDK (its `usb_h_sie_init`, `usb_h_chirp_and_reset` and transfer code, USB0 branches; the register helpers are in [fm1_usb.h](firmware/hal/fm1_usb.h)). Tests: [usbstick_test.c](tests/usbstick_test.c) runs the FAT32, WAV and slot code on a disk image, [usbstick_check.py](tests/usbstick_check.py) decodes the slots it wrote.
- **Sample slots, smaller:** each user slot holds 68 KiB now (about 6.3 s; 80 KiB, about 7.4 s, up to SLOOP 2.8.4): the last 12 KiB of each keep D2 and D3. A slot loaded before with more than 68 KiB of samples reads as empty: load it again (LOAD FROM USB, the web editor). USR3+4 holds about 12.5 s.
- **FLUTE, smaller:** to make room for LOAD FROM USB, the built-in FLUTE sample set keeps one note (C5) and 0.55 s of it (30 KB down to 6 KB), as SLOOP 2.7.2 did with SCRCH. The set, its number and its presets (LOFI FLUTE, GRAIN's FLUTE DUST) stay; they sound a little plainer.
- **After a crash:** A–D live in RAM that survives a reset; E–H and every section's D2 and D3 don't (that RAM is full). What you store while playing is lost only if the FM-1 crashes in the seconds before it's written to flash after you stop.

## Credits

GPL-3.0, like SLOOP.

- **SLOOP:** isod89 (GPL-3.0)
- **Felucca:** Leo Kuroshita / Hügelton Instruments
- **Everyone SLOOP credits:** see [README.md](README.md)
