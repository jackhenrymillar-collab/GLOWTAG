# GLOWTAG

A personal, MIDI- and sampling-focused DAW for macOS, built natively with C++ and [JUCE](https://juce.com).
The look is graffiti art drawn as calm neon line work: thin outlines, serif caps headings and
detailed glowing icons, on a dark grey background. One theme colour by default (green), with a
Rainbow mode that uses the full card palette.

![Arrange view](docs/arrange.png)

## Build on a Mac

You need Xcode (from the App Store) and CMake (`brew install cmake`).

```sh
git clone https://github.com/jackhenrymillar-collab/glowtag.git
cd glowtag
cmake -B build -G Xcode          # first run downloads JUCE 8.0.4, takes a few minutes
open build/GLOWTAG.xcodeproj     # pick the "Glowtag" scheme and press Run
```

Or build from the terminal:

```sh
cmake --build build --config Release
open build/Glowtag_artefacts/Release/GLOWTAG.app
```

If you already have JUCE checked out, add `-DGLOWTAG_JUCE_DIR=/path/to/JUCE` to the first command.

## Using it

| Key | Action |
| --- | --- |
| Space | Play / stop. Stop returns to where play started; stop again to go to the start |
| A W S E D F T G Y H U J K O L P | Play notes on the selected track (Z and X change octave) |
| R | Arm recording. Notes you play while playing are recorded into the selected track |
| Delete | Delete the selected clip (Arrange) or note (Edit) |
| Cmd D | Duplicate the selected clip |
| 1 / 2 / 3 | Arrange / Mix / Edit |
| Cmd K | Command palette (views, transport, theme) |
| Up / Down | Select track |

Any MIDI keyboard that is plugged in plays and records into the selected track too.

**Arrange**
- Click the ruler to move the playhead. Drag along the strip under the bar numbers to set the loop, and toggle looping with the loop button in the top bar.
- Drag clips to move them (between lanes too), drag their right edge to resize, Alt-drag to copy, and right-click for duplicate, delete and loop length.
- Double-click an empty lane to make a new clip, or a clip to open it in Edit.
- Drop audio files (wav, aiff, flac, mp3) onto a lane to make audio clips.
- Cmd + scroll zooms, scroll moves along the timeline.

**Edit** (piano roll for the selected clip)
- Pencil: click to draw a note and drag right to set its length. Drag notes to move them, drag the right edge to resize.
- Scissors split a note, the velocity tool drags velocity, and the lane under the grid sets velocity per column.
- Double-click a note (or press Delete) to remove it. Scroll moves up and down the keyboard.
- Loop sets how often the clip's notes repeat inside it.

**Devices** (bottom panel) is modular. Each module you add wires itself in (effects go at the end of the
chain, an LFO patches into the last module's first free knob). Drag between jacks to patch,
pull a plugged-in jack to re-wire it, double-click a cable to remove it, drag a module's header
to move it, and right-click a module to remove it. Double-click a device in the browser to add it.
Tidy lines everything up in signal order.

**Mix**: faders, pan knobs and mute / solo all affect the sound.

## Tests

Offline checks for the engine (transport, loop, clips, live MIDI, recording, patching):

```sh
cmake -B build -G Xcode -DGLOWTAG_TESTS=ON
cmake --build build --target GlowtagTests --config Debug
./build/GlowtagTests_artefacts/Debug/GlowtagTests
```

## Code layout

```
Source/
  Main.cpp              app and window
  MainComponent.*       owns the project, engine and views; layout and shortcuts
  Theme.*               colours (one-colour shades or Rainbow) and fonts
  Engine/
    Project.*           tracks, clips, step patterns, tempo
    Patch.*             modular device patch per track: modules, jacks, cables, auto-wiring
    AudioEngine.*       sample-accurate step sequencer with built-in drum and synth voices
  UI/
    Icons.*             glowing line-art icons and panel drawing
    GlowLookAndFeel.*   pill buttons, glowing knobs, faders
    TopBar, BrowserPanel, TrackList, ArrangerView, DevicePanel,
    MixerView, EditorView, StatusBar
```

## Roadmap

1. **Foundation** (done): app shell, audio engine, themed UI with Arrange, Mix and Edit.
2. **Core engine** (this version): timeline with MIDI and audio clips, loop, metronome, piano roll editing, MIDI keyboard playing and recording.
3. **Sampling and devices**: sampler and chopper, drum machine, synth, effects, a working device chain.
4. **Mix, Edit and polish**: full mixer routing, full piano roll, AU / VST hosting, saving projects.
