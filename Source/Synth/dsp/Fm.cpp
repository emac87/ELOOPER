// Chimera FM: DX7-style 6-operator engine + DX7 SysEx cartridge handling. Original code written
// in the spirit of the MSFA/Dexed engine (log-domain level math, 32 algorithms, 4-rate EGs).
#include "Synth/dsp/Fm.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace el::chimera {

namespace {

// ---------------------------------------------------------------- tables / small helpers
constexpr int kSineSize = 4096;
struct Tables {
    float sine[kSineSize + 1];
    float pegSemis[100];
    Tables() {
        for (int i = 0; i <= kSineSize; ++i)
            sine[i] = (float) std::sin (6.283185307179586 * (double) i / (double) kSineSize);
        static const int neg[20] { -128, -116, -104, -95, -85, -76, -68, -61, -56, -52,
                                   -49, -46, -43, -41, -39, -37, -35, -33, -32, -31 };
        for (int l = 0; l < 100; ++l) {
            int v;
            if (l <= 50) { int m = l; v = m < 20 ? neg[m] : m - 50; }
            else { int m = 100 - l; v = -(m < 20 ? neg[m] : m - 50); }
            pegSemis[l] = (float) v * (48.0f / 128.0f);
        }
    }
};
const Tables& tables() { static const Tables t; return t; }

inline float sineLookup (const float* tab, uint32_t ph) noexcept {
    const uint32_t idx = ph >> 20;
    const float frac = (float) (ph & 0xFFFFFu) * (1.0f / 1048576.0f);
    const float a = tab[idx], b = tab[idx + 1];
    return a + (b - a) * frac;
}

inline float softLimit (float x) noexcept {
    if (x != x) return 0.0f;
    const float a = std::fabs (x);
    if (a <= 1.0f) return x;
    const float e = a - 1.0f, y = 1.0f + e / (1.0f + e);
    return x < 0.0f ? -y : y;
}

// DX7 level 0..99 -> internal 0..127 (0.75 dB steps).
inline int scaleOut (int v) noexcept {
    static const int lut[20] { 0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46 };
    v = clampv (v, 0, 99);
    return v < 20 ? lut[v] : v + 28;
}
constexpr float kLog2PerUnit = 0.75f / 6.0205999f;   // one internal unit in octaves of amplitude

// ---------------------------------------------------------------- algorithms
// mods[i] = bitmask of op indexes (0 = OP1) that modulate op i; fbDst receives the feedback of fbSrc.
struct AlgDef { uint8_t mods[6]; uint8_t carriers; uint8_t fbDst, fbSrc; };
#define B(n) (1u << ((n) - 1))
const AlgDef kAlgs[32] = {
    /* 1*/ { { B(2), 0, B(4), B(5), B(6), 0 }, B(1)|B(3), 5, 5 },
    /* 2*/ { { B(2), 0, B(4), B(5), B(6), 0 }, B(1)|B(3), 1, 1 },
    /* 3*/ { { B(2), B(3), 0, B(5), B(6), 0 }, B(1)|B(4), 5, 5 },
    /* 4*/ { { B(2), B(3), 0, B(5), B(6), 0 }, B(1)|B(4), 5, 3 },
    /* 5*/ { { B(2), 0, B(4), 0, B(6), 0 }, B(1)|B(3)|B(5), 5, 5 },
    /* 6*/ { { B(2), 0, B(4), 0, B(6), 0 }, B(1)|B(3)|B(5), 5, 4 },
    /* 7*/ { { B(2), 0, B(4), B(5)|B(6), 0, 0 }, B(1)|B(3), 5, 5 },
    /* 8*/ { { B(2), 0, B(4), B(5)|B(6), 0, 0 }, B(1)|B(3), 3, 3 },
    /* 9*/ { { B(2), 0, B(4), B(5)|B(6), 0, 0 }, B(1)|B(3), 1, 1 },
    /*10*/ { { B(2), B(3), 0, B(5)|B(6), 0, 0 }, B(1)|B(4), 2, 2 },
    /*11*/ { { B(2), B(3), 0, B(5)|B(6), 0, 0 }, B(1)|B(4), 5, 5 },
    /*12*/ { { B(2), 0, B(4)|B(5)|B(6), 0, 0, 0 }, B(1)|B(3), 1, 1 },
    /*13*/ { { B(2), 0, B(4)|B(5)|B(6), 0, 0, 0 }, B(1)|B(3), 5, 5 },
    /*14*/ { { B(2), 0, B(4)|B(5), 0, B(6), 0 }, B(1)|B(3), 5, 5 },
    /*15*/ { { B(2), 0, B(4)|B(5), 0, B(6), 0 }, B(1)|B(3), 1, 1 },
    /*16*/ { { B(2)|B(3)|B(5), 0, B(4), 0, B(6), 0 }, B(1), 5, 5 },
    /*17*/ { { B(2)|B(3)|B(5), 0, B(4), 0, B(6), 0 }, B(1), 1, 1 },
    /*18*/ { { B(2)|B(3)|B(4), 0, 0, B(5), B(6), 0 }, B(1), 2, 2 },
    /*19*/ { { B(2), B(3), 0, B(6), B(6), 0 }, B(1)|B(4)|B(5), 5, 5 },
    /*20*/ { { B(3), B(3), 0, B(5)|B(6), 0, 0 }, B(1)|B(2)|B(4), 2, 2 },
    /*21*/ { { B(3), B(3), 0, B(6), B(6), 0 }, B(1)|B(2)|B(4)|B(5), 2, 2 },
    /*22*/ { { B(2), 0, B(6), B(6), B(6), 0 }, B(1)|B(3)|B(4)|B(5), 5, 5 },
    /*23*/ { { 0, B(3), 0, B(6), B(6), 0 }, B(1)|B(2)|B(4)|B(5), 5, 5 },
    /*24*/ { { 0, 0, B(6), B(6), B(6), 0 }, B(1)|B(2)|B(3)|B(4)|B(5), 5, 5 },
    /*25*/ { { 0, 0, 0, B(6), B(6), 0 }, B(1)|B(2)|B(3)|B(4)|B(5), 5, 5 },
    /*26*/ { { 0, B(3), 0, B(5)|B(6), 0, 0 }, B(1)|B(2)|B(4), 5, 5 },
    /*27*/ { { 0, B(3), 0, B(5)|B(6), 0, 0 }, B(1)|B(2)|B(4), 2, 2 },
    /*28*/ { { B(2), 0, B(4), B(5), 0, 0 }, B(1)|B(3)|B(6), 4, 4 },
    /*29*/ { { 0, 0, B(4), 0, B(6), 0 }, B(1)|B(2)|B(3)|B(5), 5, 5 },
    /*30*/ { { 0, 0, B(4), B(5), 0, 0 }, B(1)|B(2)|B(3)|B(6), 4, 4 },
    /*31*/ { { 0, 0, 0, 0, B(6), 0 }, B(1)|B(2)|B(3)|B(4)|B(5), 5, 5 },
    /*32*/ { { 0, 0, 0, 0, 0, 0 }, B(1)|B(2)|B(3)|B(4)|B(5)|B(6), 5, 5 },
};
#undef B

// ---------------------------------------------------------------- patch (un)packing
inline uint8_t cl (int v, int hi) noexcept { return (uint8_t) (v < 0 ? 0 : (v > hi ? hi : v)); }
inline char cleanChar (uint8_t c) noexcept { c &= 0x7F; return (c >= 32 && c < 127) ? (char) c : ' '; }

Dx7Patch fromPacked (const uint8_t* d) noexcept {
    Dx7Patch p;
    for (int k = 0; k < 6; ++k) {
        Dx7Op& o = p.op[(size_t) (5 - k)];
        const uint8_t* b = d + 17 * k;
        for (int i = 0; i < 4; ++i) { o.egRate[i] = cl (b[i] & 0x7F, 99); o.egLevel[i] = cl (b[4 + i] & 0x7F, 99); }
        o.kbdBreakPoint = cl (b[8] & 0x7F, 99);
        o.kbdLeftDepth = cl (b[9] & 0x7F, 99);
        o.kbdRightDepth = cl (b[10] & 0x7F, 99);
        o.kbdLeftCurve = b[11] & 3;
        o.kbdRightCurve = (b[11] >> 2) & 3;
        o.kbdRateScaling = b[12] & 7;
        o.detune = cl ((b[12] >> 3) & 15, 14);
        o.ampModSens = b[13] & 3;
        o.keyVelSens = (b[13] >> 2) & 7;
        o.outputLevel = cl (b[14] & 0x7F, 99);
        o.oscMode = b[15] & 1;
        o.freqCoarse = (b[15] >> 1) & 31;
        o.freqFine = cl (b[16] & 0x7F, 99);
    }
    for (int i = 0; i < 4; ++i) { p.pitchEgRate[i] = cl (d[102 + i] & 0x7F, 99); p.pitchEgLevel[i] = cl (d[106 + i] & 0x7F, 99); }
    p.algorithm = d[110] & 31;
    p.feedback = d[111] & 7;
    p.oscKeySync = (d[111] >> 3) & 1;
    p.lfoSpeed = cl (d[112] & 0x7F, 99);
    p.lfoDelay = cl (d[113] & 0x7F, 99);
    p.lfoPitchModDepth = cl (d[114] & 0x7F, 99);
    p.lfoAmpModDepth = cl (d[115] & 0x7F, 99);
    p.lfoKeySync = d[116] & 1;
    p.lfoWave = cl ((d[116] >> 1) & 7, 5);
    p.lfoPitchModSens = (d[116] >> 4) & 7;
    p.transpose = cl (d[117] & 0x7F, 48);
    for (int i = 0; i < 10; ++i) p.name[i] = cleanChar (d[118 + i]);
    p.name[10] = 0;
    return p;
}

void toPacked (const Dx7Patch& p, uint8_t* d) noexcept {
    for (int k = 0; k < 6; ++k) {
        const Dx7Op& o = p.op[(size_t) (5 - k)];
        uint8_t* b = d + 17 * k;
        for (int i = 0; i < 4; ++i) { b[i] = cl (o.egRate[i], 99); b[4 + i] = cl (o.egLevel[i], 99); }
        b[8] = cl (o.kbdBreakPoint, 99);
        b[9] = cl (o.kbdLeftDepth, 99);
        b[10] = cl (o.kbdRightDepth, 99);
        b[11] = (uint8_t) ((o.kbdLeftCurve & 3) | ((o.kbdRightCurve & 3) << 2));
        b[12] = (uint8_t) ((o.kbdRateScaling & 7) | (cl (o.detune, 14) << 3));
        b[13] = (uint8_t) ((o.ampModSens & 3) | ((o.keyVelSens & 7) << 2));
        b[14] = cl (o.outputLevel, 99);
        b[15] = (uint8_t) ((o.oscMode & 1) | ((o.freqCoarse & 31) << 1));
        b[16] = cl (o.freqFine, 99);
    }
    for (int i = 0; i < 4; ++i) { d[102 + i] = cl (p.pitchEgRate[i], 99); d[106 + i] = cl (p.pitchEgLevel[i], 99); }
    d[110] = p.algorithm & 31;
    d[111] = (uint8_t) ((p.feedback & 7) | ((p.oscKeySync & 1) << 3));
    d[112] = cl (p.lfoSpeed, 99);
    d[113] = cl (p.lfoDelay, 99);
    d[114] = cl (p.lfoPitchModDepth, 99);
    d[115] = cl (p.lfoAmpModDepth, 99);
    d[116] = (uint8_t) ((p.lfoKeySync & 1) | (cl (p.lfoWave, 5) << 1) | ((p.lfoPitchModSens & 7) << 4));
    d[117] = cl (p.transpose, 48);
    bool ended = false;
    for (int i = 0; i < 10; ++i) {
        char c = p.name[i];
        if (c == 0) ended = true;
        d[118 + i] = (uint8_t) (ended ? ' ' : cleanChar ((uint8_t) c));
    }
}

uint8_t checksum (const uint8_t* d, size_t n) noexcept {
    int sum = 0;
    for (size_t i = 0; i < n; ++i) sum += d[i] & 0x7F;
    return (uint8_t) ((-sum) & 0x7F);
}

} // namespace

