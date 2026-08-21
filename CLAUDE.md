# CLAUDE.md

This file provides guidance to AI coding agents (Claude Code, Codex, and others)
when working with code in this repository.

## Project overview

naja-schematic is a C++20 ImGui-based netlist viewer/schematic browser for the
[naja](https://github.com/najaeda/naja) SNL netlist data model. It builds to two
distinct targets from the same core sources (see Architecture below): a native
desktop app and a WASM app that runs in a browser or a VSCode webview.

## Vision: part of an AI RTL-diagnosis loop

naja-schematic exists to make **AI-generated diagnosis reports on RTL/netlists
visually inspectable by a human**, not to be a general-purpose EDA schematic
viewer. The target workflow it serves:

1. An AI agent (e.g. Claude) edits/manipulates RTL, optionally using
   **naja-scope** (`https://github.com/najaeda/naja-scope`) — an MCP server,
   sibling to this repo on the same naja/SNL base, that lets an agent query a
   netlist (connectivity, hierarchy, fan-in/fan-out) without pasting RTL into
   context. It has no GUI and produces no files — it's a query layer for
   agents, not a diagnosis/report source.
2. Changes are checked with **kepler-formal**
   (`https://github.com/keplertech/kepler-formal`) — an LEC/SEC equivalence
   checker that operates on Verilog/SystemVerilog and, notably, **speaks the
   Naja interchange format directly**. Its output today is exit codes plus
   text/log reports (miter logs, `boundary_terms.txt`, `skipped_*.txt`); there
   is **no structured/JSON diagnosis format published yet** anywhere in this
   ecosystem, and internal instance/net names aren't guaranteed stable across
   designs in its SEC diagnostics (only top-level terminal names are).
3. **naja-schematic** is meant to be the visualization endpoint, but the
   report hand-off format from kepler-formal doesn't exist yet — building it
   is part of closing this loop, not something to assume is already there.

This is why ImGui was picked (portability: native + WASM/browser/VSCode
webview, so the viewer can be embedded wherever the loop runs) and why naja/SNL
was picked for netlist representation (kepler-formal already speaks it, so no
netlist-level translation is needed — only a report-level one).

**Integration surface (implemented):** the `diagnosis_response` message type
and its rendering — see "Diagnosis overlay" below. What's still missing
upstream: neither kepler-formal nor naja-scope emits this JSON today, so an
adapter that turns kepler-formal's log/text output into `diagnosis_response`
items is the remaining piece to actually close the loop.

## Building

Clone with submodules (`thirdparty/imgui`, `thirdparty/naja`) — if already cloned
without them: `git submodule update --init --recursive`.

### Native standalone target (`naja-schematic-standalone`)

Uses CMake presets (`CMakePresets.json`), which pin the compiler to system Apple
Clang so Emscripten on `PATH` doesn't get auto-detected. Build directory lives
*outside* the repo, at `../naja-schematic-build/<preset>`.

```bash
cmake --preset native-debug      # or native-release
cmake --build --preset native-debug
```

Requires SDL2 and OpenGL (e.g. `brew install sdl2`). Pulls in `thirdparty/naja`
as a CMake subdirectory (`naja_nl`, `naja_snl_verilog`, `naja_snl_liberty`,
`naja_snl_systemverilog`, `naja_nl_dump`). `NativeFileDialog.mm` uses Cocoa, so
this target is macOS-only as currently written.

Run directly, optionally with a netlist to load (dispatched by extension —
`.v` → Verilog, `.sv` → SystemVerilog via slang, otherwise treated as an SNL
directory):

```bash
../naja-schematic-build/native-debug/naja-schematic-standalone [path/to/netlist]
```

### WASM target (`naja-schematic`)

Requires Emscripten (`emcmake`/`emrun` on `PATH`). Detected automatically in
`CMakeLists.txt` by matching the compiler name (`em++`), not by a build flag.

```bash
mkdir build-wasm && cd build-wasm
emcmake cmake ..
cmake --build .
emrun --port 8080 naja-schematic.html
```

The WASM app doesn't link naja directly — it talks to a netlist server over a
WebSocket instead. Start the server first:

```bash
python3 scripts/najaeda_server.py   # serves ws://localhost:8081/ws
```

(`scripts/test_server.py` is a minimal canned-response stub for protocol
testing without a real netlist backend.)

## Architecture

### Dual-mode, shared core

The app has two entry points that share almost everything except how netlist
data is sourced:

- **`src/main_native.cpp`** — native desktop: SDL2 + OpenGL 3.3 core, a plain
  `while` loop calling `appFrame()`. Backed by `LocalSNLProvider`, which loads
  netlists directly through the naja SNL C++ API in-process (Verilog,
  SystemVerilog, or pre-built SNL directories).
- **`src/main_wasm.cpp`** — browser/VSCode webview: SDL2 + OpenGL ES via
  Emscripten's `emscripten_set_main_loop()`. Backed by `WebSocketProvider`,
  a thin wrapper around `WebSocketClient` that connects to
  `ws://localhost:8081/ws` (implemented by `scripts/najaeda_server.py`, a
  Python asyncio server built on `najaeda`).

Both providers implement **`INetlistProvider`** (`src/INetlistProvider.h`):
`send()`, `on_open()`/`on_message()`/`on_close()`/`on_error()` callback
registration, and `start()`. `LocalSNLProvider::start()` fires `on_open`
synchronously and answers requests in-process; `WebSocketProvider` is a passive
wrapper since the underlying socket connects in its constructor.

`src/AppLogic.h/.cpp` holds the logic shared by both entry points:
- `setupProvider(AppState&)` wires all provider callbacks and calls
  `provider->start()`. All response-message dispatch (`root_response`,
  `instances_response`/`primitives_response`/`children_loaded`,
  `terms_response`, `equipotential_response`, `expanded_instance_terms`,
  `error`, ...) lives here, driven by the `"response"` field of incoming JSON —
  this is the one place to look when tracing how a server/provider reply turns
  into UI state.
- `appFrame(AppState&)` runs one ImGui frame (poll events, build UI, render)
  and returns `false` on quit.

### Wire protocol

Requests/responses are JSON with a `"request"`/`"response"` type field (e.g.
`load_root`, `load_instance`, `load_primitives`, `load_terms`,
`load_equipotential` → `*_response`). Both `LocalSNLProvider` (native,
`buildRootResponse()`/`buildInstancesResponse()`/etc.) and
`najaeda_server.py` (WASM/browser) must independently implement this same
protocol — when changing one side, check the other.

`diagnosis_response` is different: it's a **server push**, not a reply to a
request (a diagnosis run finishes on its own schedule), and it *annotates*
the already-loaded netlist rather than loading anything:

```json
{
  "response": "diagnosis_response",
  "items": [
    {
      "kind": "instance",          // "instance" | "net"
      "path": ["u1", "u2"],        // instance-name path, root excluded; [] = top level
      "terminal": "Q",             // pin/port base name, no bus-bit suffix; "net" only
      "severity": "error",         // "info" | "warning" | "error"
      "message": "...",
      "source": "kepler-formal"    // free-form, shown in the UI
    }
  ]
}
```
`najaeda_server.py` doesn't implement this yet (no upstream data source —
see Vision above); `scripts/test_server.py` sends a canned example after
`load_root` as a demo/test fixture. Native/standalone mode has no server at
all, so it gets diagnosis data via **File > Load Diagnosis JSON...**
(reads a `{"items": [...]}` file or a bare array through the same
`DiagnosisItem` parser) instead.

