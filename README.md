# ELooper

ELooper is a lightweight, low-latency looper and DAW for macOS. It records audio and MIDI into loops that start and stop on the beat, layers overdubs, and can automate anything on its mixer or on a plug-in. Its mixer has input gain, inserts, a 10-band EQ, sends and buses, and it hosts Audio Unit and VST3 instruments and effects. Tempo and phase can be shared with other apps over Ableton Link. The application is written in C++17 on JUCE 8.0.4, with the real-time core kept free of JUCE so it can be unit-tested on its own. For day-to-day use, see the [User Guide](docs/USER_GUIDE.md). For code structure, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Quick start (macOS)

```bash
xcode-select --install          # once, if you don't have the command line tools
brew install cmake ninja        # once
tools/build-mac.sh --open       # builds, runs the tests and launches ELooper.app
```
The first build downloads JUCE and Ableton Link and takes a few minutes. On first launch macOS asks for microphone access, which is needed to record from your audio interface. Then open Settings (Cmd+,) to pick your interface and buffer size.

## Features

**Loops**
- Audio, MIDI and automation loops. Every loop has an automation layer that can drive any parameter on its track while the loop plays.
- Quantised start and stop for each loop: None, Beat, Bar, 2 Bars, 4 Bars, or the master loop length.
- Master bars set the default record length. A loop can instead record a fixed number of bars, or record freely.
- After-record behaviour (play, overdub or stop), and playback as a loop or a one-shot.
- Take folders: keep every recording pass as a take, or cycle through takes while recording. Choose or delete takes at any time.
- Auto monitoring: the live input is heard until a loop has been recorded, then it is muted so the loop and the input do not double up.
- Bounce a track, with all of its effects, into a new audio track. The original moves to a Bounced page and can be restored.

**Mixer and routing**
- Per-track input gain, pre-fader and post-fader insert chains, a 10-band graphic EQ, fader and pan.
- Sends to buses, either pre or post fader. Buses can send to other buses. Any routing that would create a feedback cycle is refused.
- Track outputs go to Master or to a bus. Master goes to a hardware output pair.
- Multi-output instruments: create aux tracks for the extra stereo pairs of an instrument.

**Plug-ins and presets**
- Audio Unit and VST3 instruments, effects and MIDI effects.
- Plug-in presets, and channel presets that capture a track's levels, EQ, monitoring and plug-in chain.

**Timing and sync**
- Ableton Link tempo and phase sync, with optional start/stop sync.
- Metronome, tap tempo and time signature settings.
- Latency-compensated recording, with a manual offset in samples for fine-tuning.

**Timeline**
- A timeline page for arrangement. Place loops as clips, set a loop region, and use arrangement record to write the loops you launch into the timeline.

**MIDI editing**
- Piano roll and step sequencer for MIDI loops, with undo and redo.

**Control**
- MIDI controller learn for Play/Stop, Record selected loop, Stop All and Tap Tempo, plus per-loop Trigger, Record and Stop.

**Projects**
- Projects are saved as `.elooper` folders holding the session file and every recorded audio take.

## Building (macOS)

Requirements:
- macOS 11 or later
- Xcode Command Line Tools (`xcode-select --install`)
- CMake 3.22 or later

With the Xcode generator:

```sh
cmake -S . -B build -G Xcode
cmake --build build --config Release
```

With Ninja (`brew install ninja`):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The app is written to `build/ELooper_artefacts/Release/ELooper.app`. By default the app is built as a universal binary (arm64 and x86_64).

CMake downloads JUCE 8.0.4 and the Ableton Link SDK when it configures, so the first configure needs network access. To use local checkouts instead, pass their paths:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DELOOPER_JUCE_DIR=/path/to/JUCE \
      -DELOOPER_LINK_DIR=/path/to/link
```

Other options:
- `-DELOOPER_BUILD_TESTS=OFF` skips the test executables.
- `-DELOOPER_BUILD_APP=OFF` builds only the pure C++ core library.

## Running the tests

```sh
ctest --test-dir build --output-on-failure
```

With the Xcode generator, add the configuration: `ctest --test-dir build -C Release --output-on-failure`.

There are two test suites:
- `core` runs the unit tests for the real-time core, which has no JUCE dependency.
- `engine` renders the real audio engine offline, with no audio device.

## Known limitations

- **VST2 is not supported.** The VST2 SDK is no longer distributable, so plug-ins are hosted as Audio Unit and VST3 only.
- **No time-stretching.** Audio loops follow tempo changes by varispeed, which also changes their pitch.
- **Plug-in delay compensation is not implemented.** Plug-in latency is measured and reported, but it is not compensated.
- **Automation is applied per audio block**, so it is block-rate rather than sample-accurate. Internal mixer parameters are smoothed.
- **Linux builds exist for CI only.** The Linux build checks that the code compiles and the tests pass. It is not a supported way to run ELooper.

## Licensing

ELooper depends on two libraries with their own licences:
- **JUCE** is available under the AGPLv3 or a commercial licence.
- **Ableton Link** is available under the GPLv2+ or a commercial licence.

Personal use of ELooper is fine. Distributing a build requires complying with the licences of both libraries. This is not legal advice; check the JUCE and Ableton Link licence terms before you distribute anything.
