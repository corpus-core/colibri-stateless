# Colibri Stateless — Node.js Native Addon

A self-contained N-API addon (`colibri_native.node`) that statically links the
full Colibri C library. In Node.js it replaces the WASM build and benefits from
the platform-optimized assembly in [blst](https://github.com/supranational/blst),
making BLS pairing (the dominant cost of proof verification) **~25-30x faster**
than the WASM fallback.

The addon is not published on its own: it is bundled as `prebuilds/<platform>-<arch>/colibri_native.node`
inside the npm package `@corpus-core/colibri-stateless`. The package's Node
entry point (`index.node.js`) loads the matching prebuild automatically and
falls back to WASM when no prebuild exists for the current platform.

## Supported prebuild targets

| Target | CI runner |
|--------|-----------|
| `linux-x64` | ubuntu-latest |
| `linux-arm64` | ubuntu-24.04-arm |
| `darwin-arm64` | macos-latest |
| `darwin-x64` | macos-15-intel |
| `win32-x64` | windows-latest |

## Building locally

```bash
cmake -S . -B build/node-addon -DCMAKE_BUILD_TYPE=Release -DNODE_ADDON=1
cmake --build build/node-addon --target colibri_native
# -> build/node-addon/node-addon/colibri_native.node
```

Only the [node-api-headers](https://github.com/nodejs/node-api-headers) are
required (fetched automatically by CMake); the N-API symbols are resolved from
the Node.js host process at load time, so the addon is independent of the Node
version (N-API version 8, Node >= 12.22).

## Environment variables (JS side)

| Variable | Effect |
|----------|--------|
| `C4_NATIVE_ADDON=<path>` | Load the addon from an explicit path (used by local dev/test builds) |
| `C4_FORCE_NATIVE=1` | Fail instead of falling back to WASM |
| `C4_DISABLE_NATIVE=1` | Skip the native addon entirely (always WASM) |
| `C4_DEBUG_NATIVE=1` | Log the reason when falling back to WASM |

## Testing

```bash
# build the addon (see above), build/refresh the TS output, then:
cd bindings/emscripten
npm run test:node:native   # runs the shared fixture suite against the native addon
```

## Architecture

- `src/addon.c` implements the same functional surface as the Emscripten
  wrapper (`bindings/emscripten/ems.c`), but with JS-friendly types
  (strings, `Uint8Array`, `BigInt`, externals) instead of WASM heap pointers.
- JSON status strings are serialized with `req_ptr_as_string=true` because
  native 64-bit pointers are not safely representable as JS numbers.
- The JS storage interface (`register_storage`) is bridged to the C
  `storage_plugin_t`; callbacks into JS are safe because the C core only runs
  within synchronous N-API calls.
- The TypeScript side selects the runtime in `bindings/emscripten/src/runtime_node.ts`
  (see `runtime.ts` for the runtime abstraction shared with WASM).