// ---------------------------------------------------------------- public patch API
Dx7Patch dx7InitVoice() noexcept {
    Dx7Patch p;
    p.op[0].outputLevel = 99;
    return p;
}

std::array<uint8_t, 155> dx7ToVced (const Dx7Patch& p) noexcept {
    std::array<uint8_t, 155> v {};
    for (int k = 0; k < 6; ++k) {
        const Dx7Op& o = p.op[(size_t) (5 - k)];
        uint8_t* b = v.data() + 21 * k;
        for (int i = 0; i < 4; ++i) { b[i] = o.egRate[i]; b[4 + i] = o.egLevel[i]; }
        b[8] = o.kbdBreakPoint; b[9] = o.kbdLeftDepth; b[10] = o.kbdRightDepth;
        b[11] = o.kbdLeftCurve; b[12] = o.kbdRightCurve; b[13] = o.kbdRateScaling;
        b[14] = o.ampModSens; b[15] = o.keyVelSens; b[16] = o.outputLevel;
        b[17] = o.oscMode; b[18] = o.freqCoarse; b[19] = o.freqFine; b[20] = o.detune;
    }
    for (int i = 0; i < 4; ++i) { v[(size_t) (126 + i)] = p.pitchEgRate[i]; v[(size_t) (130 + i)] = p.pitchEgLevel[i]; }
    v[134] = p.algorithm; v[135] = p.feedback; v[136] = p.oscKeySync;
    v[137] = p.lfoSpeed; v[138] = p.lfoDelay; v[139] = p.lfoPitchModDepth; v[140] = p.lfoAmpModDepth;
    v[141] = p.lfoKeySync; v[142] = p.lfoWave; v[143] = p.lfoPitchModSens; v[144] = p.transpose;
    bool ended = false;
    for (int i = 0; i < 10; ++i) {
        if (p.name[i] == 0) ended = true;
        v[(size_t) (145 + i)] = (uint8_t) (ended ? ' ' : cleanChar ((uint8_t) p.name[i]));
    }
    return v;
}

