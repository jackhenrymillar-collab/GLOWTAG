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
| Space | Play / stop |
| 1 / 2 / 3 | Arrange / Mix / Edit |
| Cmd K | Command palette (views, transport, theme) |
| Up / Down | Select track |

- Drag the tempo in the LCD up or down to change BPM.
- Click a theme swatch to change the colour, or turn on Rainbow.
- In Edit, click the piano roll with the pencil to add or remove notes. The beat changes live.
- Double-click the arranger to open the selected track in Edit.
- The device panel is modular. Each module you add wires itself in (effects go at the end of the
  chain, an LFO patches into the last module's first free knob). Drag between jacks to patch,
  pull a plugged-in jack to re-wire it, double-click a cable to remove it, drag a module's header
  to move it, and right-click a module to remove it. Double-click a device in the browser to add it.
  Tidy lines everything up in signal order.
- Mixer faders, pan knobs and mute / solo all affect the sound.

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

1. **Foundation** (this version): app shell, audio engine, themed UI with Arrange, Mix and Edit.
2. **Core engine**: real transport and timeline, MIDI and audio clips, MIDI keyboard recording.
3. **Sampling and devices**: sampler and chopper, drum machine, synth, effects, a working device chain.
4. **Mix, Edit and polish**: full mixer routing, full piano roll, AU / VST hosting, saving projects.
