#!/usr/bin/env python3
"""Builds Chimera's built-in FM voice library.

Input : a file of unique 128-byte packed DX7 voices (made by scan_carts.py from a folder of .syx
        carts). Output: Source/Synth/dsp/FmLibraryData.cpp (voices sorted into sound categories).

Sorting: the voice name decides first (keyword lists below); voices with unclear names are sorted
by their settings (carrier envelope shape, ratios, transpose, algorithm). Inside a category the
voices sorted by name come first (A-Z), then the ones sorted by settings (A-Z).
Usage: build_fm_library.py voices.bin Source/Synth/dsp/FmLibraryData.cpp
"""
import sys, re, math, collections

CATS = ["Piano", "E.Piano", "Organ", "Keys", "Bass", "Brass", "Winds", "Strings", "Pads", "Voices",
        "Leads", "Synths", "Bells", "Mallets", "Plucks", "Guitars", "Drums", "FX"]

# (category, keywords). Order matters: first hit wins. Keywords match the name with spaces,
# dots and digits removed, so "E.PIANO 1" -> "EPIANO".
RULES = [
    ("Bass",    "BASS BAS5 BAZZ BASZ FRETLES SLAPB UPRIGHT SUBBAS BS1 BS2 CONTRAB"),
    ("E.Piano", "EPIANO EPNO EPN ELPIANO ELECPIAN EPF RHODE RODES ROADS RHOD WURL TINE FENDER FULLTIN DYNO STAGE73 "
                "ELEGRAND ELECGRAND EGRAND CP70 CP80 EPIA FMPIANO DXPIANO REEDPNO PIANET"),
    ("Piano",   "PIANO PIANA PNO GRAND STEINW BOSEN UPRIGHTP HONKY TACKP PIAN KLAVIER FLUGEL BECHST YAMAHAC"),
    ("Organ",   "ORGAN ORGN HAMMOND B3 LESLIE DRAWBAR PIPES PIPEO CHURCH TONEWH FARFIS VOX CATHED PERCORG ROTARY "
                "ACCORD HARMONIU MELODICA BANDONE MUSETTE CALLIOP REEDORG PUFFO"),
    ("Keys",    "CLAV HARPSI HARPSC CEMBALO CLAVI CELEST SPINET VIRGINAL KEYS KEYBOARD TOYPIAN CLV HRPSI"),
    ("Brass",   "BRASS BRAS BRS TRUMPET TRUMP TRPT TROMB TRBN TRMB TUBA HORN FLUGELH CORNET FANFAR BUGLE SECTION "
                "EUPHON SOUSA BIGBAND STAB HRN"),
    ("Winds",   "FLUTE FLUT SAX OBOE CLARIN CLAR BASSOON FAGOT PICCOLO RECORD PANFL PANPIP WHISTL OCARIN SHAKU PIPE "
                "HARMONICA HARMNCA BLUESHARP REED WOODW OBOI ENGLH BAGPIP FIFE BOTTLE BLOW BREATH WIND QUENA SYRINX"),
    ("Strings", "STRING STRNG STRG STRS VIOLIN VIOLA CELLO VLN VIOL FIDDLE ORCHES ORCH ENSEMB PIZZ ARCO TREMOL "
                "CHAMBER QUARTET SYMPH SOLINA BOWED"),
    ("Voices",  "VOICE VOX CHOIR CHOR VOCAL VOCO VOWEL HUMAN SING ANGEL AAH OOH AHH CHANT SOPRAN TENOR MALE FEMALE "
                "BOYS GIRL CHORAL VOIC WHISPER FORMANT TALK"),
    ("Guitars", "GUITAR GUIT GTR GIT STRAT TELECAS LESPAUL 12STR NYLON STEELG ACOUST BANJO MANDOL SITAR KOTO SHAMIS "
                "UKULE LUTE OUD BOUZ DOBRO PEDALST BALALAI DULCIM ZITHER SAZ JAZZG FUZZG DISTG WAHG MUTEG ELGUIT HARP"),
    ("Mallets", "MARIMB VIBRA VIBES VIBE XYLO GLOCK KALIMB STEELDR STEELD MALLET BALAFON LOGDRUM TUBULAR WOODBL "
                "GAMELAN MBIRA CELESTA MUSICBOX MUSBOX MUSIKBOX TINKLE"),
    ("Bells",   "BELL CHIME GONG CARILL HANDB TUBBEL CRYSTAL GLASS TINKL DING CLANG CHURCHB COWBEL"),
    ("Drums",   "DRUM DRM KICK SNARE SNR TOM HIHAT HHAT CYMBAL CYMB CONGA BONGO TIMPAN TIMBAL CLAP CLAVE PERC "
                "SHAKER TAMBOUR TRIANG AGOGO CABASA TABLA TAIKO SIMMONS LINN RIMSH COWB CRASH RIDE GUIRO "
                "CASTAN BLOCK DJEMBE SYNDRUM"),
    ("FX",      "FX EFFECT NOISE WIND STORM RAIN THUNDER HELI LASER ZAP SIREN ALARM PHONE TELEPH BIRD TRAIN EXPLO "
                "SPACE UFO ALIEN GHOST SCREAM BOMB GUN SHOT MOTOR ENGINE JET OCEAN SURF RING "
                "CRASH BUZZ BLEEP COMPUT ROBOT R2D2 SWEEP DROP FALL RISE WHOOSH SCIFI STARW RANDOM SH& SEQ"),
    ("Pads",    "PAD ATMOS AMBIEN DREAM CLOUD WARM SOFT SLOW SWELL HEAVEN ETHER MIST FLOAT WASH DRONE AURA "
                "HALO LUSH VELVET SILK EVOLV DRIFT"),
    ("Leads",   "LEAD SOLO MOOG MINI SYNC SQUARE SAWL SAW PULSE PORTA GLIDE WHISTLER OBX PROPH JUPIT ARP ODYSS "
                "SCREAM FUNKY LYLE SQR"),
    ("Plucks",  "PLUCK PLUK PICK HARP ZIMB CIMBAL STACC SHORT"),
    ("Synths",  "SYN SYNTH POLY ANALOG DIGI DIGITAL FAIRL SYNCLAV PPG OBERH JUNO CS80 KORG ROLAND"),
]
RULES = [(c, k.split()) for c, k in RULES]

