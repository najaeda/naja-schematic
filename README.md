# naja-schematic

## Building

Clone the repo

Install emscripten.


```bash
cd naja-schematic
mkdir build-wasm
cd build-wasm
emcmake cmake ..
cmake --build . 

```

## Launching
```bash
emrun --port 8080 naja-schematic.html
```

## Native standalone

```bash
cmake --preset native-debug   # or native-release
cmake --build --preset native-debug
```

Opens a design directly from the command line, optionally pre-loaded with a
diagnosis (a `diagnosis_response`-shaped JSON: a `{"items": [...]}` object,
or a bare array — see `CLAUDE.md`'s "Wire protocol" section for the item
shape):

```bash
naja-schematic-standalone design.sv --diagnosis diagnosis.json
```

`<design>` and `--diagnosis <path>` are both optional and order-independent.
Without `--diagnosis`, use File > Load Diagnosis JSON... in the app instead.

