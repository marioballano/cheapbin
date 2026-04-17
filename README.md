# ♪ CHEAPBIN

**Binary-to-chiptune music generator for macOS.**

Feed it any file — executables, images, archives — and listen to what your binaries sound like as 8-bit chiptune melodies.

## Build

```bash
cmake -B build
cmake --build build
```

Requires macOS (Core Audio) and a C11 compiler (Xcode CLT or Xcode).

## Usage

```bash
./build/cheapbin <file>
```

Examples:

```bash
./build/cheapbin /bin/ls
./build/cheapbin /bin/cat
./build/cheapbin ~/Documents/photo.jpg
./build/cheapbin /usr/lib/libc.dylib
```

### Controls

| Key     | Action         |
|---------|----------------|
| `space` | Pause / Resume |
| `q`     | Quit           |

## How It Works

### Pipeline

```
Binary File → Chunking → Statistical Analysis → Composition → Synthesis → Core Audio → 🔊
                                                                    ↓
                                                           Terminal Visualizer
```

### Binary Analysis

The file is divided into 256-byte sections. For each section, cheapbin computes:

- **Shannon entropy** (0–8 bits) — measures randomness/compressibility
- **Byte mean** — average byte value
- **Variance** — how spread out the byte values are
- **Zero ratio** — fraction of null bytes

### Mapping to Music

| Statistic | Musical Parameter |
|-----------|-------------------|
| Entropy | Tempo (100–160 BPM) — high entropy = faster |
| Byte mean | Root key — `mean % 12` selects the tonal center |
| Variance | Waveform — low=triangle, medium=square, high=sawtooth |
| Zero ratio | Rests — null-heavy regions become silence |
| Individual bytes | Melody notes on a pentatonic minor scale |
| Bit patterns | Drum triggers (kick, snare, hi-hat) |

All melody notes are quantized to the **pentatonic minor scale**, which guarantees pleasant-sounding output regardless of input data. A running-average filter smooths pitch transitions.

### 4-Channel Synthesis (NES-inspired)

| Channel | Waveform | Role |
|---------|----------|------|
| CH1 — Melody | Square/Saw/Tri (data-driven) | Lead melody from byte values |
| CH2 — Bass | Triangle | Root + fifth alternation |
| CH3 — Arpeggio | Square (25% duty) | Minor chord arpeggiation |
| CH4 — Drums | Noise | Kick/snare/hat from bit patterns |

Each channel has its own **ADSR envelope** for authentic chiptune attack and decay characteristics. Audio is synthesized at 44100 Hz, 16-bit mono, and played through Core Audio's AudioQueue API.

### Terminal UI

The display shows real-time:
- Per-channel level meters with color coding
- Animated waveform visualization
- Current note, waveform type, and BPM
- Progress bar through the binary file
- Playback controls

## Architecture

```
src/
├── main.c       — CLI parsing, main loop, signal handling
├── reader.c/h   — Binary file I/O
├── analyzer.c/h — Per-section entropy, mean, variance computation
├── composer.c/h — Binary → musical event sequence mapping
├── synth.c/h    — Waveform generators, ADSR envelopes, 4-channel mixer
├── audio.c/h    — Core Audio AudioQueue wrapper
└── display.c/h  — ANSI terminal UI and visualizer
```

## Dependencies

- **macOS** (Core Audio / AudioToolbox framework)
- **C11** compiler
- **CMake** ≥ 3.20
- No external libraries

## License

Public domain. Do whatever you want with it.
