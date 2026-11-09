# Chimera patch JSON schema

Produced by `patchToJson(const Patch&)`, consumed by `patchFromJson(json, Patch&)` (`Source/Synth/dsp/PatchJson.h`).

Rules
- Keys are the C++ field names exactly. Structs are objects; arrays are JSON arrays; enums are integers (lists below); bools are `true`/`false` (numbers are also accepted on input: non-zero = true); `uint8` fields are integers.
- Input is a partial update: missing keys keep their current value; arrays may be shorter (only the given leading entries change); `null`/wrong-typed values are ignored. Every value is clamped to the range shown; integers are rounded. Invalid JSON, or a top-level value that is not an object, returns false and changes nothing.
- Samples and DX7 carts are NOT in the JSON (audio/cart data travel separately). Read-only info keys (marked RO) are emitted but ignored on input.
- Strings: patch/layer/macro names are cut to 256 bytes; `fm.name` keeps printable ASCII, max 10 chars.

## Patch (top level)
| key | type | range / units | meaning |
|---|---|---|---|
| name | string | | patch name |
| category | string | | e.g. "Bass", "Pad" |
| layers | array[4] of Layer | | the four layers |
| globalLfo | array[2] of Lfo | | global LFOs (ModSrc GlobalLfo1/2) |
| macroNames | array[4] of string | | macro labels |
| chorus | Chorus | | master chorus |
| delay | Delay | | master delay |
| reverb | Reverb | | master reverb |
| masterVolume | float | 0..1 | master level (squared for gain) |
| bendRange | float | 0..24 semitones | pitch-bend range |
| sampleNames (RO) | array[4] of string | "" if none | sample file name per layer |
| sampleRoots (RO) | array[4] of int | MIDI note, 60 if none | sample root note per layer |
| cartVoices (RO) | array[4] of array of string | | voice names of the loaded DX7 cart per layer (empty array if none) |
| cartNames (RO) | array[4] of string | | cart file name per layer |

## Layer
| key | type | range / units | meaning |
|---|---|---|---|
| engine | int enum Engine | 0..5 | synthesis engine |
| name | string | | optional label |
| analog | Analog | | Analog engine params |
| wtTable | int | 0..7 | wavetable index (names below) |
| wtPosition | float | 0..1 | wavetable morph position |
| fm | Dx7Patch | | full DX7 voice for the FM engine |
| fmAmount | float | 0..2 | scales modulator depth |
| sampler | Sampler | | Sampler engine params |
| pluck | Pluck | | Pluck engine params |
| filter | int enum FilterType | 0..8 | filter type |
| cutoff | float | 20..20000 Hz | filter cutoff |
| resonance | float | 0..1 | filter resonance |
| drive | float | 0..1 | pre-filter saturation |
| keyTrack | float | 0..1 | cutoff follows note (1 = 100%) |
| filterEnvAmount | float | -1..1 | = -60..+60 semitones of cutoff |
| ampEnv | Env | | amplitude envelope |
| filterEnv | Env | | filter envelope |
| modEnv | Env | | mod envelope |
| useAmpEnv | bool | | apply ampEnv (FM layers often rely on operator EGs instead) |
| lfo | array[2] of Lfo | | layer LFOs (ModSrc Lfo1/Lfo2) |
| level | float | 0..1 | layer level (squared for gain) |
| pan | float | -1..1 | pan |
| transpose | int | -48..48 semitones | |
| fine | float | -100..100 cents | |
| keyLo, keyHi | int | 0..127 | key zone |
| velLo, velHi | int | 0..127 | velocity zone |
| velocitySens | float | 0..1 | velocity sensitivity |
| unison | int | 1..7 | unison voices |
| unisonDetune | float | 0..1 | detune spread (1 = 50 cents) |
| unisonSpread | float | 0..1 | stereo width of unison voices |
| glide | float | 0..10 s | portamento time |
| mode | int enum VoiceMode | 0..2 | |
| polyphony | int | 1..32 | voices for this layer |
| output | int | 0..3 | stereo output pair (only 0 goes through FX) |
| mods | array[8] of ModSlot | | modulation matrix |

