//! `fetchcontent-deps.txt` lists the dependencies whose
//! `FETCHCONTENT_SOURCE_DIR_<NAME>` override `build.rs` passes to CMake
//! (and reruns on). It must name exactly the `FetchContent_Declare` calls
//! of the C build, so a new dependency cannot miss its override.

use std::collections::BTreeSet;
use std::fs;
use std::path::{Path, PathBuf};

fn listed() -> BTreeSet<String> {
    include_str!("../fetchcontent-deps.txt")
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .map(String::from)
        .collect()
}

/// Upper-cased names of every `FetchContent_Declare(<name> ...)` in `text`.
fn declared_in(text: &str, out: &mut BTreeSet<String>) {
    let mut remaining = text;
    while let Some(pos) = remaining.find("FetchContent_Declare(") {
        remaining = &remaining[pos + "FetchContent_Declare(".len()..];
        let name: String = remaining
            .trim_start()
            .chars()
            .take_while(|c| c.is_ascii_alphanumeric() || *c == '_')
            .collect();
        assert!(!name.is_empty(), "FetchContent_Declare without a name");
        out.insert(name.to_ascii_uppercase());
    }
}

fn cmake_files(repo: &Path) -> Vec<PathBuf> {
    let mut files = vec![repo.join("test/CMakeLists.txt")];
    for entry in fs::read_dir(repo.join("libs")).expect("libs/").flatten() {
        let file = entry.path().join("CMakeLists.txt");
        if file.is_file() {
            files.push(file);
        }
    }
    files
}

#[test]
fn list_matches_the_cmake_declarations() {
    let repo = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    if !repo.join("libs").is_dir() {
        eprintln!("libs/ not found -- crate is out of tree, skipping");
        return;
    }
    let mut declared = BTreeSet::new();
    for file in cmake_files(&repo) {
        declared_in(
            &fs::read_to_string(&file).expect("CMakeLists.txt"),
            &mut declared,
        );
    }
    assert_eq!(
        listed(),
        declared,
        "fetchcontent-deps.txt must list every FetchContent_Declare in libs/ and test/"
    );
}
