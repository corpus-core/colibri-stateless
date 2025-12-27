use sha2::{Digest, Sha256};
use std::env;

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        println!("Usage: debug_digest <file>");
        return;
    }
    let bytes = std::fs::read(&args[1]).unwrap();

    let mut hasher = Sha256::new();
    hasher.update(&bytes);
    let hash = hasher.finalize();
    println!("SHA256: {:?}", hex::encode(hash));

    let mut masked = hash.clone();
    masked[0] &= 0x1f; // Keep last 5 bits (00011111)
    println!("Masked: {:?}", hex::encode(masked));
}