## Env
| key | type | range | meaning |
|---|---|---|---|
| attack, decay, release | float | 0..10 s | stage times |
| sustain | float | 0..1 | sustain level |

## Lfo
| key | type | range | meaning |
|---|---|---|---|
| shape | int enum LfoShape | 0..6 | |
| rateHz | float | 0.01..40 | rate when not synced |
| sync | bool | | tempo-synced |
| beats | float | 0.0625..16 | beats per cycle when synced |
| retrigger | bool | | restart on note (layer LFOs) |
| fadeIn | float | 0..10 s | fade-in after note on (layer LFOs) |

## ModSlot
| key | type | range | meaning |
|---|---|---|---|
| src | int enum ModSrc | 0..17 | source |
| dst | int enum ModDst | 0..17 | destination |
| amount | float | -1..1 | depth |

## Analog
| key | type | range | meaning |
|---|---|---|---|
| wave1, wave2 | int enum Wave | 0..4 | oscillator waveforms |
| pw1, pw2 | float | 0.05..0.95 | pulse width (Square) |
| osc2Semi | float | -24..24 | osc 2 pitch, semitones |
| osc2Fine | float | -100..100 | osc 2 pitch, cents |
| mix | float | 0..1 | 0 = osc 1, 1 = osc 2 |
| sub | float | 0..1 | sub-oscillator level |
| noise | float | 0..1 | noise level |
| sync | bool | | osc 2 hard-synced to osc 1 |

## Sampler
| key | type | range | meaning |
|---|---|---|---|
| start | float | 0..1 | start point (fraction of length) |
| loop | int enum LoopMode | 0..2 | |
| loopStart, loopEnd | float | 0..1 | loop points (fraction of length) |
| reverse | bool | | play backwards |

## Pluck
| key | type | range | meaning |
|---|---|---|---|
| decay | float | 0..1 | ring time (~0.2..10 s) |
| damping | float | 0..1 | 0 bright, 1 dark |
| pickPos | float | 0..0.5 | pluck position along string |
| noiseColor | float | 0..1 | 0 dark excitation, 1 bright |

## Dx7Patch (`layer.fm`)
`op` is an array of 6 operator objects. **Index 0 = OP1, index 5 = OP6** (display order of the DX7 panel; SysEx stores OP6 first but the codec/engine already converted).

| key | type | range | meaning |
|---|---|---|---|
| op | array[6] of Dx7Op | | operators, op[0] = OP1 |
| pitchEgRate | array[4] of int | 0..99 | pitch EG rates R1..R4 |
| pitchEgLevel | array[4] of int | 0..99 | pitch EG levels L1..L4 (50 = no shift) |
| algorithm | int | 0..31 | algorithm number minus 1 (0 = ALG 1) |
| feedback | int | 0..7 | |
| oscKeySync | int | 0..1 | oscillator key sync |
| lfoSpeed, lfoDelay | int | 0..99 | |
| lfoPitchModDepth, lfoAmpModDepth | int | 0..99 | |
| lfoKeySync | int | 0..1 | |
| lfoWave | int | 0..5 | 0 triangle, 1 saw down, 2 saw up, 3 square, 4 sine, 5 sample&hold |
| lfoPitchModSens | int | 0..7 | |
| transpose | int | 0..48 | 24 = no transpose (C3 centre) |
| name | string | max 10 chars | voice name |

## Dx7Op
| key | type | range | meaning |
|---|---|---|---|
| egRate | array[4] of int | 0..99 | EG rates R1..R4 |
| egLevel | array[4] of int | 0..99 | EG levels L1..L4 |
| kbdBreakPoint | int | 0..99 | keyboard-scaling break point (39 = C3) |
| kbdLeftDepth, kbdRightDepth | int | 0..99 | scaling depth |
| kbdLeftCurve, kbdRightCurve | int | 0..3 | 0 -LIN, 1 -EXP, 2 +EXP, 3 +LIN |
| kbdRateScaling | int | 0..7 | |
| ampModSens | int | 0..3 | |
| keyVelSens | int | 0..7 | |
| outputLevel | int | 0..99 | |
| oscMode | int | 0..1 | 0 ratio, 1 fixed |
| freqCoarse | int | 0..31 | |
| freqFine | int | 0..99 | |
| detune | int | 0..14 | 7 = centre |

