<p align="center"><img src="assets/logo/phreak-logo.png" alt="PHREAK" width="460"></p>

<p align="center"><b>A six-track groovebox firmware for the M-VAVE FM-1, built for your own samples.</b><br>
By <a href="https://dagnazty.dev/">dag</a>.<br>
Free and open source (GPL-3.0), based on <a href="https://github.com/isod89/sloop-fm1">SLOOP</a> 2.8.4 by isod89 and <a href="https://github.com/hugelton/Felucca">Felucca</a> by Leo Kuroshita / Hügelton Instruments.</p>

<p align="center">
<a href="#install">Install</a> ·
<a href="#quick-start">Quick start</a> ·
<a href="EXTRAS.md">What PHREAK adds, in detail</a> ·
<a href="GUIDE.md">Complete guide (SLOOP)</a> ·
<a href="BUILDING.md">Building</a>
</p>

---

PHREAK turns the FM-1 into a **six-track groovebox**: three synth tracks and **three drum tracks** (D1, D2, D3) playing together. Drums come from a USB stick: plug in a stick of WAVs, pick a folder on the FM-1's screen and it becomes a kit. Then shape those samples like a synth, with an **envelope, a resonant filter, an LFO, tune, bit crush, drive and loop**. Songs have **eight sections, A to H**. Everything else in SLOOP is still there: twelve synth engines and 153 sounds, 37 drum kits, parameter locks, punch-in effects, song mode, the web editor, MIDI and USB audio.