Dx7Patch dx7FromVced (const uint8_t* v) noexcept {
    Dx7Patch p;
    for (int k = 0; k < 6; ++k) {
        Dx7Op& o = p.op[(size_t) (5 - k)];
        const uint8_t* b = v + 21 * k;
        for (int i = 0; i < 4; ++i) { o.egRate[i] = cl (b[i], 99); o.egLevel[i] = cl (b[4 + i], 99); }
        o.kbdBreakPoint = cl (b[8], 99); o.kbdLeftDepth = cl (b[9], 99); o.kbdRightDepth = cl (b[10], 99);
        o.kbdLeftCurve = b[11] & 3; o.kbdRightCurve = b[12] & 3; o.kbdRateScaling = b[13] & 7;
        o.ampModSens = b[14] & 3; o.keyVelSens = b[15] & 7; o.outputLevel = cl (b[16], 99);
        o.oscMode = b[17] & 1; o.freqCoarse = b[18] & 31; o.freqFine = cl (b[19], 99); o.detune = cl (b[20], 14);
    }
    for (int i = 0; i < 4; ++i) { p.pitchEgRate[i] = cl (v[126 + i], 99); p.pitchEgLevel[i] = cl (v[130 + i], 99); }
    p.algorithm = v[134] & 31; p.feedback = v[135] & 7; p.oscKeySync = v[136] & 1;
    p.lfoSpeed = cl (v[137], 99); p.lfoDelay = cl (v[138], 99);
    p.lfoPitchModDepth = cl (v[139], 99); p.lfoAmpModDepth = cl (v[140], 99);
    p.lfoKeySync = v[141] & 1; p.lfoWave = cl (v[142], 5); p.lfoPitchModSens = v[143] & 7;
    p.transpose = cl (v[144], 48);
    for (int i = 0; i < 10; ++i) p.name[i] = cleanChar (v[145 + i]);
    p.name[10] = 0;
    return p;
}

