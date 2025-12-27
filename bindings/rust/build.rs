use std::env;
use std::path::PathBuf;

fn main() {
    // Simple build script that uses a pre-built combined library
    // Run build.sh first to create the combined library

    let project_root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .to_path_buf();

    // Compile the colibri.c wrapper
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

    // Link the combined library
    println!("cargo:rustc-link-search=native=target/colibri");
    println!("cargo:rustc-link-lib=static=colibri_combined");

    // Platform-specific system libraries
    let target = env::var("TARGET").unwrap();

    if target.contains("apple") {
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
        println!("cargo:rustc-link-lib=framework=Security");
        println!("cargo:rustc-link-lib=c++");
    } else if target.contains("linux") {
        println!("cargo:rustc-link-lib=dylib=ssl");
        println!("cargo:rustc-link-lib=dylib=crypto");
        println!("cargo:rustc-link-lib=stdc++");
    }
}
