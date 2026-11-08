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
12. [Bouncing a track](#bouncing-a-track)
13. [Timeline and arrangement record](#timeline-and-arrangement-record)
14. [Ableton Link](#ableton-link)
15. [MIDI controllers](#midi-controllers)
16. [Keyboard shortcuts](#keyboard-shortcuts)
17. [Saving and opening projects](#saving-and-opening-projects)

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
- **Docks**: **Mixer**, **Auto** (automation) and **Editor** (the MIDI editor). Each dock opens below the page. Drag the bar between the page and a dock to resize it.
- **Settings** and a CPU readout.

The **Loops** page is the main screen. It has no mixer controls, only loops. Each track is a row: a slim label on the left, then the track's loops as cells to the right. Click a cell to select it. Plain buses are shown on the mixer only (a bus that holds loops still appears at the bottom of the Loops page). All levels, effects, sends and routing are on the **Mixer** dock.

**Scenes:** the bar at the top has **Stop all** and one **Scene** button above each column of loops. **Scene 1** plays the first loop of every track and stops every other loop. **Scene 2** does the same with the second loop, and so on. Each loop starts and stops on its own quantise setting (the next bar by default). A track whose slot in that column is empty goes silent. A loop that is recording is left alone. The scene button lights up while its loops are the ones playing. Right-click a Scene button (or **Stop all**) for **MIDI Learn**.

## Tracks

There are five kinds of track:

| Kind | What it does |
|---|---|
| **Audio** | Records from an audio input. |
| **Instrument** | Takes MIDI input and plays a software instrument (Audio Unit). Its MIDI loops record that input. |
| **Bus** | Receives sends and track outputs. It can record what it receives. |
| **Master** | The final mix. It goes to the hardware output pair. Every session has one. |
| **AuxOut** | An extra stereo output of a multi-output instrument. See [Mixer and routing](#mixer-and-routing). |

A new session starts with one audio track and one instrument track.

**Adding tracks:** click **+ Add Track** below the last track and choose Audio, Instrument or Bus. You can also use the **Track** menu (Add Audio Track, Add Instrument Track, Add Bus).

**Adding loops:** each track row ends with a **+** button. It adds an audio loop (MIDI loop on instrument tracks). Automation loops are hidden for now; automation will come back in a different form.

**Track label:** the label at the left of each row shows the track's colour, its type icon (synth, drums, sampler, EQ and so on, or the channel type), its name and its input (or instrument). Click it to select the track, double-click to rename it, right-click for the track menu, and drag it up or down to reorder tracks. A multi-out instrument has a **Show / Hide outputs** button.

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
5. **Overdub.** Shift-click a playing loop, or double-click an audio loop, to overdub on top of it. The cell turns orange while overdubbing. Shift-click or double-click again to stop overdubbing.

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

- **Click**: trigger the loop. An empty loop records, a recording loop closes, a playing loop stops, and a stopped loop plays. Clicking also selects the loop.
- **Shift-click**: overdub.
- **Double-click**: overdub an audio loop, or open the editor for a MIDI loop.
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

To edit a MIDI loop, double-click it, or right-click and choose **Edit MIDI**. The editor opens in the **Editor** dock (`E`). It has two tabs:

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

**Each loop has an automation layer.** Automation runs while the loop plays. Automation-only loops are hidden for now (automation will be redone differently); loops of that kind in older projects are not shown on the Loops page.

**To record automation:** start a loop recording or overdubbing, then move a control on its channel, such as a fader, pan, EQ band, send or plug-in parameter. Those movements are written into that loop's automation layer.

**To draw automation:** open the **Auto** dock (`A`) and select the loop. Use **+ Lane** in the panel to add a lane. The parameter menu groups parameters into Mixer, EQ, Sends and each plug-in. Use the pencil tool to draw points freehand. Each lane has **On** to enable it, **Clear** to empty it, and **X** to remove it.

The panel has two views. **Loop layer** shows the selected loop's automation. **Track lanes** shows the track's own lanes, which are used on the timeline.

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

**Plug-in presets:** from a plug-in slot, choose **Presets...**. Choose **Save Preset...** to save the plug-in's current state, or pick a saved preset to load it.

**Channel presets:** a channel preset captures a track's input gain, volume, pan, EQ, monitoring, MIDI effects, instrument, and pre- and post-fader inserts. Right-click the track label (or its mixer strip header) and choose **Channel Presets...**, or use the **Track** menu.

Plug-in and channel presets are stored in your Application Support folder, not in the project, so they are available to every project.

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
| `A` | Show or hide the Automation dock |
| `E` | Show or hide the Editor dock |
| `T` | Switch between the Loops and Timeline pages |
| `⌘B` | Bounce the selected track |

In the piano roll, `⌘A`, `⌘D`, `Q`, `Delete`, `Backspace` and the arrow keys act on the selected notes, as described in [MIDI loops](#midi-loops). Undo and redo are `⌘Z`, `Shift`+`⌘Z` and `⌘Y`.

## Saving and opening projects

Use **File** to manage projects:

- **New** (`⌘N`) starts an empty session.
- **Open...** (`⌘O`) opens an existing project.
- **Save** (`⌘S`) saves the project.
- **Save As...** (`⇧⌘S`) saves a copy under a new name.

A project is a folder with the extension `.elooper`. It holds a `session.xml` file describing tracks, loops, plug-ins and settings, and an `audio` folder with the recorded takes as WAV files. Keep the whole folder together when you move or back up a project.

Plug-in presets and channel presets are not stored in the project. See [Plug-ins and presets](#plug-ins-and-presets).