### Core modules (`src/`)

- **`NetlistTree`** — the design hierarchy tree (instances, terms, bus bits),
  lazily populated by provider requests as nodes are expanded.
- **`GUIData`** — top-level UI state container (the netlist tree plus the list
  of currently-displayed `Equipotential`s).
- **`SchematicView`** / **`EquipotentialView`** — the two main render panels;
  operate on the renderer-side types in `Types.h` (`InstanceShape`, `Port`,
  `NetWire`, `RenderEquipotential`) as opposed to the wire-format types
  (`InstanceResponseJson`, `TermResponseJson`, `Equipotential`, ...) also
  defined there.
- **`Console`** — in-app log/message panel.
- **`WebSocketClient`** — low-level Emscripten WebSocket binding used by
  `WebSocketProvider`.
- **`DiagnosisStore`** — global static store (same pattern as
  `EquipotentialView`) for the current `diagnosis_response`/loaded-JSON
  diagnosis set. Indexes items by instance path and by (path, terminal) net
  key; `NetlistTree` and `EquipotentialView` query it during rendering to
  tint flagged instances/pins/wires and show tooltips — see `instanceColor()`
  /`netColor()` in `src/DiagnosisStore.h`. Cleared on every fresh
  `root_response`/`root_loaded` (stale diagnoses reference the old design).
- **`DiagnosisView`** — renders the flat diagnostics list into the
  bottom-left panel in `AppLogic.cpp`.

### Diagnosis overlay

Once `DiagnosisStore` holds data, three places render it — no polling, they
just query the store each frame:
- `NetlistTree` (`NetlistTreeInstanceNode::getColor()`/`getDiagnostics()`) —
  colors the tree label and shows a hover tooltip for a flagged instance.
- `EquipotentialView`/`SchematicView` — tints a flagged instance's box
  outline (`InstanceShape::diagOutline`), a flagged pin's dot
  (`Port::color`), and a wire whose endpoint pin is flagged (`NetWire::color`
  inherits the pin's color); hovering an instance box shows the same tooltip
  as the tree.
- `DiagnosisView` — the flat list, independent of what's currently expanded/
  loaded in the tree or schematic.

Path matching convention: `DiagnosisItem::pathKey()` (slash-joined instance
names, root excluded) must match `NetlistTreeInstanceNode::getPathKey()` and
`EquipotentialView`'s instance-item keys — all three are built the same way,
from instance *names*, not the provider's numeric `child_id`s (those aren't
stable inputs for an external tool like kepler-formal to reference).

### VSCode integration

`.vscode/settings.json` locks `cmake-tools` to preset mode
(`native-debug`), pointing IntelliSense at
`../naja-schematic-build/native-debug/compile_commands.json`.
