# naja-schematic

[![Native macOS Build](https://github.com/najaeda/naja-schematic/actions/workflows/native-macos.yml/badge.svg?branch=main)](https://github.com/najaeda/naja-schematic/actions/workflows/native-macos.yml)
[![Native Linux Build](https://github.com/najaeda/naja-schematic/actions/workflows/native-linux.yml/badge.svg?branch=main)](https://github.com/najaeda/naja-schematic/actions/workflows/native-linux.yml)
[![Emscripten Build](https://github.com/najaeda/naja-schematic/actions/workflows/emscripten.yml/badge.svg?branch=main)](https://github.com/najaeda/naja-schematic/actions/workflows/emscripten.yml)

A C++20, [Dear ImGui](https://github.com/ocornut/imgui)-based netlist
viewer/schematic browser for the [naja](https://github.com/najaeda/naja) SNL
netlist data model. It builds to two targets from the same core sources: a
native desktop app and a WASM app that runs in a browser or a VSCode webview.

## Features

- Interactive hierarchical schematic and equipotential (net fan-out) views
- Lazy-loaded design tree (instances, terms, bus bits) for large netlists
- Diagnosis overlay: color-codes and annotates flagged instances/pins/nets
  from a `diagnosis_response` JSON payload, with hover tooltips and a flat
  diagnostics list panel
- Loads Verilog, SystemVerilog, or pre-built SNL netlists natively; the
  browser build talks to a Python netlist server over WebSocket
- Runs natively on macOS or Linux, or anywhere with a browser/VSCode via WASM

## Building

Clone with submodules:

```bash
git clone --recursive https://github.com/najaeda/naja-schematic.git
# or, if already cloned without them:
git submodule update --init --recursive
```

### Native standalone target (`naja-schematic-standalone`)

Builds on macOS and Linux. Requires SDL2 and OpenGL, plus a native file-picker
backend (Cocoa on macOS, `zenity` on Linux — see `CLAUDE.md` for the full
Linux dependency list).

On macOS:

```bash
brew install sdl2
```

Build with CMake presets, which pin the compiler to Apple Clang so
Emscripten on `PATH` doesn't get auto-detected:

```bash
cmake --preset native-debug      # or native-release
cmake --build --preset native-debug
```

The build directory lives outside the repo, at
`../naja-schematic-build/<preset>`.

On Linux, invoke CMake directly instead (the presets are macOS-specific):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Run (`../naja-schematic-build/native-debug/naja-schematic-standalone` on
macOS, `build/naja-schematic-standalone` on Linux), optionally with a
netlist to load (dispatched by extension — `.v` → Verilog, `.sv` →
SystemVerilog, otherwise treated as an SNL directory) and/or a diagnosis
JSON to pre-load:

```bash
naja-schematic-standalone [path/to/netlist] [--diagnosis path/to/diagnosis.json]
```

`<netlist>` and `--diagnosis <path>` are both optional and order-independent.
Without `--diagnosis`, use File > Load Diagnosis JSON... in the app instead.

### WASM target (`naja-schematic`)

Requires [Emscripten](https://emscripten.org/) (`emcmake`/`emrun` on `PATH`).

```bash
mkdir build-wasm && cd build-wasm
emcmake cmake ..
cmake --build .
emrun --port 8080 naja-schematic.html
```

The WASM app doesn't link naja directly — it talks to a netlist server over a
WebSocket. Start the server first:

```bash
python3 scripts/najaeda_server.py   # serves ws://localhost:8081/ws
```

## Architecture

The app has two entry points (`src/main_native.cpp`, `src/main_wasm.cpp`)
that share almost everything except how netlist data is sourced, behind an
`INetlistProvider` interface. Requests/responses between the UI and a
provider are JSON messages over a small request/response protocol; a
`diagnosis_response` message is a server push that annotates the currently
loaded netlist without loading anything new.

See [`CLAUDE.md`](CLAUDE.md) for the full architecture writeup, including the
wire protocol's message shapes and the diagnosis overlay's data flow — it
doubles as the guide used by AI coding agents working in this repo.

## Status

This project is under active development as part of an evolving AI
RTL-diagnosis workflow. Interfaces (in particular the wire protocol and the
`diagnosis_response` shape) may still change.

## License

Apache License 2.0 — see [LICENSE](LICENSE).
