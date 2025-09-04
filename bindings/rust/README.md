Colibri Rust Bindings

Rust-Bindings für die Colibri C-Bibliothek. Baut die C-Bibliothek via CMake (falls `COLIBRI_LIB_DIR` nicht gesetzt) und bietet eine asynchrone High-Level-API.

Build

- Systemvoraussetzungen: CMake, C-Toolchain
- Optional: Setze `COLIBRI_LIB_DIR` auf das Verzeichnis, das `libc4.*` enthält. Sonst baut `build.rs` die Lib selbst.

```bash
cd bindings/rust
cargo build
```

Tests (Integration)

Nutzen die Dateien unter `test/data`. Tests laufen seriell (globaler C-Storage).

```bash
cargo test -- --test-threads=1
```

Hinweise:
- Tests werden dynamisch aus `test/data/*/test.json` generiert und erscheinen einzeln im Report.
- Tests mit `requires_chain_store: true` werden derzeit automatisch übersprungen [[memory:5023876]].

WASM (Browser/wasm32)

- Dieses Crate ist für native Targets gedacht. Für wasm32 empfiehlt sich die bestehende Emscripten/TS-Binding unter `bindings/emscripten`.
- Falls dennoch wasm32 benötigt wird, müssen die C-Funktionen per `wasm-bindgen`-kompatibler Toolchain bereitgestellt werden (z.B. via Emscripten) und das Netzwerksubsystem (HTTP) über JS-Brücke laufen. In diesem Crate ist auf `wasm32` das direkte HTTP-Handling deaktiviert.


