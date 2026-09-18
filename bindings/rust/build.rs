// Build script for the `colibri-stateless` crate.
//
// The crate needs a set of native static libraries produced from the C
// code in `src/api/` (`api` + `api_ffi`) and the core Colibri libraries
// (verifier, prover, chain modules, third-party crypto). This script
// covers three scenarios:
//
// 1. `COLIBRI_LIB_DIR` env var: skip building and link statically
//    against the archives already present in that directory. Used by
//    CI when the native library is produced by a separate job.
// 2. Dev mode: when the crate is inside the upstream repository (i.e.
//    the parent directory contains `CMakeLists.txt`), invoke CMake to
//    build a curated set of targets, then either merge them into a
//    single archive or emit multiple `-l static=<name>` directives.
// 3. Published mode: when installed from crates.io the C sources are
//    missing; the script attempts to download a prebuilt archive from
//    GitHub Releases matching the crate version.
//
// `DOCS_RS=1` and `COLIBRI_SKIP_NATIVE=1` skip everything (used by
// rustdoc and CI systems that build the native library separately).
// `COLIBRI_CMAKE_BUILD_DIR` relocates the CMake tree of scenario 2.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    println!("cargo:rerun-if-env-changed=COLIBRI_LIB_DIR");
    println!("cargo:rerun-if-env-changed=COLIBRI_CMAKE_BUILD_DIR");
    println!("cargo:rerun-if-env-changed=COLIBRI_SKIP_NATIVE");
    println!("cargo:rerun-if-env-changed=DOCS_RS");
    println!("cargo:rerun-if-env-changed=WASI_SDK_PATH");
    println!("cargo:rerun-if-changed=build.rs");

    let target = env::var("TARGET").expect("TARGET env var must be set by cargo");
    let manifest_dir = PathBuf::from(
        env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR must be set by cargo"),
    );

    if env::var("DOCS_RS").is_ok() {
        return;
    }

    if env::var("COLIBRI_SKIP_NATIVE").ok().as_deref() == Some("1") {
        println!(
            "cargo:warning=COLIBRI_SKIP_NATIVE=1 -- skipping native lib build/link. \
             Provide -lc4 (or the individual archives) yourself."
        );
        return;
    }

    let link = resolve_link_setup(&manifest_dir, &target);

    for search in &link.search_paths {
        println!("cargo:rustc-link-search=native={}", search.display());
    }
    for lib in &link.static_libs {
        println!("cargo:rustc-link-lib=static={lib}");
    }

    link_system_libs(&target);
}

/// Re-run the build script when the C sources change.
///
/// Deliberately does **not** watch the CMake output directory: that
/// lives inside the repository, so watching it would make every build
/// dirty the inputs of the next one and Cargo would rebuild forever.
fn emit_source_rerun_hints(repo_root: &Path) {
    for rel in [
        "CMakeLists.txt",
        "src/api/colibri.c",
        "src/api/colibri_common.c",
        "src/api/colibri.h",
        "src",
        "libs/crypto",
    ] {
        let path = repo_root.join(rel);
        if path.exists() {
            println!("cargo:rerun-if-changed={}", path.display());
        }
    }
}

struct LinkSetup {
    search_paths: Vec<PathBuf>,
    static_libs: Vec<String>,
}

/// Build a `LinkSetup` that links every static archive found (recursively)
/// under `dir`.
fn link_setup_from_dir(dir: &Path, target: &str) -> LinkSetup {
    let mut static_libs = Vec::new();
    for archive in find_static_archives(dir, target) {
        let name = archive_lib_name(&archive, target);
        if !static_libs.contains(&name) {
            static_libs.push(name);
        }
    }
    LinkSetup {
        search_paths: vec![dir.to_path_buf()],
        static_libs,
    }
}

