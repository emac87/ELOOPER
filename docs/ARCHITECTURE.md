# ELooper architecture (contributor brief)

C++17 + JUCE 8 + Ableton Link. macOS target (Audio Unit hosting, CoreAudio/CoreMIDI); also builds on Linux (no plug-in hosting, ALSA) for CI.

## Layers
| Dir | What | Threads |
|---|---|---|
| `Source/Core` | Pure C++ real-time core, no JUCE: loop state machine (`LoopCore`), chunk pool, lock-free queues (`RtUtil.h`), quantise math (`TimeMath.h`), automation lanes, MIDI clips, graphic EQ, routing graph, normalised param mapping (`ParamMapping.h`). Unit-tested. | audio + message |
| `Source/Model/Session.h` | The session document: tracks (Audio/Instrument/Bus/Master/AuxOut), loops, plugins, sends, timeline, transport settings, presets. | message thread only |
| `Source/Engine` | `AudioEngine` (render callback), `LinkClock`, `EngineController` (THE API for UI), `PluginHost`, `PresetManager`, `SessionSerializer`, `MidiMapping`. | |
| `Source/UI` | JUCE components. Black theme (`UI/Theme.h`), shared `Selection` (`UI/Selection.h`). | message thread |

## Rules
* UI code never touches the audio thread or `*Rt` internals directly except reading UI atomics — **use `EngineController`** for every change and `loopStatus()/transportStatus()/meter()` for display (poll from a `juce::Timer` at `theme::uiRefreshHz`).
* Listen for model changes with `EngineController::Listener::sessionChanged(Change, Id)` and rebuild/refresh only what changed.
* All parameters are normalised 0..1 (`ParamTarget` + `Core/ParamMapping.h` for dB/pan display).
* Ids (`el::Id`) are stable; `kMasterId == 1`.
* `#include <JuceHeader.h>` (resolves to `Source/JuceHeader.h`). Namespace `el`. 4-space indent, JUCE style (`camelCase` methods, `PascalCase` types, space before call parens is optional).
* Prefer `std::unique_ptr` members; no raw `new` without ownership. No `juce::Thread::sleep` on the message thread.

## Signal flow per track
`[MIDI in + MIDI loops] -> MIDI FX -> instrument` or `[audio input x input gain]` -> (monitor gate: Auto mutes live input once a loop is recorded, unless recording/armed) + audio-loop playback -> pre-fader inserts -> graphic EQ -> fader/pan -> post-fader inserts -> output bus (+ sends pre/post fader to any bus; bus->bus allowed, cycles rejected). Multi-out instruments feed AuxOut tracks.

## Loops
Types: Audio, MIDI, Automation. Each loop also has an *automation layer* (`LoopModel::automation`) driving any parameter on its track while the loop plays. Settings per loop (`LoopSettings`: start/stop quantise, record length master/bars/free, after-record play/overdub/stop, one-shot, replace vs take-folder, cycle takes, overdub feedback). Master bars (`TransportSettings::masterBars`) = default record length. Bounce resamples a track's full output into a new track; the original goes to the "Bounced" page.

## Build & check
```
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DELOOPER_JUCE_DIR=/path/JUCE -DELOOPER_LINK_DIR=/path/link
tools/check.sh Source/UI/MyFile.cpp      # fast syntax/type check of single files (parallel-safe)
cmake --build build                       # full build
ctest --test-dir build
```
New `.cpp` files in Source/UI, Source/Engine, Source/Model are globbed automatically (re-run cmake configure after adding files so `compile_commands.json` knows them).
