#!/usr/bin/env python3
"""Build-time music generator for the FujiNet demo.

The tune ("Pocket Packets", an original composition for this demo) is written
below as Python data: bars of (instrument, note, rows) events per SID voice.
This script turns it into the tables the player in src/player.s reads.

    python3 demo/tools/gen_music.py OUT.inc

Pattern bytes (one pattern = one 16-row bar of one voice):
    $00-$5f  note (C-0 = 0): start it with the current instrument and length
    $60-$6f  set instrument
    $80-$bf  set length to (byte & $3f) + 1 rows
    $c0      rest (gate off) for the current length
    $ff      end of pattern
Order lists: pattern numbers, then $ff and the position to loop back to.
"""
import sys

SPEED = 6            # frames per row: 50 Hz / 6 = 8.3 rows/s, 125 BPM in 16ths
VIB_DELAY = 10       # frames before a held note starts its vibrato
FILTER_RES = 0xC0    # $d417 resonance (high nibble); bit 0 routes voice 1
PAL_CLOCK = 985248
NTSC_CLOCK = 1022727

NOTE_NAMES = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6,
              "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}


def n(name):
    """'A4' -> 57 (C-0 = 0)."""
    return NOTE_NAMES[name[:-1]] + 12 * int(name[-1])


# ---------------------------------------------------------------- instruments
# wavetable programs: (control byte, note) per frame. note < $80 is added to
# the played note, $80 + n plays absolute note n (drums). ("jump", i) loops to
# entry i of the same program.
INSTRUMENTS = {}
WAVETABLE = []


def instrument(name, ad, sr, program, pw=0x08, pw_speed=0, vibrato=False, filtered=False):
    start = len(WAVETABLE)
    for wave, note in program:
        if wave == "jump":
            WAVETABLE.append((0xFF, start + note))
        else:
            WAVETABLE.append((wave, note))
    flags = (1 if vibrato else 0) | (2 if filtered else 0)
    INSTRUMENTS[name] = dict(index=len(INSTRUMENTS), ad=ad, sr=sr, pw=pw,
                             pw_speed=pw_speed, wt=start, flags=flags)


def absn(note):
    return 0x80 + note


instrument("bass", 0x08, 0x80, [(0x21, 0), ("jump", 0)], filtered=True)
instrument("kick", 0x08, 0x00, [(0x81, absn(76)), (0x41, absn(50)), (0x41, absn(44)),
                                (0x41, absn(40)), (0x41, absn(36)), (0x41, absn(33)),
                                (0x40, absn(31)), ("jump", 6)])
instrument("snare", 0x08, 0x00, [(0x81, absn(88)), (0x41, absn(48)), (0x41, absn(46)),
                                 (0x81, absn(84)), (0x81, absn(82)), ("jump", 4)])
instrument("hat", 0x02, 0x00, [(0x81, absn(94)), ("jump", 0)])
instrument("ohat", 0x05, 0x00, [(0x81, absn(93)), ("jump", 0)])
instrument("arpmin", 0x05, 0x96, [(0x41, 0), (0x41, 3), (0x41, 7), ("jump", 0)],
           pw=0x04, pw_speed=0x30)
instrument("arpmaj", 0x05, 0x96, [(0x41, 0), (0x41, 4), (0x41, 7), ("jump", 0)],
           pw=0x04, pw_speed=0x30)
instrument("lead", 0x09, 0xA8, [(0x21, 0), (0x41, 0), ("jump", 1)],
           pw=0x08, pw_speed=0x18, vibrato=True)
instrument("pad", 0x48, 0xC8, [(0x21, 0), ("jump", 0)], filtered=True)
instrument("lead2", 0x6A, 0x9A, [(0x41, 0), ("jump", 0)], pw=0x06, pw_speed=0x10,
           vibrato=True)