fn resolve_link_setup(manifest_dir: &Path, target: &str) -> LinkSetup {
    if let Ok(dir) = env::var("COLIBRI_LIB_DIR") {
        let dir = PathBuf::from(dir);
        assert!(
            dir.exists(),
            "COLIBRI_LIB_DIR ({}) does not exist",
            dir.display(),
        );
        // Link every archive found in the directory -- works both for
        // a single combined `libc4.a` and for the multi-archive layout
        // shipped in the release assets.
        let setup = link_setup_from_dir(&dir, target);
        assert!(
            !setup.static_libs.is_empty(),
            "COLIBRI_LIB_DIR ({}) contains no static libraries",
            dir.display(),
        );
        return setup;
    }

    // The C core cannot be linked against rustc's `wasm32-unknown-unknown`
    // target (no libc, no C++ runtime, entirely different ABI). We do
    // support `wasm32-wasip1` via the wasi-sdk: fall through to the
    // regular CMake / prebuilt paths below. Fail early for every other
    // wasm target with a pointer to the JS/TS binding.
    if (target.starts_with("wasm32") || target.starts_with("wasm64")) && target != "wasm32-wasip1" {
        panic!(
            "colibri-stateless: WebAssembly target `{target}` is not supported.\n\
             \n\
             Supported wasm target: `wasm32-wasip1` (WASI Preview 1, built via wasi-sdk).\n\
             \n\
             For browser / wasm-bindgen environments use the JS/TS binding instead:\n\
             https://www.npmjs.com/package/@corpus-core/colibri-stateless\n\
             \n\
             If you have static archives built for your wasm target,\n\
             point COLIBRI_LIB_DIR at them to override this."
        );
    }

    let repo_root = manifest_dir
        .parent()
        .and_then(Path::parent)
        .map(Path::to_path_buf);
    if let Some(root) = repo_root.as_ref() {
        if root.join("CMakeLists.txt").exists() && root.join("src/api/colibri.c").exists() {
            return build_with_cmake(root, target);
        }
    }

    if let Some(setup) = fetch_prebuilt(target) {
        return setup;
    }

    panic!(
        "colibri-stateless: cannot locate the native library.\n\
         \n\
         Set COLIBRI_LIB_DIR=<dir with the static archives> to point at\n\
         pre-built static libraries, or build from a git checkout so `build.rs` can\n\
         invoke CMake automatically. See bindings/rust/README.md for\n\
         details."
    );
}

/// Directory the CMake tree is configured and built in.
///
/// Deliberately **not** derived from `OUT_DIR`: that path contains the
/// profile plus a Cargo build hash (`.../release/build/colibri-stateless-<hash>/out`)
/// and pushes the deep `_deps/evmone_external-src/.git/modules/...`
/// paths of the vendored dependencies past Windows' 260-character
/// `MAX_PATH` limit, which makes the FetchContent submodule clone fail
/// with "Filename too long".
///
/// Override with `COLIBRI_CMAKE_BUILD_DIR` when the default still ends
/// up too long (e.g. a deeply nested checkout on Windows).
fn cmake_build_root(repo_root: &Path, target: &str) -> PathBuf {
    if let Ok(dir) = env::var("COLIBRI_CMAKE_BUILD_DIR") {
        return PathBuf::from(dir);
    }
    // `/build-*` is git-ignored at the repo root.
    repo_root.join("build-rust").join(target)
}