std::vector<uint8_t> dx7MakeCart (const std::vector<Dx7Patch>& voices) {
    std::vector<uint8_t> out (4104, 0);
    const uint8_t hdr[6] { 0xF0, 0x43, 0x00, 0x09, 0x20, 0x00 };
    std::memcpy (out.data(), hdr, 6);
    for (size_t i = 0; i < 32; ++i)
        toPacked (i < voices.size() ? voices[i] : dx7InitVoice(), out.data() + 6 + 128 * i);
    out[4102] = checksum (out.data() + 6, 4096);
    out[4103] = 0xF7;
    return out;
}

bool parseDx7SysEx (const uint8_t* data, size_t size, std::vector<Dx7Patch>& out, std::string& error) {
    out.clear();
    error.clear();
    if (data == nullptr || size == 0) { error = "empty data"; return false; }

    const uint8_t* packed = nullptr;
    if (data[0] == 0xF0) {
        if (size < 6) { error = "SysEx message too short"; return false; }
        if (data[1] != 0x43) { error = "not a Yamaha SysEx message"; return false; }
        if (data[3] == 0x09) {
            if (size < 4104) { error = "DX7 32-voice bulk dump is truncated"; return false; }
            packed = data + 6;
        } else if (data[3] == 0x00) {
            if (size < 163) { error = "DX7 single-voice dump is truncated"; return false; }
            out.push_back (dx7FromVced (data + 6));
            return true;
        } else {
            error = "Yamaha SysEx, but not a DX7 voice dump (unsupported format byte)";
            return false;
        }
    } else if (size == 4096) {
        packed = data;
    } else {
        error = "not DX7 data (expected SysEx or 4096 raw packed bytes)";
        return false;
    }
    out.reserve (32);
    for (int i = 0; i < 32; ++i) out.push_back (fromPacked (packed + 128 * i));
    return true;
}

