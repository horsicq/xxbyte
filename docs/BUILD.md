# Building xxbyte

Requires CMake 3.20+, a C11 compiler and the sibling `xxfclib` source tree.

Run from the `xxbyte` source directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DXXFC_BUILD_PROFILE=bytecode
cmake --build build --config Release --parallel
```

The `bytecode` profile builds only the xxfclib dependencies xxbyte needs.
Shared or CRT-free builds require `-DXXFC_BUILD_PROFILE=full` instead.

When the checkout includes `tests/`, run:

```sh
ctest --test-dir build -C Release --output-on-failure
```

Set `XXBYTE_XXFCLIB_SOURCE_DIR` for another dependency location. All decoder
families are enabled by default; select a subset with, for example,
`-DXXBYTE_FAMILIES="dotnet;dex;wasm"`. `XXBYTE_BUILD_CLI` and
`XXBYTE_BUILD_TESTS` control the console tool and tests when their `tools/`
and `tests/` source directories are present.

See [README](../README.md) for the API and usage.