/// Invoke CMake in the repo checkout to produce the archives, then
/// link against every archive we need.
fn build_with_cmake(repo_root: &Path, target: &str) -> LinkSetup {
    // We deliberately do NOT use `COMBINED_STATIC_LIB` from the repo's
    // CMakeLists.txt: its dependency walk misses transitive static
    // libraries and produces an archive with only `verify.o` +
    // `prover.o`. Instead we build the top-level targets we need and
    // link every resulting `.a` file individually -- reliable and
    // avoids touching shared CMake logic.
    emit_source_rerun_hints(repo_root);
    let out_dir = cmake_build_root(repo_root, target);
    let mut cfg = cmake::Config::new(repo_root);
    cfg.out_dir(&out_dir)
        .define("CURL", "OFF")
        .define("CLI", "OFF")
        .define("HTTP_SERVER", "OFF")
        .define("CHAIN_OP", "ON")
        .define("CMAKE_BUILD_TYPE", "Release")
        .define("CMAKE_POSITION_INDEPENDENT_CODE", "ON")
        // `build` builds ALL_BUILD implicitly, which contains every
        // registered chain module plus the deps.
        .build_target("verifier");

    if target == "wasm32-wasip1" {
        configure_wasip1(&mut cfg);
    }

    let dst = cfg.build();
    let build_dir = dst.join("build");

    // We also need the prover / chain module archives that aren't
    // pulled in by "verifier" alone. A second build call reuses the
    // same build directory and just adds targets.
    let extra_targets = [
        "prover",
        "eth_verifier",
        "eth_prover",
        "op_verifier",
        "op_prover",
        "api_ffi",
    ];
    for tgt in extra_targets {
        // Use raw `cmake --build <dir> --target <t>` so we don't reset
        // build-system state. `--config` is required for multi-config
        // generators (Visual Studio, Xcode), which otherwise default to
        // Debug and would produce archives the rest of the build cannot
        // find.
        let ok = Command::new("cmake")
            .arg("--build")
            .arg(&build_dir)
            .arg("--config")
            .arg("Release")
            .arg("--target")
            .arg(tgt)
            .status()
            .map(|s| s.success())
            .unwrap_or(false);
        if !ok {
            println!("cargo:warning=Failed to build extra target {tgt}");
        }
    }

    // Discover every static archive under the build dir, including
    // `libapi.a` / `libapi_ffi.a` from `src/api`. We link them all --
    // macOS/BSD `ld` and GNU `ld --start-group` both handle the
    // resulting order-independence for us.
    let mut search_paths = Vec::new();
    let mut static_libs: Vec<String> = Vec::new();
    let archives = find_static_archives(&build_dir, target);
    for archive in archives {
        let name = archive_lib_name(&archive, target);
        let dir = archive
            .parent()
            .expect("archive path should have parent")
            .to_path_buf();
        if !search_paths.contains(&dir) {
            search_paths.push(dir);
        }
        if !static_libs.contains(&name) {
            static_libs.push(name);
        }
    }

    LinkSetup {
        search_paths,
        static_libs,
    }
}

fn find_static_archives(root: &Path, target: &str) -> Vec<PathBuf> {
    let mut out = Vec::new();
    if !root.exists() {
        return out;
    }
    walk_archives(root, target, &mut out);
    out
}

fn walk_archives(dir: &Path, target: &str, out: &mut Vec<PathBuf>) {
    let Ok(entries) = fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            walk_archives(&path, target, out);
        } else if is_static_archive(&path, target) {
            out.push(path);
        }
    }
}

/// Note the check is against `TARGET`, not a `cfg!` on the build
/// script itself -- the build script is compiled for the host, so
/// `cfg!(target_env = "msvc")` would report the wrong answer when
/// cross-compiling to or from Windows.
fn is_static_archive(p: &Path, target: &str) -> bool {
    match p.extension().and_then(|e| e.to_str()) {
        Some("a") => true,
        Some("lib") => target.contains("msvc"),
        _ => false,
    }
}

fn archive_lib_name(p: &Path, target: &str) -> String {
    let stem = p
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("")
        .to_string();
    // Unix archives are `lib<name>.a` and the linker wants `<name>`.
    // MSVC takes the file stem verbatim, so stripping a `lib` prefix
    // there would turn a genuine `libfoo.lib` into an unresolvable
    // `foo.lib`.
    if target.contains("msvc") {
        stem
    } else {
        stem.strip_prefix("lib").unwrap_or(&stem).to_string()
    }
}