// ---------------------------------------------------------------- FmVoice
void FmVoice::prepare (float sampleRate) noexcept {
    sr = sampleRate > 1000.0f ? sampleRate : 48000.0f;
    invSr = 1.0f / sr;
    (void) tables();
    patch = nullptr;
    released = true;
    silent = true;
    blockLeft = 0;
    for (auto& o : ops) o = OpState();
}

void FmVoice::noteOn (const Dx7Patch& p, int noteIn, int velocity) noexcept {
    patch = &p;
    note = clampv (noteIn, 0, 127);
    velocity = clampv (velocity, 1, 127);
    released = false;
    silent = false;
    blockLeft = 0;
    algo = std::min<int> (p.algorithm, 31);
    rng.seed ((uint32_t) (note * 2654435761u) ^ 0x1234567u);

    const AlgDef& A = kAlgs[algo];
    fbScale = p.feedback ? std::exp2 ((float) p.feedback - 9.0f) : 0.0f;
    fbH0 = fbH1 = 0.0f;

    const float velAtt = std::log2 (127.0f / (float) std::max (velocity, 16));
    const int rateX = clampv (note / 3 - 7, 0, 31);

    for (int i = 0; i < 6; ++i) {
        const Dx7Op& o = p.op[(size_t) i];
        OpState& s = ops[(size_t) i];

        // keyboard level scaling
        const int offset = note - (std::min<int> (o.kbdBreakPoint, 99) + 21);
        int group, depth, curve;
        if (offset >= 0) { group = offset / 3; depth = o.kbdRightDepth; curve = o.kbdRightCurve; }
        else { group = (-offset + 2) / 3; depth = o.kbdLeftDepth; curve = o.kbdLeftCurve; }
        int scale;
        if (curve == 0 || curve == 3) scale = (group * depth * 329) >> 12;
        else {
            static const int expTab[33] { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66,
                                          80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250 };
            scale = (expTab[std::min (group, 32)] * depth * 329) >> 15;
        }
        if (curve < 2) scale = -scale;

        const float vel = o.keyVelSens ? -(float) o.keyVelSens * 2.3f * velAtt : 0.0f;
        s.opLevel = clampv ((float) (scaleOut (o.outputLevel) + scale) + vel, 0.0f, 127.0f);
        static const float amTab[4] { 0.0f, 0.2588f, 0.4274f, 1.0f };
        s.amSens = amTab[o.ampModSens & 3];

        const int rateDelta = (o.kbdRateScaling * rateX) >> 3;
        for (int k = 0; k < 4; ++k) {
            const int q = std::min (63, ((int) std::min<int> (o.egRate[k], 99) * 41 >> 6) + rateDelta);
            s.speed[k] = 1.15f * std::exp2 ((float) q * 0.25f);
            s.target[k] = (float) scaleOut (o.egLevel[k]);
        }
        s.level = s.target[3];
        if (s.target[0] > s.level && s.level < 53.0f) s.level = std::min (53.0f, s.target[0]);
        s.stage = 0;

        s.fixed = o.oscMode != 0;
        s.fixedHz = std::pow (10.0f, (float) (o.freqCoarse & 3) + (float) std::min<int> (o.freqFine, 99) * 0.01f);
        s.ratio = (o.freqCoarse == 0 ? 0.5f : (float) o.freqCoarse) * (1.0f + (float) std::min<int> (o.freqFine, 99) * 0.01f);
        s.detuneRatio = std::exp2 ((float) ((int) o.detune - 7) * 1.5f / 1200.0f);
        s.phase = p.oscKeySync ? 0u : rng.nextU();
        s.amp = 0.0f; s.ampStep = 0.0f; s.inc = 0;
    }

    // carriers that can actually sound share the headroom (1 carrier = unity)
    int nCar = 0;
    for (int i = 0; i < 6; ++i) {
        if (! (A.carriers & (1u << i)) || ops[(size_t) i].opLevel < 1.0f) continue;
        const Dx7Op& o = p.op[(size_t) i];
        if (o.egLevel[0] || o.egLevel[1] || o.egLevel[2] || o.egLevel[3]) ++nCar;
    }
    carrierScale = nCar > 1 ? 1.0f / std::sqrt ((float) nCar) : 1.0f;

    // pitch EG
    pegOn = false;
    for (int k = 0; k < 4; ++k) if (p.pitchEgLevel[k] != 50) pegOn = true;
    const Tables& T = tables();
    for (int k = 0; k < 4; ++k) {
        pegTarget[k] = T.pegSemis[std::min<int> (p.pitchEgLevel[k], 99)];
        pegSpeed[k] = 12000.0f * std::exp2 (((float) std::min<int> (p.pitchEgRate[k], 99) - 99.0f) / 7.5f);
    }
    pegVal = pegOn ? pegTarget[3] : 0.0f;
    pegStage = pegOn ? 0 : 5;

    // LFO
    lfoHz = 0.06f + 50.0f * std::pow ((float) std::min<int> (p.lfoSpeed, 99) / 99.0f, 2.2f);
    lfoPhase = p.lfoKeySync ? 0.0 : (double) rng.uni();
    lfoT = 0.0f;
    const float dl = (float) std::min<int> (p.lfoDelay, 99) / 99.0f;
    const float total = 7.0f * std::pow (dl, 1.7f);
    lfoHold = 0.4f * total;
    lfoFade = 0.6f * total;
    lfoSH = rng.bi();
    lfoSemis = 0.0f; lfoAmU = 0.0f;
    curPitch = 0.0f;
}

