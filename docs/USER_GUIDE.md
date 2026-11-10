# ELooper User Guide

This guide covers day-to-day use of ELooper on macOS. For building the app, see the [README](../README.md). Keyboard shortcuts use `⌘` for the Command key.

## Contents

1. [First launch](#first-launch)
2. [The main screen](#the-main-screen)
3. [Tracks](#tracks)
4. [Recording a first loop](#recording-a-first-loop)
5. [Loop states and colours](#loop-states-and-colours)
6. [Loop controls and the right-click menu](#loop-controls-and-the-right-click-menu)
7. [Take folders](#take-folders)
8. [MIDI loops](#midi-loops)
9. [Automation](#automation)
10. [Mixer and routing](#mixer-and-routing)
11. [Plug-ins and presets](#plug-ins-and-presets)
12. [Chimera synth](#chimera--built-in-synth)
13. [Chimera outside ELooper](#chimera-outside-elooper)
14. [Bouncing a track](#bouncing-a-track)
15. [Timeline and arrangement record](#timeline-and-arrangement-record)
16. [Ableton Link](#ableton-link)
17. [MIDI controllers](#midi-controllers)
18. [Keyboard shortcuts](#keyboard-shortcuts)
19. [Saving and opening projects](#saving-and-opening-projects)

## First launch

Open **Edit > Settings** (`⌘,`) and go to the **Audio** tab. Choose your audio interface, then set:

- **Sample rate**: the rate your interface and your other software use.
- **Buffer size**: smaller buffers give lower latency. For live looping, 64 to 128 samples is a good range. Raise the buffer if you hear dropouts or crackles, especially with plug-ins.

The Audio tab also shows the measured round-trip latency. ELooper shifts recorded loops earlier by that amount so they line up with what you heard. If a recording still sounds early or late, use **Recording latency offset (samples)** to fine-tune it. The offset can be negative.

Before you record, set the **Input** of an audio track (see [Tracks](#tracks)) and check that the input meter moves.

## The main screen

The **transport bar** across the top holds:

- **Play / Stop** button (`Space`).
- **Tempo**, in BPM. Drag up or down to change it (hold Shift for fine steps), or double-click to type a value.
- **Tap** for tap tempo.
- **Time signature** and **Master bars**. Master bars is the default record length for new loops. Choose 1, 2, 4, 8, 16 or 32, or **Custom...** for 1 to 256 bars.
- **Metronome** toggle.
- **Link** toggle, with the number of Link peers, and a start/stop sync toggle. See [Ableton Link](#ableton-link).
- **Arrangement record** toggle. See [Timeline and arrangement record](#timeline-and-arrangement-record).
- **Pages**: **Loops**, **Timeline** and **Bounced**.
- **Docks**: **Mixer** and **Editor** (the MIDI editor) open below the page; drag the bar between the page and a dock to resize it. **Automation** opens the automation layer on top of everything (see [Automation](#automation)).
- **Settings** and a CPU readout.

On a narrow window, the transport bar moves **Tap**, **Link**, **Sync** and **Arr Rec** into a **•••** menu, and at very narrow widths it moves **Metro** there too. The dock buttons shorten to **Mix**, **Auto** and **Edit**. Stopping the transport sends note-off and sustain-off messages, so no notes hang.

The **Loops** page is the main screen. It has no mixer controls, only loops. Each track is a column: its label is pinned along the bottom of the screen and its loops are stacked above it, loop 1 at the top, with **+** under the last one. Click a cell to select it. **+ Track** at the end of the labels adds a track. Plain buses are shown on the mixer only (a bus that holds loops still appears as a column). All levels, effects, sends and routing are on the **Mixer** dock. The labels and scene buttons scroll together with the loops.

**Scenes:** the **Scene** buttons run down the left side, one per row of loops. **Scene 1** plays the top row (the first loop of every track) and stops every other loop. **Scene 2** does the same with the second row, and so on. Each loop starts and stops on its own quantise setting (the next bar by default). A track whose slot in that row is empty goes silent. A loop that is recording is left alone. The scene button lights up while its row is the one playing. **Stop all** sits in the bottom-left corner, beside the labels. Right-click a Scene button or **Stop all** for **MIDI Learn**.

**Scenes menu and keys:** the **Scenes** menu lists **Launch Scene 1** to **Launch Scene 9** and **Stop All Loops**. The number keys do the same: `1` to `9` launch scenes 1 to 9, and `0` stops all loops. The keys are ignored while you type in a text field, so a name or a tempo entry never starts a scene by accident.

## Tracks

There are five kinds of track:

| Kind | What it does |
|---|---|
| **Audio** | Records from an audio input. |
| **Instrument** | Takes MIDI input and plays a software instrument: an Audio Unit, such as the [Chimera](#chimera--built-in-synth) synth. Its MIDI loops record that input. |
| **Bus** | Receives sends and track outputs. It can record what it receives. |
| **Master** | The final mix. It goes to the hardware output pair. Every session has one. |
| **AuxOut** | An extra stereo output of a multi-output instrument. See [Mixer and routing](#mixer-and-routing). |

A new session starts with one audio track and one instrument track.

**Adding tracks:** click **+ Add Track** below the last track and choose Audio, Instrument or Bus. You can also use the **Track** menu (Add Audio Track, Add Instrument Track, Add Bus).

**Adding loops:** each track row ends with a **+** button. It adds an audio loop (MIDI loop on instrument tracks). Automation loops are hidden for now; automation will come back in a different form.

**Track label:** the label under each column shows the track's colour, its type icon (synth, drums, sampler, EQ and so on, or the channel type), its name and its input (or instrument). Click the icon to show or hide the track's plug-in window (right-click it to pick any plug-in on the track; on an empty instrument track it opens the instrument menu). Click the name to rename the track. Click anywhere else on the label to select the track, right-click anywhere else for the track menu, and drag sideways to reorder tracks. A multi-out instrument has a **Show / Hide outs** button; its outputs and Sum folder are the columns right after it.

**Track right-click menu** (for Audio, Instrument and Bus tracks where listed):

- **Rename...** and **Colour**.
- **Input**: None, a single input, or a stereo pair (audio tracks only).
- **MIDI Input**: the MIDI device (or All Devices), and the MIDI channel (Omni or 1 to 16). Instrument tracks only.
- **Instrument...**: choose the instrument plug-in. Instrument tracks only.
- **Monitor**: Auto, On or Off.
- **Output**: Master, or a bus. Not available on Master.
- **Channel Presets...**
- **Multi-Out: Create Outputs + Sum Folder**: instrument tracks whose instrument has more than one stereo output. See [Mixer and routing](#mixer-and-routing).
- **Bounce Track** and **Remove Track**. Not available on Master.

## Recording a first loop

1. **Set the master bars.** In the transport bar, pick a value such as 4. New loops use this as their record length.
2. **Set an input.** Right-click an audio track's header, choose **Input**, and pick the interface input your instrument or microphone is on.
3. **Start the loop.** Click an empty cell. The transport starts if it is stopped. Loops start and stop on the quantise point (Bar by default), so the cell blinks yellow until recording begins on that bar.
4. **Play.** The cell turns red while it records. Recording ends after the master bars, and the loop then plays (After Record defaults to Play). You can also click the cell again to close the recording early, on the next quantise point.
5. **Overdub.** Shift-click a playing loop to overdub on top of it. The cell turns orange while overdubbing. Shift-click again to stop overdubbing.

Once a loop has been recorded, an audio track in Auto monitor mutes its live input, so you hear the loop and not the input on top of it. Arm the track, or set Monitor to On, to hear the input again.

## Loop states and colours

| Appearance | Meaning |
|---|---|
| Dim, no fill | Empty. No material has been recorded. |
| Red | Recording. |
| Orange | Overdubbing. |
| The loop's colour | Playing. Loops use their own colour, or their track's colour if they have none. |
| Dimmed, muted colour | Stopped. |
| Blinking yellow | A change is waiting for the next quantise point, such as a loop that will start, stop or record on the next bar. |

## Loop controls and the right-click menu

**Mouse:**

- **Click**: trigger the loop. An empty loop records, a recording loop closes, a playing loop stops, and a stopped loop plays. Clicking also selects the loop. Every click acts, so you can re-tap a loop while it runs to stop or restart it live.
- **Shift-click**: overdub. Shift-click again to stop overdubbing.
- **Cmd-click**: open the MIDI editor for a MIDI loop. On an audio loop it does nothing.
- **Double-click**: no special action. The first click of a double-click has already triggered the loop.
- **Right-click**: open the loop menu.

**Right-click menu:**

- **Record**, **Play**, **Stop**, **Overdub** and **Clear** (Clear asks for confirmation). Play, Stop, Overdub and Clear are disabled on an empty loop.
- **Start Quantize** and **Stop Quantize**: None, Beat, Bar, 2 Bars, 4 Bars or Master.
- **Record Length**: Master, 1, 2, 4, 8 or 16 bars, or Free.
- **After Record**: Play, Overdub or Stop.
- **Play Mode**: Loop or One Shot.
- **Tempo Changes**: Keep Pitch (time-stretch, the default) or Change Pitch (varispeed, like speeding up tape). Audio loops only.
- **Record Mode**: Replace, Take Folder, or Take Folder + Cycle.
- **Overdub Feedback**: 100%, 90%, 75% or 50%. Lower values let the new layer replace more of the old material.
- **Takes**: select a take, or delete one.
- **Colour** and **Rename...**
- **Duplicate**: MIDI loops only.
- **Add to Timeline at Playhead**
- **Edit MIDI**: MIDI loops only.
- **Edit Automation**
- **MIDI Learn**: Trigger, Record or Stop. See [MIDI controllers](#midi-controllers).
- **Remove Loop** (asks for confirmation).

The **Loops** section of **Settings** sets the defaults for new loops. Changing them does not change existing loops.

## Take folders

A take folder keeps several recordings of the same loop, so you can pick the best one later.

1. Right-click the loop and choose **Record Mode > Take Folder**.
2. Record. Each recording pass is added as a new take. A loop can hold up to eight takes.
3. To keep recording continuously, choose **Record Mode > Take Folder + Cycle** instead. Each cycle of the loop becomes its own take.
4. To pick a take, right-click the loop and choose **Takes > Take N**. To remove a take you do not want, choose **Takes > Delete Take N**.

In **Replace** mode, which is the default, re-recording overwrites the active take.

## MIDI loops

Add a MIDI loop with **+M** on an instrument track, or from the track's **+** menu. Its clips record the MIDI that arrives at the instrument track.

To edit a MIDI loop, Cmd-click it, or right-click and choose **Edit MIDI**. The editor opens in the **Editor** dock (`E`). It has two tabs:

**Length** (both views): **-** and **+** change the loop by one bar. **x2** doubles the loop and repeats the pattern into the new bars (2 bars become 4, with the same notes in bars 3-4). **/2** halves it and keeps the first half (notes that ran past the new end are shortened). Both can be undone with `⌘Z`.

**Piano Roll** shows notes on a time and pitch grid.

- Grid: 1/4, 1/8, 1/16 or 1/32, with **triplet** and **snap** toggles, and a **Quantize** button.
- Velocity for new notes is set with the **Vel** control. Zoom in and out with the zoom buttons.
- Click empty space to add a note. Drag a note to move it, and drag its right edge to change its length. Drag on empty space to select several notes. Double-click a note to delete it.
- Keys: `⌘A` selects all notes, `⌘D` duplicates the selection, `Q` quantises the selection, `Delete` or `Backspace` removes it, and the arrow keys move it. Shift with up or down moves by an octave.
- Right-click a note for **Delete**, **Mute / Unmute** and **Velocity**.

**Step Seq** is a step sequencer. Each lane (row) is a drum sound or a note, and each column is a step. Click or drag across cells to turn steps on and off. Each lane header has a mute button and a remove button. **+ Add lane** at the bottom adds a lane from the **Drums (GM)** list or any note.

- **Velocity and ratchet per step:** right-drag a step up or down to set its velocity, or right-click it for the **Velocity** and **Ratchet** menus. A ratchet re-triggers the step 2-8 times inside its length (drawn as dividers in the cell). The bar along the bottom of a step shows its velocity. **Ratchet** at the top left sets the ratchet for new steps; **Vel** in the header sets their velocity.
- **Lane menu:** right-click a lane name for **Change note**, **Rename lane...** (or double-click the name), **Velocity (all steps)** (set, ramp up/down, accent on the beat, humanize), **Ratchet (all steps)**, **Fill** (every 1/2/4/8 steps), **Shift** left/right, **Copy / Paste / Clear lane**, **Move up / down**, mute and remove.
- **Lanes menu** (top left): load a lane preset. **Drum maps** has presets named for Baby Audio Tekno (all 18 voices on C1-F2), Kontakt kits (Eloy Drums, Krimh / Trivium, Extinction Level Event, GetGood, Studio Drummer), Superior Drummer, EZdrummer, Addictive Drums, 16 chromatic pads (Battery, Drum Rack, MPC, Maschine), Logic's drum kits and General MIDI. The acoustic-kit presets use the GM note layout those libraries accept for the core kit; if a lane stays silent, set the plug-in's MIDI map to GM or use **Change note** on that lane. **Scales** builds lanes for one octave of a scale (major, minor, modes, pentatonics, blues, harmonic and melodic minor, Phrygian dominant, Hungarian minor and more) from any root. **My presets** saves your current lanes (names and notes), optionally with the pattern, and loads them back; they are stored in Application Support/ELooper/StepPresets. **Transpose all lanes**, **Reset lanes to automatic** and **Clear all steps** are there too.
- The lane layout is saved with the loop.

Undo and redo work in both tabs: `⌘Z` for undo, and `Shift`+`⌘Z` or `⌘Y` for redo.

## Automation

Automation records and plays back parameter changes, such as a fader or a plug-in knob, along with the loop.

**Each loop has an automation layer.** Automation runs while the loop plays. Automation-only loops are hidden for now; loops of that kind in older projects are not shown on the Loops page.

**The automation layer:** click **Automation** at the top (or press `A`). It opens on top of whatever is on screen (the loops, the MIDI editor or the mixer) and is see-through, so you can still see what's underneath. Clicks pass through to the loops and mixer underneath, except on the overlay's top bar and lane rows. It shows the selected loop's lanes; select a different loop to switch. **Close** (or the **Automation** button again) hides it. Its controls:

- **Loop layer / Track lanes**: the selected loop's automation, or the track's own lanes (used on the timeline).
- **Pencil**: drag to draw points freehand. Without it, click to add a point and drag points to move them (Shift-drag selects several, Alt-drag bends a segment; double-click or right-click a point to delete it).
- **Eraser**: drag across a lane to remove every point in that span.
- **Write** (record arm, red when on): while on, moving any control on the loop's channel (fader, pan, EQ band, send or plug-in parameter) writes it into the loop's automation as the loop plays. Recording or overdubbing a loop always writes, with or without Write.
- **+ Lane**: add a lane; the menu groups parameters into Mixer, EQ, Sends and each plug-in.
- Each lane has **On** (enable), **Clear** (empty it) and **X** (remove it).

## Mixer and routing

Open the **Mixer** dock (`M`) and drag the bar above it to make it taller. Hide it again with the same button and the Loops page shows only loops. The strips are in the same order as the loop rows (a multi-out instrument's outputs and Sum folder sit right after it), then the buses, with Master pinned on the right. Each strip, from top to bottom:

- **Header:** the colour stripe, the plug-in icon (click: show or hide the track's main plug-in; right-click: any plug-in on the track), the name and the input. Click to select, double-click the name to rename, right-click for the track menu.
- **M** (mute), **S** (solo), the red **arm** button (an armed track forces its input to be heard) and **Monitor** (cycles Auto, On and Off; in Auto the live input is heard until a loop is recorded).
- Slots, Logic style: **Instrument** (instrument tracks), **MIDI FX**, **Audio FX**, **EQ** (click the thumbnail to edit the 10-band EQ, **On** toggles it), **Sends** and **Post FX**. Every list ends with an empty dashed slot: click it to add.
- **In** (input gain) and **Pan** bars, the **Vol** fader with a stereo meter beside it, and the **Output** slot.

**Bars and the fader:** drag left/right or up/down to change them (they never jump to where you click). Hold Shift for fine changes and double-click to reset (0 dB, or centre for pan). The mouse wheel scrolls and never changes a level.

**Plug-in slots:** click a plug-in to show or hide its window. Click the dot on an effect to bypass it (grey dot = bypassed). Right-click it for **Show/Hide window**, **Bypass**, **Replace...**, **Presets...**, **Move up/down**, **Move after/before the fader** and **No Plug-in (remove)**.

**Choosing a plug-in:** the menu lists **Recent** plug-ins first, then a submenu per type (for instruments: Synthesizers, Drum Machines & Kits, Samplers, Keyboards, Bass, Strings/Pads, Organs; for effects: EQ & Filter, Dynamics, Reverb, Delay, Modulation, Distortion & Amps, Other), then **Audio Units** with every plug-in by manufacturer, then **Scan for Plug-ins...**. The type is a best guess from the plug-in's name and category.

**Output slot:** shows where the track goes: **Stereo Out** (Master) or a bus. Click it for **Stereo Out (Master)** or **Bus** > any bus or **New Bus**. On Master it shows the hardware output pair, such as `Out 1-2`.

**Sends:** a send sends part of a track to a bus. Click the empty **+ Send** slot and choose a bus or **New Bus**. Each send is a bar with the bus name and level: drag it to set the level, right-click it for **Pre-fader** / **Post-fader** or **Remove send**.

**Buses:** add one from the **Track** menu, **+ Add Track**, or **New Bus** in an Output or Send menu. Buses are shown on the mixer only. A track or bus can send to Master or to any other bus. ELooper refuses an output or send that would create a feedback cycle, and shows an alert.

**Multi-output instruments:** an instrument can produce more than one stereo pair (for example one per drum in a kit). Right-click the instrument track and choose **Multi-Out: Create Outputs + Sum Folder**. This adds one AuxOut track for each extra pair (labelled with its outputs, such as **Output 3/4**) and a **Sum** folder channel. The instrument's main output and every AuxOut feed the Sum channel, and the Sum channel goes where the instrument used to go, so one fader controls the whole kit while each output keeps its own volume, pan, effects and sends. On the Loops page the outputs and the Sum channel sit indented under the instrument. Click **Hide / Show outputs** on the instrument's label to fold them away or show them again. Choosing the menu item again only adds what is missing. Removing the instrument track removes its outputs and its Sum folder.

## Plug-ins and presets

**Scanning:** open **ELooper > Settings > Plug-ins** and click **Scan for Plug-ins**. The plug-in menus are built from the scan, so scan again after you install a new plug-in. See **Choosing a plug-in** in [Mixer and routing](#mixer-and-routing) for how the menus are organised.

**Inserting:** click an empty slot on the mixer strip, or choose **Instrument...** from an instrument track's menu. Click the plug-in to open its window.

**Tempo and position:** hosted plug-ins receive the session tempo and song position, so tempo-synced effects and instruments follow ELooper.

**Plug-in presets:** from a plug-in slot, choose **Presets...**. Choose **Save Preset...** to save the plug-in's current state, or pick a saved preset to load it.

**Channel presets:** a channel preset captures a track's input gain, volume, pan, EQ, monitoring, MIDI effects, instrument, and pre- and post-fader inserts. Right-click the track label (or its mixer strip header) and choose **Channel Presets...**, or use the **Track** menu.

Plug-in and channel presets are stored in your Application Support folder, not in the project, so they are available to every project.

## Chimera — built-in synth

Chimera is a multi-layer synthesizer made alongside ELooper. It is no longer built into ELooper: install the Chimera Audio Unit (see [Chimera outside ELooper](#chimera-outside-elooper)), choose **Scan for Plug-ins...**, then pick it from the instrument menu under **AU Instruments > ELooper**. Projects saved with the old built-in Chimera load the Audio Unit with the same sound once it has been scanned. Up to four layers can sound at once, each with its own engine, filter, envelopes, LFOs and modulation.

**Adding Chimera**

1. Add an instrument track: **+ Add Track** > **Instrument**, or **Track > Add Instrument Track**.
2. Load Chimera: on the mixer strip, click **+ Instrument** and choose **AU Instruments > ELooper > Chimera**. You can also right-click the track label and choose **Instrument...**.
3. Click the **Chimera** chip on the strip to show or hide its window.

In the instrument menu, **Random instrument** loads a random instrument from every scanned Audio Unit instrument.

**Top bar**

- **Preset browser:** **<** and **>** step through the factory presets. The preset menu lists them by category, then your **User presets**.
- **Dice:** click for a random preset, factory or user, never the same one twice. Shift-click builds a brand-new random patch named **Random NNNN**: one to three layers, with engines chosen by weight, and Macro 1 set to Brightness. Save it with **Save as** if you like it.
- **Save, Save as, Load:** **Save** overwrites the current user preset, or asks for a name the first time. **Save as** always asks for a name. **Load** opens a `.chimera` file.
- **Macros:** four macros, named by each preset. Right-click one to rename it (up to 24 characters). Macros are modulation sources and can be automated.
- **Master:** the output volume, 70% by default.
- **Voices and meter:** the number of sounding voices, and the output level.

User presets are saved as `.chimera` files in the **ELooper > Chimera Presets** folder in Application Support. Choose **Show preset folder** from the preset menu to open it.

**Layers**

The four numbered tabs select the layer you are editing. Each tab shows the layer's engine (or its custom name) and a power dot. Click the dot to turn a layer off or on again. Turning it on restores the last engine used. A new Chimera has layer 1 on Analog and layers 2 to 4 off. An off layer shows five engine buttons: Analog, Wavetable, FM, Sampler and Pluck.

Drop a `.syx` file on the window to load it as a DX7 cartridge into the selected layer. Drop an audio file to load it into the Sampler.

The **Voice** column on the left holds the layer's general settings:

- **Level** and **Pan**, both automatable.
- **Transpose** (±48 semitones) and **Fine** (±100 cents).
- **Mode:** **Poly**, **Mono** or **Legato**. Mono and Legato play one note at a time per layer, and the most recent note takes priority.
- **Polyphony:** 1 to 32 voices for the layer. All layers share 64 voices in total.
- **Glide:** portamento time, from 0 to 5 seconds.
- **Unison:** 1 to 7 voices, with **Detune** (up to 50 cents of spread) and **Spread** (stereo width).
- **Vel sens:** how much note velocity changes the layer's loudness.
- **Key lo / Key hi** and **Vel lo / Vel hi:** the keys and velocities the layer responds to. Give two layers different key ranges to make a split, or the same full range to layer them.
- **Output:** **Main**, **Out 3-4**, **Out 5-6** or **Out 7-8**. Only the Main pair goes through the Chimera effects. Use **Multi-Out** on the track menu to give the other pairs their own tracks.
- **Copy**, **Paste** and **Init:** Copy takes the whole layer, including its loaded sample or cartridge, and Paste puts it on the selected layer. Init resets the layer to a plain analog sound.

**Engines**

Each layer has one engine. Its controls are on the **Engine** panel.

**Analog**

- Two oscillators, each a sine, triangle, saw, square or noise wave. Square pulse width is adjustable from 5% to 95%.
- Osc 2 has **Semitones** (±24) and **Fine** (±100 cents).
- **Mix** crossfades from osc 1 (left) to osc 2 (right).
- **Sub** adds a square wave one octave below osc 1. **Noise** adds white noise.
- **Hard sync to osc 1** resets osc 2 each time osc 1 completes a cycle.

**Wavetable**

- Eight built-in tables: Basic Shapes, PWM, Harmonics, Formant, Digital, Organ, Bell and Sync Sweep. Each table morphs through 16 frames.
- **Position** scans through the frames. Drag the frame view to change it.

**FM**

- A six-operator, DX7-compatible engine with 32 algorithms. Pick the algorithm with its control (1 to 32), and the diagram shows how the operators are routed.
- **Feedback** (0 to 7) and **FM amount** (0 to 2), which scales the modulator depth.
- Select an operator with **OP 1** to **OP 6**. Each operator has **Mode** (Ratio or Fixed), **Coarse**, **Fine**, **Detune**, **Level** and **Vel sens**, plus the rate and level of its four envelope stages.
- **Use amp envelope:** when off, only the operator envelopes shape the sound, as on a real DX7. Loading a cartridge turns it off.
- **Load cart (.syx)**, or drop a `.syx` file, loads either a 32-voice cartridge or a single-voice patch. A cartridge's voices appear in a grid on the right. Click one to load it.
- **Save cart** writes the layer's voices to a `.syx` file. Without a loaded cartridge, it saves the current voice alone. Empty slots in a 32-voice file are Init voices.
- **Chimera cart** loads the built-in **Chimera Voices** cart: 32 original voices in e-pianos and keys, bells and mallets, basses, brass and winds, leads, pads and strings, and percussion and FX.
- **Dice** next to it: click for a random voice from the layer's cart (Chimera Voices if the layer has no cart). Shift-click loads a random `.syx` file from your carts folder, then a random voice from it. If no folder is set yet, Shift-click asks you to choose one.
- **Folder...** sets the carts folder. ELooper remembers it, and searches its subfolders.

**Sampler**

- **Load sample**, or drop an audio file (WAV, AIFF, FLAC, MP3 or Ogg). A sample can be up to 2 minutes long.
- **Root note** is the note that plays the sample at its original pitch. It is read from the file if the file stores one, and otherwise it is note 60.
- **Start** sets where playback begins. **Reverse** plays the sample backwards.
- **Loop** is Off, Forward or Ping-pong, with **Loop start** and **Loop end** points.

**Pluck**

- A Karplus-Strong plucked string.
- **Decay** sets the ring time, from about 0.2 to 10 seconds. **Damping** runs from bright to dark. **Pick position** sets where the string is plucked. **Noise colour** sets the excitation, from dark to bright.

**Filter**

Each layer has its own filter.

- **Type:** Off, LP 12, LP 24, HP 12, HP 24, BP, Notch, Ladder or Comb. Off bypasses the filter.
- **Cutoff** (20 Hz to 20 kHz) and **Resonance**, both automatable.
- **Drive** adds saturation before the filter.
- **Key track** sets how much the cutoff follows the played note, from 0% to 100%.
- **Env amount** sets how far the filter envelope moves the cutoff, up to ±60 semitones.

**Envelopes**

Each layer has three envelopes, each with Attack, Decay, Sustain and Release:

- **Amp** shapes the layer's loudness.
- **Filter** shapes the cutoff, scaled by **Env amount**.
- **Mod** is a source for the modulation matrix.

**Modulation page**

- **Layer LFO 1** and **LFO 2** belong to each layer. **Global LFO 1** and **2** are shared by all layers.
- There are seven shapes: Sine, Triangle, Saw Up, Saw Down, Square, Sample & Hold and Smooth Random.
- **Sync** locks the rate to the song tempo, and the rate then follows beat divisions from 1/16 to 16 beats. Without Sync, the rate runs from 0.01 to 40 Hz.
- **Retrigger** (restart on each note) and **Fade in** (ramp up after each note) apply to the layer LFOs only.
- **Mod matrix:** eight slots per layer. Each slot has a source, a destination and an amount from -1 to +1.
  - Sources: LFO 1, LFO 2, Global LFO 1, Global LFO 2, Amp, Filter and Mod envelopes, Velocity, Key Track, Mod Wheel, Aftertouch, Pitch Bend, Random, and Macro 1 to 4.
  - Destinations: Pitch, Cutoff, Resonance, Drive, Level, Pan, Pulse Width, Osc Mix, Osc 2 Pitch, Noise, Wave Position, FM Amount, Sample Start, Pluck Damping, LFO 1 Rate, LFO 2 Rate and Filter Env Amount.
- **Pitch bend range** sets the bend range for all layers, from 0 to 24 semitones (2 by default).

**Effects page**

The effects apply to the Main output pair only.

- **Chorus:** Rate (0.05 to 5 Hz), Depth and Mix.
- **Delay:** **Tempo sync** switches the time from milliseconds (1 to 2000) to beat divisions up to 4 beats. Also **Feedback** (up to 95%), **Tone**, **Mix** and **Ping-pong**.
- **Reverb:** Size, Damping, Pre-delay (0 to 200 ms), Width and Mix.

Each effect has an **On** switch.

**Factory presets**

Chimera has 52 factory presets in nine categories. The preset menu lists them in this order.

| Category | Count | Examples |
|---|---|---|
| Init | 2 | Init, Sampler |
| Bass | 9 | Fat Bass, Acid Line, Deep Sub, Squelch Box |
| Lead | 8 | Super Saw Lead, Sync Lead, Glide Lead, Crystal Lead |
| Pad | 8 | Warm Pad, Morph Pad, Choir Loft, Shimmer Veil |
| Keys | 6 | Glass Keys, Drawbar Organ, Vibe Mallets |
| FM | 6 | FM Bell, FM Brass, Wood Marimba, Metal Anvil |
| Pluck | 5 | Nylon Pluck, Harp, Koto Strum |
| Texture | 4 | Drone Machine, Wind Sweep, Comb Cathedral |
| Layered | 4 | Keys + Pad, Bass \| Keys Split, Pluck Over Pad |

**Automation and MIDI**

- **Automatable:** Macro 1 to 4, Master, and each layer's Level, Cutoff, Resonance and Pan. They appear in the automation layer's **+ Lane** menu.
- **Mod wheel** (CC 1) drives the Mod Wheel source. **Channel pressure** drives Aftertouch. **Pitch bend** follows the pitch bend range.
- **Sustain** (CC 64) holds notes until the pedal is released.
- **All Notes Off** (CC 123) releases all notes. **All Sound Off** (CC 120) silences everything at once and clears the effect tails.

## Chimera outside ELooper

Chimera is also built as a standalone app, an Audio Unit and a VST3 plug-in, and as a web page. The downloads are in the **latest** release at `https://github.com/emac87/ELOOPER/releases/download/latest/`, with the same URL pattern for each zip.

**Mac** (`Chimera-macos.zip`):

- `Chimera.app` is the standalone app. On first launch, use **System Settings > Privacy & Security > Open Anyway**.
- `Chimera.component` is the Audio Unit. Copy it to `~/Library/Audio/Plug-Ins/Components/`.
- `Chimera.vst3` is the VST3 plug-in. Copy it to `~/Library/Audio/Plug-Ins/VST3/`.
- The builds are not notarised. If a host refuses to load a plug-in, run this in Terminal:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Chimera.component ~/Library/Audio/Plug-Ins/VST3/Chimera.vst3
```

**Windows** (`Chimera-windows.zip`):

- `Chimera.exe` is the standalone app. SmartScreen may warn because the build is not signed: click **More info** > **Run anyway**.
- `Chimera.vst3` is the VST3 plug-in. Copy the whole folder to `C:\Program Files\Common Files\VST3\`.
- There is no AAX build.

**Web** (`chimera.html`, from the same release):

- Open it in Chrome, Edge, Firefox or Safari, then click the page to start audio.
- Computer keyboard: `A` `W` `S` `E` `D` `F` `T` `G` `Y` `H` `U` `J` `K` play notes, and `Z` and `X` change octave. Web MIDI works in Chrome and Edge.
- Drag `.syx` carts and audio samples onto the page.
- **Export** saves the patch as a `.chimera.json` file. Web presets are separate from the desktop `.chimera` presets.

Presets saved in ELooper and in the plug-ins are the same `.chimera` files, in the same preset folder on the same computer.

## Bouncing a track

Bouncing renders a track's full output, including its effects, into a new audio loop on a new track. The loop is one cycle of the track's longest loop.

1. Select the track and press `⌘B`, or right-click its header and choose **Bounce Track**.
2. Confirm. The new track plays the bounce once it starts.
3. The original track moves to the **Bounced** page (**View > Bounced**). Its plug-ins are suspended and it is silent.

On the **Bounced** page, **restore** brings a track back to the main page. **Delete** removes it and its loops, after confirmation.

## Timeline and arrangement record

The **Timeline** page (`T`) shows one row per track on a bar ruler, with a playhead.

- **Click the ruler** to move the playhead.
- **Shift-drag the ruler** to set the loop region. Right-click the ruler to turn the region on or off.
- **Add a clip:** double-click an empty spot in a track row, then choose a loop from that track. Or, on the Loops page, right-click a loop and choose **Add to Timeline at Playhead**. The clip snaps to the bar.
- **Edit a clip:** drag it to move it, drag its edge to change its length, or right-click it for **Duplicate** and **Delete**.

Switching to the Timeline page puts the transport in timeline mode.

**Arrangement record** writes your performance into the timeline. Turn on the **arrangement record** toggle in the transport bar, then go back to the **Loops** page and launch loops. Each launch is written to the timeline as a clip.

## Ableton Link

Turn on **Link** in the transport bar to share tempo and phase with other Link apps on your network. The button shows the number of peers. Turn on the start/stop sync toggle to share transport start and stop as well.

Tempo changes made in ELooper are sent to the other apps, and tempo changes from them are followed.

## MIDI controllers

ELooper can be controlled from a MIDI controller. There are two kinds of mapping.

**Global mappings** are set in **Settings > Controllers**. Click one of the learn buttons, then move a control on the controller:

- **Learn Play/Stop**
- **Learn Record selected loop**
- **Learn Stop All**
- **Learn Tap Tempo**

While a button is waiting, it reads **Waiting for MIDI...**. Each mapping appears in the **Mappings** list with its device, channel and CC or note number. Use **Remove** to delete one, or **Clear All** to delete them all.

**Per-loop mappings** are set on the loop. Right-click the loop, choose **MIDI Learn**, and pick **Trigger**, **Record** or **Stop**, then press a control.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `⌘N` | New session |
| `⌘O` | Open project |
| `⌘S` | Save project |
| `⇧⌘S` | Save project as |
| `⌘,` | Settings |
| `Space` | Play / Stop |
| `R` | Trigger the selected loop (record, close, play or stop) |
| `O` | Overdub the selected loop |
| `Delete` or `Backspace` | Clear the selected loop (asks for confirmation) |
| `M` | Show or hide the Mixer dock |
| `A` | Show or hide the automation layer |
| `E` | Show or hide the Editor dock |
| `T` | Switch between the Loops and Timeline pages |
| `⌘B` | Bounce the selected track |
| `1` to `9` | Launch scene 1 to 9 (ignored while typing in a text field) |
| `0` | Stop all loops |

In the piano roll, `⌘A`, `⌘D`, `Q`, `Delete`, `Backspace` and the arrow keys act on the selected notes, as described in [MIDI loops](#midi-loops). Undo and redo are `⌘Z`, `Shift`+`⌘Z` and `⌘Y`.

## Saving and opening projects

Use **File** to manage projects:

- **New** (`⌘N`) starts an empty session.
- **Open...** (`⌘O`) opens an existing project.
- **Save** (`⌘S`) saves the project.
- **Save As...** (`⇧⌘S`) saves a copy under a new name.
- **Open Autosave...** opens a recovered copy from the autosave folder (see below).

A project is a folder with the extension `.elooper`. It holds a `session.xml` file describing tracks, loops, plug-ins and settings, and an `audio` folder with the recorded takes as WAV files. Keep the whole folder together when you move or back up a project.

**Saving is safe:** ELooper writes the new project to a temporary folder, checks it can be read back, and only then swaps it in place of the old one. If anything fails, the previous save is kept. Any other files you put in the project folder yourself (notes, artwork and so on) are kept too; only `session.xml` and `audio` are replaced.

**Unsaved changes:** a project with unsaved changes shows a dot (•) after its name in the title bar. When you start a new session, open a project, quit or close a window with unsaved changes, ELooper asks whether to **Save**, **Don't Save** or **Cancel**.

**Autosave:** while the transport is stopped and the project has unsaved changes, ELooper saves a copy every 2 minutes into the ELooper **Autosave** folder in Application Support. The copy does not change your project file or its unsaved state. To recover one, choose **File > Open Autosave...**. A recovered project is detached from its original file, so Save asks where to put it rather than overwriting the autosave.

Plug-in presets and channel presets are not stored in the project. See [Plug-ins and presets](#plug-ins-and-presets).