/// Try to download a prebuilt static-library archive matching the
/// current crate version and target triple. Uses `curl` and `tar`
/// which are available on all Tier-1 platforms.
fn fetch_prebuilt(target: &str) -> Option<LinkSetup> {
    let version = env::var("CARGO_PKG_VERSION").ok()?;
    let out_dir = PathBuf::from(env::var("OUT_DIR").ok()?);
    let extract_dir = out_dir.join("prebuilt");

    let sentinel = extract_dir.join(".ok");
    if !sentinel.exists() {
        let asset = format!("colibri-native-{target}.tar.gz");
        let url = format!(
            "https://github.com/corpus-core/colibri-stateless/releases/download/v{version}/{asset}"
        );
        let archive_path = out_dir.join(&asset);

        if !download(&url, &archive_path) {
            eprintln!("cargo:warning=Could not download {url}");
            return None;
        }
        fs::create_dir_all(&extract_dir).ok()?;
        let ok = Command::new("tar")
            .arg("-xzf")
            .arg(&archive_path)
            .arg("-C")
            .arg(&extract_dir)
            .status()
            .map(|s| s.success())
            .unwrap_or(false);
        if !ok {
            eprintln!("cargo:warning=Extraction of {asset} failed");
            return None;
        }
        fs::write(&sentinel, format!("{version}\n")).ok()?;
    }

    // Prebuilt archives ship every `.a` / `.lib` under `lib/`. Link
    // them all so the CI archive layout doesn't have to know which
    // symbols downstream crates end up pulling in.
    let lib_dir = extract_dir.join("lib");
    let setup = link_setup_from_dir(&lib_dir, target);
    if setup.static_libs.is_empty() {
        eprintln!(
            "cargo:warning=Prebuilt archive contained no static libraries under {}",
            lib_dir.display()
        );
        return None;
    }
    Some(setup)
}

fn download(url: &str, dest: &Path) -> bool {
    let status = Command::new("curl")
        .args(["-fsSL", "-o"])
        .arg(dest)
        .arg(url)
        .status();
    matches!(status, Ok(s) if s.success()) && dest.exists()
}

/// Platform system libraries required by the C core (C++ runtime for
/// `evmone`, TLS is handled by rustls in reqwest).
fn link_system_libs(target: &str) {
    if target.contains("apple") {
        println!("cargo:rustc-link-lib=c++");
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
        println!("cargo:rustc-link-lib=framework=Security");
    } else if target.contains("msvc") {
        // The MSVC C++ runtime is pulled in automatically via the
        // `/DEFAULTLIB` directives that `cl` embeds into the object
        // files, so there is nothing to declare here. Both CMake (via
        // the `cmake` crate) and rustc default to the dynamic `/MD`
        // runtime, so the two halves agree.
    } else if target.contains("android") {
        println!("cargo:rustc-link-lib=c++_shared");
    } else if target == "wasm32-wasip1" {
        link_wasip1_cxx_runtime();
    } else if target.contains("linux") || target.contains("unknown-linux") {
        println!("cargo:rustc-link-lib=stdc++");
        println!("cargo:rustc-link-lib=m");
    }
}

/// Configure a CMake build for the `wasm32-wasip1` target using the
/// wasi-sdk toolchain file. Requires `WASI_SDK_PATH` to point at an
/// unpacked wasi-sdk release (see https://github.com/WebAssembly/wasi-sdk).
fn configure_wasip1(cfg: &mut cmake::Config) {
    let sdk = wasi_sdk_path();
    let toolchain = pick_wasip1_toolchain(&sdk);

    cfg.define("CMAKE_TOOLCHAIN_FILE", &toolchain)
        // WASI has no filesystem-agnostic path abstraction; disable the
        // simple file storage and rely on the always-available in-memory
        // one. Hosts wanting persistence must supply their own storage.
        .define("FILE_STORAGE", "OFF")
        .define("MEMORY_STORAGE", "ON")
        // PROVER_CACHE is a server-side optimisation with a sizeable
        // static footprint; not useful in a sandboxed wasm build.
        .define("PROVER_CACHE", "OFF")
        // wasm objects are inherently position-independent; -fPIC does
        // not apply and confuses some third-party build scripts (blst).
        .define("CMAKE_POSITION_INDEPENDENT_CODE", "OFF")
        // The SP1-based ZK proof host tool cannot cross-compile to wasm.
        .define("ETH_ZKPROOF", "OFF");
}