> **Status:** PHREAK **1.2**, a personal fork, tested on one FM-1. Install it at your own risk. Your projects, presets, samples and settings are kept when you update, and you can go back to SLOOP or the official firmware at any time (see [Going back](#going-back)).

<p align="center"><img src="assets/screens/phreak-boot-about.png" alt="The PHREAK boot screen and ABOUT page" width="640"></p>

<p align="center"><sub>The boot screen and the ABOUT page, as the firmware draws them (rendered by the host tests: a release build shows its number where these say DEV).</sub></p>

## Contents

1. [What PHREAK adds](#what-phreak-adds)
2. [Install](#install)
3. [Quick start](#quick-start)
4. [Samples from a USB stick](#samples-from-a-usb-stick)
5. [Three drum tracks](#three-drum-tracks)
6. [Shaping your samples](#shaping-your-samples)
7. [Eight song sections](#eight-song-sections)
8. [The controls](#the-controls)
9. [Everything from SLOOP](#everything-from-sloop)
10. [Limits and differences from SLOOP](#limits-and-differences-from-sloop)
11. [Versions](#versions)
12. [Building and tests](#building-and-tests)
13. [Credits](#credits)
14. [Licence](#licence)

## What PHREAK adds

| | |
| --- | --- |
| **LOAD FROM USB** | Hold **OCT+** while switching on. Browse a FAT32 stick, starting in `SAMPLES/`, through as many folders as you like, and load a folder of WAVs, or (1.2) one WAV alone, into sample slot USR1, USR2, USR3 or USR4. Any WAV works (8 to 32-bit, mono or stereo, any rate). [More](#samples-from-a-usb-stick) |
| **Three drum tracks** | **D1, D2, D3** play together, each with its own kit, pattern, length, DIV, swing, mute, pan and level. ALGORITHM turns through 1, 2, 3, D1, D2, D3. D2 starts on your USR1 kit, D3 on USR2. [More](#three-drum-tracks) |
| **USR kit sound** (1.1) | On a drum track playing a USR kit, the **ENV**, **LFO** and **EDIT** pages open: ADSR with a gate, a resonant LP / BP / HP filter, LFO to pitch, filter and level, TUNE, BITS, DRIVE and LOOP. Each drum track has its own settings. [More](#shaping-your-samples) |
| **Sections A–H** | Eight song sections instead of four, on the FM-1 and in the web editor. [More](#eight-song-sections) |
| **More trap** | Four more 808 basses (808 LONG, 808 PUNCH, 808 GRIT, 808 GLIDE) and three drum kits: **RAGE**, **PLUGG** and **MEMPHIS**. |
| **Its own look** | A new boot screen and ABOUT page: a lightning bolt in the four track colours, through the knob dial. |

The full technical description is in [EXTRAS.md](EXTRAS.md).

## Install

PHREAK is installed over USB from a computer, as SLOOP is. Your projects, user presets, samples and settings are kept.

**From the command line** (Python 3, with `pip3 install mido python-rtmidi`). Connect the FM-1 with a USB **data** cable, directly (no hub), close every other app that uses MIDI, then run:

```bash
python3 tools/fm1_install.py build/phreak-1.2.fwsc
```

It checks the package, puts the FM-1 in update mode, writes it and restarts it. `python3 tools/fm1_install.py --info` shows what the connected FM-1 runs; PHREAK X.Y reports itself as `FM-1_9XY` (1.2 is `FM-1_912`).

**From the browser.** The installer and the web editor in this repository can be served from your own computer, with your build inside (Web MIDI needs `localhost`):

```bash
python3 web/make_site.py build/phreak-1.2.fwsc 1.2 build/site
```

```bash
python3 -m http.server 8766 --bind 127.0.0.1 --directory build/site
```

Then open `http://localhost:8766/webapp/installer/` in **Chrome or Edge** and press **INSTALL**. The editor is at `http://localhost:8766/webapp/editor/`. Use this local editor, not SLOOP's online one: only this one knows sections E–H.

After an install, unplug and plug the FM-1 back in once, so the computer finds its USB audio input again.

### Going back

- **To SLOOP 2.8.4:** install its `.fwsc` the same way. SLOOP ignores sections E–H, D2 and D3 and the USR kit sound, and it sees the sample slots as 80 KB where PHREAK uses 68 KB of each. **Save a backup in the editor first.**
- **To the official firmware:** save a backup, then use M-VAVE's own updater **M-UPGRADE** with the official FM-1 firmware (V16), as described in SLOOP's [GUIDE.md](GUIDE.md).

### Rescue

- **The FM-1 no longer starts PHREAK:** hold **OCT−** alone while switching it on (the screen says *SLOOP USB RESCUE*), then install again.
- **An install was cut off:** the FM-1 stays in update mode. Run the install again and it finishes.
- If an FM-1 no longer starts at all, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

> Custom firmware is installed at your own risk. No warranty.

## Quick start

1. **Put samples on a stick.** Format a USB stick as FAT32 and make a folder `SAMPLES`, with a folder per kit inside (`SAMPLES/MY KIT/KICK.wav`, `SNARE.wav`, `HAT.wav`…).
2. **Load them.** Plug the stick into a powered USB-C OTG adapter (see [the hardware](#the-hardware)), hold **OCT+** and switch the FM-1 on. Turn **KNOB** to your kit's folder, press **OCT+** to open it, pick **LOAD … > USR1** and press **OCT+**. Switch off and on again.
3. **Play them.** Turn **ALGORITHM** to **D2**: the screen says *DRUM TRACK D2*, and D2 plays the USR1 kit. The white keys are your samples, in file-name order.
4. **Make a beat.** Press **REC** and play, or tap **SEQ** for the drum grid. Turn ALGORITHM back to **D1** and record a second drum part: D2 keeps playing.
5. **Shape it.** On D2, tap **EDIT** until **TONE** shows: turn **CUT** down and **RES** up. Tap **ENV**: lower **SUS** and **REL** to make the hits shorter.
6. **Keep it.** Hold **SAVE** and press keys 5–8 to store the loop into sections A–D (key 15 first for E–H). Hold SAVE and press keys 1–4 or 9–12 to play them.

## Samples from a USB stick

Hold **OCT+** while switching on: **LOAD FROM USB** opens. The FM-1's USB link to the computer is off on this screen; switch off to leave.

- **Browsing:** it starts in `SAMPLES/`, or at the top of the stick if there is no such folder, and goes into folders at any depth. **KNOB** picks a line, **OCT+** opens a folder or loads, **OCT−** goes back up.
- **One file alone** (1.2): a folder also lists its WAV files (indented, after its folders). Press **OCT+** on one, then pick **LOAD THIS > USR1** to **USR4**: that file fills the slot and plays across the whole keyboard, from its note (`BASS C2.wav`) or C4. That's how you get a sample onto a synth or bass track: on track 1, 2 or 3, pick a SAMPLE sound with **PRESETS** (UP BASS, GRAND PNO…), then tap **EDIT** for its **SET** page and turn **KNOB 1 (SET)** to that slot, USR1–USR4. ENV, LFO, filter (CUT), TUNE and the rest then shape it as any synth sound. OCT− goes back to the folder.
- **Loading a folder:** a folder with WAV files in it lists **LOAD n WAVS > USR1** to **USR4** first. Up to 16 files go into the slot, mixed to mono and converted for the FM-1 (22,050 Hz ADPCM). A slot holds about **6.3 s**; when the files are longer, the longest are shortened, with a short fade, until everything fits.
- **Where the sounds land:**
  - **One file:** it plays across the whole keyboard (its root note from its name, else C4).
  - **Every name has a note** (`PAD C3.wav`, `PAD G3.wav`): the files split the keyboard between their notes, for a melodic instrument on a synth track.
  - **Otherwise:** the files go to the drum lanes in name order (KICK, KICK 2, SNARE, CLAP, HAT…), so the folder plays as a kit on a drum track.
- **WAV files:** 8, 16, 24 or 32-bit PCM, mono or stereo, any sample rate. Long file names are fine.

### The hardware

The FM-1 cannot power a USB stick, and a stick (or a hub) only shows up when it gets 5 V. Use a **USB-C OTG adapter with a charging port** (USB-C plug, USB-A socket, USB-C power in) with a phone charger plugged in: the charger powers the stick and charges the FM-1. A plain OTG adapter, or a hub that only powers its own ports, shows *PLUG IN A USB STICK*. Sticks over 32 GB usually come formatted as exFAT: reformat them as FAT32.

The loaded samples live in the FM-1's flash, in slots USR1–USR4: every synth track (SAMPLE engine, SET USR1–4) and every drum track (KIT USR1–4, USR3+4) can play them, and they stay after power-off until you load something else into the slot.

## Three drum tracks

**ALGORITHM** picks the track you see and play: **1, 2, 3** (synths), then **D1, D2, D3** (drums). The screen says *DRUM TRACK D2*; the TRACKS strip and the DRUMS screen say *d2* or *d3*.

- **The one you pick is the drum track** for everything: the DRUMS screen, the keys, REC, the step grid, the kit (PRESETS), LENGTH, DIV, swing, MUTE, PAN and LEVEL. D1's level is GLO → DRUMS → LVL, as before; D2 and D3 have their own.
- **All three play together**, wherever you are, and follow song mode.
- **At power-on** (and after NEW), D2 is on the **USR1** kit and D3 on **USR2**, both empty.
- **They are kept** with the working project and with every section A–H.
- They share the drums' **6 voices** (when more hits sound at once, the oldest gives way, the kick last), the INSERT, the FX filter and the sends.

## Shaping your samples

When the drum track you're on plays a **USR kit** (USR1–USR4 or USR3+4), these pages open on it. Factory kits still say *DRUM TRACK* there.

| Button | Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- | --- |
| **ENV** | ENV | ATK | DEC | SUS | REL |
| | ENV DEST | FLT: the envelope opens the filter | PIT: a pitch drop at the start | — | — |
| **LFO** | LFO | RATE | WAVE | PHASE | FADE |
| | LFO 2 | SYNC to the tempo | TRIG: NOTE / FREE | POL: BI / UNI | — |
| | LFO DEST | PIT: vibrato | FLT: wobble | — | AMP: tremolo |
| **EDIT** | SAMPLE | KIT | TUNE ±24 semitones | BITS | LOOP |
| | TONE | CUT | RES | DRV | — |
| | FILTER | TYPE: LP / BP / HP | CUT | RES | — |

- **Each hit holds for the step's GATE** (SEQ → PATTERN → GATE, a share of a step at the track's DIV), then plays its release. Played from the keys, a hit holds the same way. For long sounds, turn GATE up and DIV longer.
- **LOOP** plays the sample again from its start for as long as the hit holds: a short sample becomes a pad or a drone. Shape its end with REL.
- **The filter** is ANALOG's resonant one; ENV DEST → FLT and LFO DEST → FLT move it.
- **D1, D2 and D3 each have their own settings**, kept with the project and with each section A–H.
- **At the defaults** (no attack, SUS and REL full, CUT open, nothing else) a hit plays exactly as it did before 1.1, and costs no extra processing. Projects from before open that way.

## Eight song sections

| Hold SAVE and press | Does |
| --- | --- |
| keys **1–4** | play sections **A–D** on the next bar |
| keys **9–12** | play sections **E–H** |
| keys **5–8** | store the loop into **A–D**, or into **E–H** after key **15** switches them over (the tiles say which) |
| several keys | a quick chain, mixing A–H freely |

SAVE → SECTION lists all eight in two columns, the SONG screen picks any of them, and the local web editor's sections, Song, LIVE, backup and song-file pages carry E–H.

## The controls

| Control | What it does |
| --- | --- |
| **ALGORITHM** | the track: 1 · 2 · 3 (synths) · D1 · D2 · D3 (drums) |
| **PRESETS** | the selected track's sound, or the drum kit |
| **KNOB 1–4** | the four values at the bottom of the screen, each in its colour |
| **OCT− / OCT+** | octave (both: back to 0) · on a drum track, held: ghost / hard hits |
| **FX · SEL · ENV · LFO · EDIT · GLO** | tap: their pages · hold FX, SEL, EDIT, GLO: a layer |
| **HOME** | the TRACKS screen · hold: the menu. **ABOUT** (the PHREAK version): hold HOME, turn **SELECT** to the SYSTEM section, **PRESETS** down to ABOUT, **OCT+** opens it, **OCT−** closes |
| **SAVE** | on TRACKS: the SONG screen · hold: the song layer (sections A–H, above) |
| **ARP · SEQ** | tap: their pages · hold: note repeat · steps |
| **PLAY · REC** | start / stop · record, overdub, hold to clear the track |
| **EDIT + OCT− / OCT+** | undo / redo |
| **OCT+ held at power-on** | LOAD FROM USB |
| **OCT− held at power-on** | USB rescue |

Every page, layer and combination is described in SLOOP's [GUIDE.md](GUIDE.md); this README lists what PHREAK changes.

## Everything from SLOOP

PHREAK keeps all of SLOOP 2.8.4. In short:

- **Twelve synth engines and 153 sounds:** analog, 4-op FM, 6-op FM with DX7 patches, phase distortion, three-oscillator, tonewheel organ, formant voice, granular, lo-fi chip, sampler, physical models and noise.
- **Drums:** 16 sounds on the white keys, ghost and hard hits, ratchets, 37 kits plus PHREAK's three, your USR and SYN kits, per-sound TUNE and DECAY.
- **The sequencer:** 64 steps per track with its own length and division, chords, ties and slides, parameter locks, micro timing, fill conditions, MPC swing, live recording with overdub, a free take or a set tempo.
- **Performance:** 27 punch-in effects, note repeat, live erase, chords and scales on the keys, mute, solo, fills, tap tempo.
- **Effects:** an INSERT and a filter on every track, chorus, tempo delay and reverb, the master DUST, DUCK, DJ filter and limiter, the TEXTURE bed.
- **MIDI and audio:** USB MIDI in and out, TRS MIDI in, MIDI clock in and out, CCs and MIDI LEARN, a USB audio input for recording on a computer or phone.
- **The web editor:** sounds, sequencer with piano roll, MIDI files, CHOP, drum kits and drum synth, DX7 cartridges, LIVE page, backups.

## Limits and differences from SLOOP

- **Sample slots are 68 KB** (about 6.3 s; 80 KB in SLOOP): the last 12 KB of each slot keep D2 and D3. A slot filled with more than 68 KB before PHREAK reads as empty: load it again.
- **D2 and D3:** only D1 has parameter locks; D2 and D3 send no MIDI while another drum track is shown.
- **The web editor** sees the drum track on screen; its backups keep D1, not D2 and D3. The version string it reads stays `SLOOP 2.8.4+`.
- **USR kit sound:** ENV SYNC and the SHAPE amounts do nothing on drums (their knobs show "—"); a hit holds for GATE, not for as long as you hold the key.
- **Kit numbers:** RAGE, PLUGG and MEMPHIS come after JAZZ, so the user kits (USR1–4, SYN1–4) are three numbers higher than in SLOOP.
- **FLUTE** keeps one note (C5) of its built-in sample set, to make room for LOAD FROM USB.
- **After a crash:** sections E–H and every section's D2 and D3 are not in the RAM that survives a reset. What you store while playing is lost only if the FM-1 crashes in the seconds before it's written to flash.

## Versions

| Version | Identity | What changed |
| --- | --- | --- |
| **1.2** | FM-1_912 | LOAD FROM USB lists a folder's WAV files: load one alone into a slot, across the keyboard, for a synth or bass track; *by dag* on the boot screen and ABOUT |
| **1.1** | FM-1_911 | ENV, LFO and EDIT pages for USR kits on the drum tracks: envelope with gate, resonant filter, LFO, tune, bits, drive, loop |
| **1.0** | FM-1_910 | the name PHREAK and its own boot screen and ABOUT page |
| **0.9** | FM-1_909 | drum tracks D2 and D3; the earlier payphone sounds, kits and screen removed; 68 KB sample slots |
| **0.8** | FM-1_908 | song sections A–H |
| **0.5–0.7** | | LOAD FROM USB: the stick, folders at any depth, long file names |
| **0.4** | | the USB host probe, finding the stick through a powered adapter |
| **0.1–0.3** | | first builds on SLOOP 2.8.4: a payphone theme and the trap sounds |

## Building and tests

You need the JieLi toolchain and the AC79 SDK, Python 3 with Pillow, and Docker on a Mac (see [BUILDING.md](BUILDING.md)). Then:

```bash
sh build.sh --release 1.2
```

That writes `build/phreak-1.2.fwsc`. The JieLi clang sometimes crashes under Docker on Apple silicon; run the build again and it goes through.

```bash
sh tests/run_tests.sh
```

This runs the host test suite with no hardware: audio renders against golden hashes, the sequencer, the UI pages, flash storage, the update loader, MIDI, the web pages, and PHREAK's own tests ([usbstick_test.c](tests/usbstick_test.c), [xdrums_test.c](tests/xdrums_test.c), [usrsound_test.c](tests/usrsound_test.c)). One check fails, *target cost of the render loops*: it fails on plain SLOOP 2.8.4 too, because its budget file is out of date upstream.

The boot logo comes from [tools/gen_phreak_logo.py](tools/gen_phreak_logo.py) (`--assets DIR` writes the PNGs used here).

## Credits

- **PHREAK** by **[dag](https://dagnazty.dev/)**.
- **[SLOOP](https://github.com/isod89/sloop-fm1)** by **isod89**: everything PHREAK is built on, and everyone SLOOP credits (see [SLOOP.md](SLOOP.md) and [GUIDE.md](GUIDE.md)).
- **[Felucca](https://github.com/hugelton/Felucca)** by **Leo Kuroshita** (@kurogedelic) / **Hügelton Instruments**: the firmware SLOOP grew from.
- The USB host mode follows JieLi's AC79 SDK.
- Samples: Versilian Studios VSCO-2 CE and VCSL, Sonic Pi (CC0). Font: Terminus (SIL OFL 1.1). Icons: Fukiai (MIT). PHASE engine after CrispyZebra (GPL); VOICE after klattsch (MIT); FM6: msfa from Dexed (Apache-2.0). Details in [LICENSING.md](LICENSING.md).

## Licence

Code: GPL-3.0-only (see [LICENSE](LICENSE), and [LICENSING.md](LICENSING.md) for the assets). No warranty. M-VAVE and FM-1 are trademarks of their owners; PHREAK is not affiliated with M-VAVE or with the SLOOP and Felucca projects. Drum kit names describe styles, not products.
