fn main() {
    // build_program("../program"); // Disabled for manual build
    println!("cargo:rerun-if-changed=../program/src");
}