CARRIERS = [  # per algorithm 1..32, operator numbers (1..6) that are heard
    [1,3],[1,3],[1,4],[1,4],[1,3,5],[1,3,5],[1,3],[1,3],[1,3],[1,4],[1,4],[1,3],[1,3],[1,3],[1,3],[1],
    [1],[1],[1,4,5],[1,2,4],[1,2,4,5],[1,3,4,5],[1,2,4,5],[1,2,3,4,5],[1,2,3,4,5],[1,2,4],[1,2,4],
    [1,3,6],[1,2,3,5],[1,2,3,6],[1,2,3,4,5],[1,2,3,4,5,6]]

def op(v, n):   # operator n (1..6): packed data stores OP6 first
    o = v[(6 - n) * 17:(6 - n) * 17 + 17]
    return dict(r=o[0:4], l=o[4:8], level=o[14], fixed=o[15] & 1, coarse=(o[15] >> 1) & 31, fine=o[16])

def by_name(name):
    key = re.sub(r"[^A-Z&]", "", name.upper())
    if not key: return None
    for cat, kws in RULES:
        for k in kws:
            if k in key: return cat
    return None

def by_settings(v):
    algo = v[110] & 31
    transpose = min(v[117], 48)
    car = [op(v, n) for n in CARRIERS[algo]]
    car = [c for c in car if c["level"] > 50] or car
    mods = [op(v, n) for n in range(1, 7) if n not in CARRIERS[algo] and op(v, n)["level"] > 55]
    n = len(car)
    attack = sum(c["r"][0] for c in car) / n          # 99 = instant
    sustain = sum(min(c["l"][2], c["l"][0]) for c in car) / n
    decay = sum(c["r"][1] + c["r"][2] for c in car) / (2 * n)
    ratios = [(c["coarse"] or 0.5) for c in car if not c["fixed"]]
    low = (transpose - 24) + (12 * math.log2(min(ratios)) if ratios else 0) <= -19   # 1.5+ octaves down
    inharm = any(m["fixed"] or (m["fine"] not in (0, 50) and m["level"] > 65) for m in mods) or \
             any(m["coarse"] >= 7 for m in mods)
    fixed_car = all(c["fixed"] for c in car)
    if fixed_car and sustain < 30: return "Drums" if decay > 55 else "FX"
    if fixed_car: return "FX"
    if sustain >= 45:
        if attack < 48: return "Pads"
        if low: return "Bass"
        if n >= 4 and not inharm: return "Organ"
        return "Leads" if n <= 2 else "Synths"
    if low: return "Bass"
    if inharm: return "Bells" if decay < 45 else "Mallets"
    return "Plucks"

def main():
    data = open(sys.argv[1], "rb").read()
    voices = [data[i:i + 128] for i in range(0, len(data) - 127, 128)]
    # second de-dupe pass: same algorithm, feedback and operator tuning / level / envelopes
    seen = {}
    for v in voices:
        key = bytes([v[110] & 31, v[111] & 7]) + b"".join(v[k*17:k*17+8] + v[k*17+14:k*17+17] for k in range(6))
        name = bytes(c if 32 <= c < 127 else 32 for c in v[118:128]).decode("ascii")
        if key not in seen or (not seen[key][1].strip() and name.strip()):
            seen[key] = (v, name)
    groups = collections.defaultdict(list); named = 0; src = collections.Counter()
    for v, name in seen.values():
        cat = by_name(name)
        if cat: named += 1
        else: cat = by_settings(v)
        src[cat, bool(by_name(name))] += 1
        groups[cat].append(((not by_name(name), name.upper().strip()), v[:118] + name.encode("ascii")))
    out = ["// Generated by tools/build_fm_library.py: do not edit. Chimera's built-in FM voice library:",
           "// 128-byte packed DX7 voices, sorted into sound categories and by name inside each.",
           '#include "Synth/dsp/FmLibrary.h"', "", "namespace el::chimera::fmlib {", ""]
    blob = bytearray(); table = []
    for cat in CATS:
        g = sorted(groups.get(cat, []), key=lambda t: t[0])
        table.append((cat, len(blob) // 128, len(g)))
        for _, v in g: blob += v
    out.append("const Category kCategories[] = {")
    for cat, first, n in table: out.append('    { "%s", %d, %d },' % (cat, first, n))
    out += ["};", "const int kCategoryCount = %d;" % len(table), "const int kVoiceCount = %d;" % (len(blob) // 128), "",
            "const unsigned char kVoices[] = {"]
    for i in range(0, len(blob), 64):
        out.append(",".join(str(b) for b in blob[i:i + 64]) + ",")
    out += ["};", "", "} // namespace el::chimera::fmlib", ""]
    open(sys.argv[2], "w").write("\n".join(out))
    print("in", len(voices), "kept", len(blob) // 128, "sorted by name", named)
    for cat, first, n in table: print("  %-8s %5d voices  %4d carts  (%d by name, %d by settings)" % (cat, n, (n + 31) // 32, src[cat, True], src[cat, False]))

main()
