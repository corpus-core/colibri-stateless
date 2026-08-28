// Build script for the `colibri-stateless` crate.
//
// The crate needs a set of native static libraries produced from the C
// code in `bindings/colibri.c` + `bindings/colibri_common.c` and the
// core Colibri libraries (verifier, prover, chain modules, third-party
// crypto). This script covers three scenarios:
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
        "bindings/colibri.c",
        "bindings/colibri_common.c",
        "bindings/colibri.h",
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

    let repo_root = manifest_dir
        .parent()
        .and_then(Path::parent)
        .map(Path::to_path_buf);
    if let Some(root) = repo_root.as_ref() {
        if root.join("CMakeLists.txt").exists() && root.join("bindings/colibri.c").exists() {
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

    // Compile `bindings/colibri.c` + `colibri_common.c` here so the
    // `.o` files live inside the crate's build output (avoids polluting
    // the shared CMake tree). The archive we produce is called
    // `libcolibri_wrapper.a` and is linked BEFORE all the deep
    // dependencies so that `c4_create_*_ctx` etc. resolve into our
    // wrapper first.
    let wrapper_out = compile_wrapper(repo_root, &dst);
    let mut search_paths = vec![wrapper_out];

    // Now discover every static archive under the build dir. We link
    // them all -- macOS/BSD `ld` and GNU `ld --start-group` both
    // handle the resulting order-independence for us.
    let mut static_libs: Vec<String> = vec!["colibri_wrapper".into()];
    let archives = find_static_archives(&build_dir, target);
    for archive in archives {
        let name = archive_lib_name(&archive, target);
        if name == "colibri_wrapper" {
            continue;
        }
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

/// Compile the two bindings wrappers with the `cc` crate. Returns the
/// directory containing the resulting `libcolibri_wrapper.a`.
fn compile_wrapper(repo_root: &Path, dst: &Path) -> PathBuf {
    let mut build = cc::Build::new();
    build
        .file(repo_root.join("bindings/colibri.c"))
        .file(repo_root.join("bindings/colibri_common.c"))
        .include(repo_root.join("bindings"))
        .include(repo_root.join("src/util"))
        .include(repo_root.join("src/prover"))
        .include(repo_root.join("src/verifier"))
        .include(repo_root.join("src/chains"))
        .include(repo_root.join("src/chains/eth"))
        .include(repo_root.join("src/chains/eth/ssz"))
        .include(repo_root.join("src/chains/eth/verifier"))
        .include(repo_root.join("libs/crypto"))
        // The generated dispatcher headers `verifiers.h` / `provers.h`
        // and `version.h` live in the CMake build directory root.
        .include(dst.join("build"));
    // Match core defines used by the CMake build.
    build.define("CHAIN_ETH", None);
    build.define("CHAIN_OP", None);
    build.define("VERIFIER", None);
    build.define("PROVER", None);
    // Must match CMake `option(PROVER_CACHE)` (default ON). The wrapper itself
    // no longer calls into the cache, but keep the define consistent so any
    // `#ifdef PROVER_CACHE` in the bindings sources agrees with libprover.a.
    build.define("PROVER_CACHE", None);
    build.compile("colibri_wrapper");
    // `cc` places the archive into $OUT_DIR by default.
    PathBuf::from(env::var("OUT_DIR").unwrap())
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
    } else if target.contains("linux") || target.contains("unknown-linux") {
        println!("cargo:rustc-link-lib=stdc++");
        println!("cargo:rustc-link-lib=m");
    }
}