/// Locate the wasi-sdk that will be used both by CMake and for
/// discovering the sysroot at link time.
fn wasi_sdk_path() -> PathBuf {
    env::var("WASI_SDK_PATH").map(PathBuf::from).expect(
        "colibri-stateless: building for wasm32-wasip1 requires the wasi-sdk. \
             Set WASI_SDK_PATH to an unpacked wasi-sdk release \
             (see https://github.com/WebAssembly/wasi-sdk).",
    )
}

/// Pick the wasi-sdk CMake toolchain file for wasip1. wasi-sdk >= 22
/// ships `wasi-sdk-p1.cmake`; older releases only have `wasi-sdk.cmake`.
fn pick_wasip1_toolchain(sdk: &Path) -> PathBuf {
    let p1 = sdk.join("share/cmake/wasi-sdk-p1.cmake");
    if p1.exists() {
        return p1;
    }
    let generic = sdk.join("share/cmake/wasi-sdk.cmake");
    assert!(
        generic.exists(),
        "colibri-stateless: neither wasi-sdk-p1.cmake nor wasi-sdk.cmake found under {}. \
         Please install wasi-sdk >= 22.",
        sdk.display()
    );
    generic
}

/// Tell rustc where to find the C++ runtime for the wasm32-wasip1
/// target. evmone requires libc++/libc++abi; wasi-libc itself comes
/// from rustc's own wasi sysroot and MUST NOT be shadowed by the
/// wasi-sdk copy (rustc's crt1-command.o and libstd are compiled
/// against very specific wasi-libc symbol versions -- see e.g.
/// `__wasi_init_tp`, `pthread_join`, `pthread_detach`; if rust-lld
/// resolves `-l c` against the wasi-sdk archive first, the mismatch
/// surfaces as "undefined symbol" errors at link time).
///
/// The prebuilt release asset therefore ships `libc++.a` and
/// `libc++abi.a` inside `lib/` next to the Colibri archives and no
/// extra work is needed here. In dev mode we do the equivalent by
/// copying just those two archives into `OUT_DIR/wasi-cxx/` and
/// adding *only* that scratch directory to the rustc link search
/// path.
fn link_wasip1_cxx_runtime() {
    let Ok(sdk) = env::var("WASI_SDK_PATH") else {
        return;
    };
    let src_dir = PathBuf::from(sdk).join("share/wasi-sysroot/lib/wasm32-wasip1");
    if !src_dir.exists() {
        return;
    }
    let Some(out_dir) = env::var_os("OUT_DIR") else {
        // Should never happen inside a Cargo build script, but bail
        // out cleanly if it does.
        return;
    };
    let dst_dir = PathBuf::from(out_dir).join("wasi-cxx");
    if let Err(e) = fs::create_dir_all(&dst_dir) {
        eprintln!(
            "cargo:warning=Could not create {}: {e}",
            dst_dir.display()
        );
        return;
    }
    for lib in ["libc++.a", "libc++abi.a"] {
        let src = src_dir.join(lib);
        let dst = dst_dir.join(lib);
        if let Err(e) = fs::copy(&src, &dst) {
            eprintln!(
                "cargo:warning=Could not copy {} -> {}: {e}",
                src.display(),
                dst.display()
            );
            return;
        }
    }
    println!("cargo:rustc-link-search=native={}", dst_dir.display());
    println!("cargo:rustc-link-lib=static=c++");
    println!("cargo:rustc-link-lib=static=c++abi");
}
