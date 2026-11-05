#pragma once
// ELooper core types. Pure C++17, no JUCE. Shared by engine, model and UI.
#include <cstdint>
#include <cstddef>

namespace el {

using Id = uint32_t;
constexpr Id kInvalidId = 0;
constexpr Id kMasterId  = 1;   // the master bus always has this id

enum class TrackKind : uint8_t {
    Audio,       // records from an audio-interface input
    Instrument,  // MIDI in -> (MIDI FX) -> software instrument
    Bus,         // fed by sends / track outputs; may record (resample) loops
    Master,      // final mix, feeds hardware outputs
    AuxOut       // extra stereo output pair of a multi-out instrument plugin
};

enum class LoopType : uint8_t { Audio, Midi, Automation };

// Steady state of a loop. Pending (quantised) actions are reported separately.
enum class LoopState : uint8_t { Empty, Recording, Playing, Overdubbing, Stopped };

enum class PendingAction : uint8_t { None, Record, CloseRecord, Play, Stop, Overdub, EndOverdub };

enum class Quantize : uint8_t { None, Beat, Bar, TwoBars, FourBars, Master /* master loop length */ };

enum class RecordLength : uint8_t { Master /* session master bars */, Bars /* LoopSettings::recordBars */, Free };

enum class AfterRecord : uint8_t { Play, Overdub, Stop };

enum class MonitorMode : uint8_t {
    Auto, // hear live input while recording, or while the track has no recorded loop
    On,
    Off
};

enum class PlayMode : uint8_t { Loop, OneShot };

enum class RecordMode : uint8_t {
    Replace,    // re-recording overwrites the active take
    TakeFolder  // every recording pass appends a new take; pick the best one later
};

enum class TransportMode : uint8_t { Loops, Timeline };

// Loop behaviour. Trivially copyable: sent to the audio thread inside commands.
struct LoopSettings {
    Quantize     startQuantize = Quantize::Bar;
    Quantize     stopQuantize  = Quantize::Bar;
    RecordLength lengthMode    = RecordLength::Master;
    int16_t      recordBars    = 4;
    AfterRecord  afterRecord   = AfterRecord::Play;
    PlayMode     playMode      = PlayMode::Loop;
    RecordMode   recordMode    = RecordMode::Replace;
    bool         cycleTakes    = false;   // TakeFolder: keep recording, one take per cycle
    float        overdubFeedback = 1.0f;  // 1 = keep old material fully while overdubbing
};

// What an automation lane / controller mapping drives. Values are always normalised 0..1
// (see ParamMapping.h for the native ranges).
enum class ParamKind : uint8_t {
    Volume, Pan, InputGain, Mute,
    SendLevel,     // objectId = destination bus id
    EqBand,        // index = band 0..kEqBands-1
    PluginParam,   // objectId = plugin slot id, index = parameter index
    PluginBypass,  // objectId = plugin slot id
    LoopVolume     // objectId = loop id
};

struct ParamTarget {
    ParamKind kind = ParamKind::Volume;
    Id        objectId = kInvalidId;
    int32_t   index = -1;
    bool operator== (const ParamTarget& o) const noexcept { return kind == o.kind && objectId == o.objectId && index == o.index; }
    bool operator!= (const ParamTarget& o) const noexcept { return ! (*this == o); }
};

constexpr int kEqBands  = 10;
constexpr int kMaxSends = 8;
constexpr int kMaxTakes = 8;

} // namespace el