# ---------------------------------------------------------------------- song
# Chords: (bass root, arp root, arp instrument)
CH = {
    "Am": (n("A2"), n("A3"), "arpmin"),
    "F":  (n("F2"), n("F3"), "arpmaj"),
    "C":  (n("C3"), n("C4"), "arpmaj"),
    "G":  (n("G2"), n("G3"), "arpmaj"),
    "E":  (n("E2"), n("E3"), "arpmaj"),
}
PROG_A = ["Am", "F", "C", "G"]
PROG_B = ["Am", "F", "G", "E"]


def bass_intro(ch):
    r, _, _ = CH[ch]
    seq = [(r, 2), (r + 12, 1), (r, 1), (r, 2), (r + 12, 2), (r, 2), (r + 12, 1), (r, 1),
           (r, 2), (r + 12, 2)]
    return [("bass", note, d) for note, d in seq]


def groove(ch, fill=False):
    r, _, _ = CH[ch]
    K, S, B = "kick", "snare", "bass"
    if not fill:
        seq = [(K, r, 2), (B, r, 1), (B, r + 12, 1), (S, r, 2), (B, r, 1), (B, r + 12, 1),
               (K, r, 2), (B, r, 1), (K, r, 1), (S, r, 2), (B, r + 12, 1), (B, r, 1)]
    else:
        seq = [(K, r, 2), (B, r, 1), (B, r + 12, 1), (S, r, 2), (B, r, 1), (B, r + 12, 1),
               (S, r, 1), (S, r, 1), (K, r, 1), (S, r, 1), (S, r, 1), (S, r, 1), (S, r, 1),
               (S, r, 1)]
    return seq


def pad(ch):
    return [("pad", CH[ch][0], 16)]


def arp_hits(ch):
    _, root, ins = CH[ch]
    return [(ins, root, d) for d in (3, 3, 2, 3, 3, 2)]


def arp_long(ch):
    _, root, ins = CH[ch]
    return [(ins, root, 16)]


def hats():
    H, O = "hat", "ohat"
    return [(H, 0, 2), (H, 0, 2), (H, 0, 1), (H, 0, 1), (O, 0, 2),
            (H, 0, 2), (H, 0, 2), (H, 0, 1), (H, 0, 1), (O, 0, 2)]


def rest():
    return [("lead", None, 16)]


def melody(bars, ins="lead"):
    out = []
    for bar in bars:
        events = []
        for tok in bar.split():
            name, d = tok.split(":")
            events.append((ins, None if name == "-" else n(name), int(d)))
        out.append(events)
    return out


MELODY_1 = melody([
    "A4:4 C5:2 E5:2 D5:4 C5:2 B4:2",
    "C5:6 A4:2 F4:4 A4:4",
    "G4:4 C5:2 E5:2 G5:4 E5:4",
    "D5:6 B4:2 G4:4 B4:2 D5:2",
    "E5:4 A5:4 G5:2 E5:2 C5:4",
    "F5:4 E5:2 D5:2 C5:4 A4:4",
    "B4:4 D5:2 G5:2 F5:4 D5:4",
    "E5:8 G#4:4 B4:4",
])
MELODY_2 = MELODY_1[:4] + melody([
    "A5:2 G5:2 E5:2 G5:2 A5:4 C6:4",
    "A5:4 G5:2 F5:2 E5:4 C5:4",
    "D5:2 E5:2 G5:4 B5:4 A5:2 G5:2",
    "G#5:8 B5:4 E5:4",
])
BREAKDOWN = melody([
    "E5:8 C5:8",
    "A4:8 C5:8",
    "G4:8 E5:8",
    "D5:12 B4:4",
], ins="lead2")

# Sections: each bar is (voice 1, voice 2, voice 3) event lists
SONG = []
for i, ch in enumerate(PROG_A):                                  # intro
    SONG.append((bass_intro(ch), arp_hits(ch), hats() if i >= 2 else rest()))
LOOP_BAR = len(SONG)
for i, ch in enumerate(PROG_A + PROG_B):                         # verse
    SONG.append((groove(ch, fill=(i == 7)), arp_hits(ch), hats()))
for i, ch in enumerate(PROG_A + PROG_B):                         # chorus
    SONG.append((groove(ch, fill=(i == 7)), arp_hits(ch), MELODY_1[i]))