## Chorus
`on` bool; `rate` float 0.05..5 Hz; `depth` float 0..1; `mix` float 0..1.

## Delay
`on` bool; `sync` bool (time in beats vs ms); `beats` float 0.0625..4; `ms` float 1..2000; `feedback` float 0..0.95; `mix` float 0..1; `tone` float 0..1 (0 dark, 1 bright); `pingPong` bool.

## Reverb
`on` bool; `size` 0..1; `damping` 0..1; `mix` 0..1; `predelayMs` 0..200 ms; `width` 0..1.

## Enums (integer value = position)
- **Engine**: 0 Off, 1 Analog, 2 Wavetable, 3 FM, 4 Sampler, 5 Pluck
- **FilterType**: 0 Off, 1 LP 12, 2 LP 24, 3 HP 12, 4 HP 24, 5 BP, 6 Notch, 7 Ladder, 8 Comb
- **LfoShape**: 0 Sine, 1 Triangle, 2 Saw Up, 3 Saw Down, 4 Square, 5 Sample&Hold, 6 Smooth Random
- **VoiceMode**: 0 Poly, 1 Mono, 2 Legato
- **Wave**: 0 Sine, 1 Triangle, 2 Saw, 3 Square, 4 Noise
- **LoopMode**: 0 Off, 1 Forward, 2 PingPong
- **ModSrc**: 0 None, 1 Lfo1, 2 Lfo2, 3 GlobalLfo1, 4 GlobalLfo2, 5 AmpEnv, 6 FilterEnv, 7 ModEnv, 8 Velocity, 9 KeyTrack, 10 ModWheel, 11 Aftertouch, 12 PitchBend, 13 Random, 14 Macro1, 15 Macro2, 16 Macro3, 17 Macro4
- **ModDst**: 0 None, 1 Pitch, 2 Cutoff, 3 Resonance, 4 Drive, 5 Level, 6 Pan, 7 PulseWidth, 8 OscMix, 9 Osc2Pitch, 10 Noise, 11 WavePosition, 12 FmAmount, 13 SampleStart, 14 PluckDamping, 15 Lfo1Rate, 16 Lfo2Rate, 17 FilterEnvAmount
- **Wavetable `wtTable`**: 0 Basic Shapes, 1 PWM, 2 Harmonics, 3 Formant, 4 Digital, 5 Organ, 6 Bell, 7 Sync Sweep

## Example (abridged; real output has all 4 layers and every field)
```json
{"name":"Init","category":"","layers":[{"engine":1,"name":"","analog":{"wave1":2,"wave2":2,"pw1":0.5,"pw2":0.5,"osc2Semi":0,"osc2Fine":0,"mix":0,"sub":0,"noise":0,"sync":false},
 "wtTable":0,"wtPosition":0,"fm":{"op":[{"egRate":[99,99,99,99],"egLevel":[99,99,99,0],"outputLevel":0,"freqCoarse":1,"...":"..."},"... 5 more"],"algorithm":0,"name":"INIT VOICE","...":"..."},
 "cutoff":12000,"resonance":0.15,"ampEnv":{"attack":0.003,"decay":0.4,"sustain":0.8,"release":0.25},"lfo":[{},{}],"mods":[{"src":0,"dst":0,"amount":0},"... 7 more"],"...":"..."},"... 3 more"],
 "globalLfo":[{},{}],"macroNames":["Macro 1","Macro 2","Macro 3","Macro 4"],"chorus":{},"delay":{},"reverb":{},"masterVolume":0.7,"bendRange":2,
 "sampleNames":["","","",""],"sampleRoots":[60,60,60,60],"cartVoices":[[],[],[],[]],"cartNames":["","","",""]}
```
Partial update example: `{"masterVolume":0.5,"layers":[{"cutoff":800,"fm":{"op":[{"outputLevel":80}]}}]}` changes only those three values.