void FmVoice::noteOff() noexcept {
    if (released || patch == nullptr) return;
    released = true;
    for (auto& s : ops) if (s.stage != 5) s.stage = 4;
    if (pegStage != 5) pegStage = 4;
}

bool FmVoice::active() const noexcept {
    return patch != nullptr && !(released && silent);
}

void FmVoice::computeIncs (float pitchSemis) noexcept {
    curPitch = pitchSemis;
    const float semis = (float) (note - 69) + ((float) patch->transpose - 24.0f) + pitchSemis + pegVal + lfoSemis;
    const double baseHz = 440.0 * std::exp2 ((double) semis * (1.0 / 12.0));
    for (auto& s : ops) {
        double f = s.fixed ? (double) s.fixedHz : baseHz * (double) s.ratio;
        f *= (double) s.detuneRatio;
        double c = f * (double) invSr;
        if (c > 0.45) c = 0.45;
        if (c < 0.0) c = 0.0;
        s.inc = (uint32_t) (c * 4294967296.0);
    }
}

void FmVoice::updateControl() noexcept {
    const float dt = (float) kSubBlock * invSr;
    const Dx7Patch& p = *patch;

    // operator EGs
    for (auto& s : ops) {
        for (int guard = 0; guard < 4; ++guard) {
            if (s.stage == 3 || s.stage == 5) break;
            const int k = s.stage == 4 ? 3 : s.stage;
            const float tgt = s.target[k];
            bool advance = false, consumed = true;
            if (s.level < tgt) {
                const float slope = 1.0f + 16.0f * (1.0f - s.level * (1.0f / 127.0f));
                s.level += s.speed[k] * dt * slope;
                if (s.level >= tgt) { s.level = tgt; advance = true; }
            } else if (s.level > tgt) {
                s.level -= s.speed[k] * dt;
                if (s.level <= tgt) { s.level = tgt; advance = true; }
            } else { advance = true; consumed = false; }
            if (advance) s.stage = s.stage == 4 ? 5 : s.stage + 1;
            if (consumed) break;
        }
    }

    // pitch EG
    if (pegOn) {
        for (int guard = 0; guard < 4; ++guard) {
            if (pegStage == 3 || pegStage == 5) break;
            const int k = pegStage == 4 ? 3 : pegStage;
            const float tgt = pegTarget[k];
            bool advance = false, consumed = true;
            if (pegVal < tgt) { pegVal += pegSpeed[k] * dt; if (pegVal >= tgt) { pegVal = tgt; advance = true; } }
            else if (pegVal > tgt) { pegVal -= pegSpeed[k] * dt; if (pegVal <= tgt) { pegVal = tgt; advance = true; } }
            else { advance = true; consumed = false; }
            if (advance) pegStage = pegStage == 4 ? 5 : pegStage + 1;
            if (consumed) break;
        }
    }

    // LFO
    lfoT += dt;
    lfoPhase += (double) (lfoHz * dt);
    if (lfoPhase >= 1.0) {
        lfoPhase -= std::floor (lfoPhase);
        lfoSH = rng.bi();
    }
    const float ph = (float) lfoPhase;
    float b;
    switch (p.lfoWave) {
        case 0: b = ph < 0.25f ? 4.0f * ph : (ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f); break;
        case 1: b = 1.0f - 2.0f * ph; break;
        case 2: b = 2.0f * ph - 1.0f; break;
        case 3: b = ph < 0.5f ? 1.0f : -1.0f; break;
        case 4: b = std::sin (kTwoPi * ph); break;
        default: b = lfoSH; break;
    }
    float env = 1.0f;
    if (lfoT < lfoHold) env = 0.0f;
    else if (lfoFade > 0.0f) env = std::min (1.0f, (lfoT - lfoHold) / lfoFade);
    static const float pms[8] { 0.0f, 10.0f / 255, 20.0f / 255, 33.0f / 255, 55.0f / 255, 92.0f / 255, 153.0f / 255, 1.0f };
    lfoSemis = b * env * ((float) std::min<int> (p.lfoPitchModDepth, 99) / 99.0f) * pms[p.lfoPitchModSens & 7] * 12.0f;
    lfoAmU = (0.5f * (b + 1.0f)) * env * ((float) std::min<int> (p.lfoAmpModDepth, 99) / 99.0f);

    // amplitudes
    const AlgDef& A = kAlgs[algo];
    bool allSilent = true;
    for (int i = 0; i < 6; ++i) {
        OpState& s = ops[(size_t) i];
        const float total = s.opLevel + s.level - 127.0f;
        float target = total <= 0.25f ? 0.0f : std::exp2 (total * kLog2PerUnit - 127.0f * kLog2PerUnit);
        if (s.amSens > 0.0f) target *= std::max (0.0f, 1.0f - s.amSens * lfoAmU);
        s.ampStep = (target - s.amp) * (1.0f / (float) kSubBlock);
        if (A.carriers & (1u << i)) { if (target >= 2e-5f) allSilent = false; }
    }
    silent = allSilent;
    computeIncs (curPitch);
}

