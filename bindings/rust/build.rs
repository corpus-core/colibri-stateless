use std::env;
use std::path::PathBuf;

fn main() {
    // Build script that uses a pre-built combined library
    // Run build.sh first to create the combined library
    // For cross-compilation: ./build.sh --target <target>

    let target = env::var("TARGET").unwrap();
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());

    let project_root = manifest_dir
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .to_path_buf();

    // Compile the colibri.c wrapper
    // The cc crate automatically handles cross-compilation
    cc::Build::new()
        .file(project_root.join("bindings/colibri.c"))
        .include(project_root.join("bindings"))
        .include(project_root.join("src/util"))
        .include(project_root.join("src/prover"))
        .include(project_root.join("src/verifier"))
        .include(project_root.join("src/chains/eth"))
        .include(project_root.join("src/chains/eth/ssz"))
        .include(project_root.join("src/chains/eth/verifier"))
        .include(project_root.join("libs/crypto"))
        .compile("colibri_wrapper");

    // Link the combined library from target-specific directory
    // Library should be at: target/colibri/<target>/libcolibri_combined.a
    let lib_dir = manifest_dir.join("target/colibri").join(&target);

    // Check if target-specific library exists, fall back to legacy path
    let lib_path = if lib_dir.join("libcolibri_combined.a").exists() {
        lib_dir
    } else {
        // Fall back to legacy non-target-specific path for backwards compatibility
        manifest_dir.join("target/colibri")
    };

    println!("cargo:rustc-link-search=native={}", lib_path.display());
    println!("cargo:rustc-link-lib=static=colibri_combined");

    // Re-run build script if library changes
    println!("cargo:rerun-if-changed={}/libcolibri_combined.a", lib_path.display());

    // Platform-specific system libraries
    if target.contains("apple-darwin") {
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
        println!("cargo:rustc-link-lib=framework=Security");
        println!("cargo:rustc-link-lib=c++");
    } else if target.contains("apple-ios") {
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
        println!("cargo:rustc-link-lib=framework=Security");
        println!("cargo:rustc-link-lib=c++");
    } else if target.contains("linux") {
        println!("cargo:rustc-link-lib=dylib=ssl");
        println!("cargo:rustc-link-lib=dylib=crypto");
        println!("cargo:rustc-link-lib=stdc++");
    } else if target.contains("android") {
        println!("cargo:rustc-link-lib=c++_shared");
    }
}
