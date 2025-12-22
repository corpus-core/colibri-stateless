use std::env;

fn main() {
    // User must set COLIBRI_LIB_DIR to point to the directory containing libcolibri
    if let Ok(lib_dir) = env::var("COLIBRI_LIB_DIR") {
        println!("cargo:rustc-link-search=native={}", lib_dir);
    } else {
        // Fallback to ../../build if not specified
        println!("cargo:rustc-link-search=native=../../build");
    }

    // Allow override of link type (static/dylib), default to static
    let link_type = env::var("COLIBRI_LINK_TYPE").unwrap_or_else(|_| "static".to_string());
    println!("cargo:rustc-link-lib={}=colibri", link_type);

    // Platform-specific system libraries
    let target = env::var("TARGET").unwrap();

    if target.contains("apple") {
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
        println!("cargo:rustc-link-lib=framework=Security");
    } else if target.contains("linux") {
        println!("cargo:rustc-link-lib=dylib=ssl");
        println!("cargo:rustc-link-lib=dylib=crypto");
    } else if target.contains("android") {
        println!("cargo:rustc-link-lib=dylib=log");
    }
}