for i, ch in enumerate(PROG_A):                                  # breakdown
    SONG.append((pad(ch), arp_long(ch), BREAKDOWN[i]))
for i, ch in enumerate(PROG_A + PROG_B):                         # chorus 2
    SONG.append((groove(ch, fill=(i == 7)), arp_hits(ch), MELODY_2[i]))


# ------------------------------------------------------------------ encoding
def encode(events):
    out, ins, dur = [], None, None
    assert sum(d for _, _, d in events) == 16, events
    for name, note, d in events:
        if name != ins:
            out.append(0x60 + INSTRUMENTS[name]["index"])
            ins = name
        if d != dur:
            assert 1 <= d <= 64
            out.append(0x80 + d - 1)
            dur = d
        if note is None:
            out.append(0xC0)
        else:
            assert 0 <= note < 0x60, note
            out.append(note)
    return out + [0xFF]


def freq_table(clock):
    regs = []
    for note in range(96):
        hz = 440.0 * 2 ** ((note - 57) / 12)
        regs.append(min(65535, round(hz * 16777216 / clock)))
    return regs


def byte_lines(data, per_line=16):
    return "\n".join("        .byte " + ",".join(f"${b:02x}" for b in data[i:i + per_line])
                     for i in range(0, len(data), per_line))


def main():
    out = sys.argv[1]
    patterns, index = [], {}
    orders = [[], [], []]
    for bar in SONG:
        for v in range(3):
            data = tuple(encode(bar[v]))
            if data not in index:
                index[data] = len(patterns)
                patterns.append(data)
            orders[v].append(index[data])
    assert len(patterns) < 256 and len(INSTRUMENTS) <= 16 and len(WAVETABLE) < 256

    ins = sorted(INSTRUMENTS.values(), key=lambda i: i["index"])
    pal, ntsc = freq_table(PAL_CLOCK), freq_table(NTSC_CLOCK)
    vib = [max(1, round(f * (2 ** (0.13 / 12) - 1))) for f in pal]

    with open(out, "w") as f:
        f.write("; generated by demo/tools/gen_music.py -- do not edit\n\n")
        f.write(f"SPEED = {SPEED}\nVIB_DELAY = {VIB_DELAY}\nFILTER_RES = ${FILTER_RES:02x}\n")
        f.write(f"SONG_BARS = {len(SONG)}\n\n")
        for key in ("ad", "sr", "pw", "pw_speed", "wt", "flags"):
            f.write(f"ins_{key}:\n" + byte_lines([i[key] for i in ins]) + "\n")
        f.write("wt_wave:\n" + byte_lines([w for w, _ in WAVETABLE]) + "\n")
        f.write("wt_note:\n" + byte_lines([x for _, x in WAVETABLE]) + "\n")
        f.write("pat_lo:\n" + "\n".join(f"        .byte <pat{i}" for i in range(len(patterns))) + "\n")
        f.write("pat_hi:\n" + "\n".join(f"        .byte >pat{i}" for i in range(len(patterns))) + "\n")
        for v in range(3):
            f.write(f"order{v + 1}:\n" + byte_lines(orders[v] + [0xFF, LOOP_BAR]) + "\n")
        for i, p in enumerate(patterns):
            f.write(f"pat{i}:\n" + byte_lines(list(p)) + "\n")
        for name, tab in (("pal", pal), ("ntsc", ntsc)):
            f.write(f"freq_{name}_lo:\n" + byte_lines([r & 0xFF for r in tab]) + "\n")
            f.write(f"freq_{name}_hi:\n" + byte_lines([r >> 8 for r in tab]) + "\n")
        f.write("vib_lo:\n" + byte_lines([r & 0xFF for r in vib]) + "\n")
        f.write("vib_hi:\n" + byte_lines([r >> 8 for r in vib]) + "\n")
    size = sum(len(p) for p in patterns)
    print(f"gen_music: {len(SONG)} bars, {len(patterns)} patterns ({size} bytes), "
          f"{len(INSTRUMENTS)} instruments, {len(WAVETABLE)} wavetable steps -> {out}")


if __name__ == "__main__":
    main()