void FmVoice::render (float* out, int n, float pitchSemis, float fmAmount) noexcept {
    if (patch == nullptr) { for (int i = 0; i < n; ++i) out[i] = 0.0f; return; }
    const float fm = clampv (fmAmount, 0.0f, 2.0f);
    const float* tab = tables().sine;
    const AlgDef& A = kAlgs[algo];
    if (pitchSemis != curPitch && blockLeft > 0) computeIncs (pitchSemis);
    else curPitch = pitchSemis;

    int done = 0;
    while (done < n) {
        if (blockLeft <= 0) { curPitch = pitchSemis; updateControl(); blockLeft = kSubBlock; }
        const int cnt = std::min (n - done, blockLeft);
        for (int s = 0; s < cnt; ++s) {
            float o[6];
            float acc = 0.0f;
            for (int i = 5; i >= 0; --i) {
                OpState& op = ops[(size_t) i];
                float y = 0.0f;
                if (op.amp != 0.0f || op.ampStep != 0.0f) {
                    float m = 0.0f;
                    const unsigned mask = A.mods[i];
                    if (mask) {
                        for (int j = i + 1; j < 6; ++j) if (mask & (1u << j)) m += o[j];
                        m *= fm;
                    }
                    if (i == A.fbDst) m += fbScale * (fbH0 + fbH1);
                    const uint32_t ph = op.phase + ((uint32_t) (int32_t) (m * 8388608.0f) << 9);
                    y = sineLookup (tab, ph) * op.amp;
                }
                o[i] = y;
                op.phase += op.inc;
                op.amp += op.ampStep;
                if (i == A.fbSrc) { fbH1 = fbH0; fbH0 = y; }
                if (A.carriers & (1u << i)) acc += y;
            }
            out[done + s] = softLimit (acc * carrierScale);
        }
        blockLeft -= cnt;
        done += cnt;
    }
}

} // namespace el::chimera
