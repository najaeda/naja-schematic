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

