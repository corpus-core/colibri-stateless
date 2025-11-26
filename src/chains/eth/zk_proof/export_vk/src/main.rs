use clap::Parser;
use num_bigint::BigUint;
use std::fs::File;
use std::io::{BufRead, BufReader, Write};
use std::path::PathBuf;
use regex::Regex;
use sp1_sdk::{HashableKey, ProverClient, Prover, SP1VerifyingKey};
use serde::Deserialize;

#[derive(Parser, Debug)]
#[clap(author, version, about, long_about = None)]
struct Args {
    /// Path to the Groth16Verifier.sol file
    #[clap(long)]
    solidity_path: PathBuf,

    /// Path to the ELF binary (to compute Program Hash)
    #[clap(long)]
    elf_path: PathBuf,

    /// Output header file path
    #[clap(long, default_value = "zk_verifier_constants.h")]
    output_path: PathBuf,
}

fn main() {
    let args = Args::parse();

    // 1. Compute Program Hash from ELF
    println!("Computing VK Hash from ELF: {:?}", args.elf_path);
    let elf_bytes = std::fs::read(&args.elf_path).expect("Failed to read ELF file");
    let client = ProverClient::builder().cpu().build();
    let (_, vk) = client.setup(&elf_bytes);
    
    // Get the hash as field element string
    let vk_hash_debug = vk.hash_bn254().to_string();
    println!("VK Hash Debug String: {}", vk_hash_debug);
    
    // Parse hex from "FFBn254Fr(0x...)"
    let re_hex = Regex::new(r"0x([0-9a-fA-F]+)").unwrap();
    let vk_hash = if let Some(caps) = re_hex.captures(&vk_hash_debug) {
        BigUint::parse_bytes(caps[1].as_bytes(), 16).expect("Invalid hex in hash")
    } else {
        // Try parsing as decimal if it's just numbers (fallback)
        BigUint::parse_bytes(vk_hash_debug.as_bytes(), 10).expect("Invalid format for hash")
    };

    // 2. Parse Solidity Constants
    let sol_file = File::open(&args.solidity_path).expect("Failed to open Solidity file");
    let reader = BufReader::new(sol_file);
    let mut content = String::new();
    for line in reader.lines() {
        content.push_str(&line.unwrap());
        content.push('\n');
    }

    let mut out = File::create(&args.output_path).expect("Failed to create output file");

    writeln!(out, "#ifndef ZK_VERIFIER_CONSTANTS_H").unwrap();
    writeln!(out, "#define ZK_VERIFIER_CONSTANTS_H").unwrap();
    writeln!(out, "").unwrap();
    writeln!(out, "#include <stdint.h>").unwrap();
    writeln!(out, "").unwrap();
    writeln!(out, "/* Extracted from Groth16Verifier.sol & ELF VK */").unwrap();
    writeln!(out, "").unwrap();

    // Regex to find constants: uint256 constant NAME = VALUE;
    let re = Regex::new(r"uint256\s+constant\s+(\w+)\s*=\s*([0-9x]+);").unwrap();

    let mut found_constants = std::collections::HashMap::new();

    for cap in re.captures_iter(&content) {
        let name = &cap[1];
        let value_str = &cap[2];
        
        let value = if value_str.starts_with("0x") {
             BigUint::parse_bytes(value_str.trim_start_matches("0x").as_bytes(), 16).expect("Invalid hex")
        } else {
             BigUint::parse_bytes(value_str.as_bytes(), 10).expect("Invalid decimal")
        };

        found_constants.insert(name.to_string(), value);
    }
    
    // Helper to get and write
    let get = |name: &str| -> BigUint {
        found_constants.get(name).expect(&format!("Constant {} not found", name)).clone()
    };

    let write_point_g1 = |out: &mut File, name: &str, x_name: &str, y_name: &str| {
        let x = get(x_name);
        let y = get(y_name);
        writeln!(out, "// {}", name).unwrap();
        write_bytes(out, &format!("{}_X", name), &x);
        write_bytes(out, &format!("{}_Y", name), &y);
    };
    
    let write_point_g2 = |out: &mut File, name: &str, prefix: &str| {
        let x0 = get(&format!("{}_X_0", prefix));
        let x1 = get(&format!("{}_X_1", prefix));
        let y0 = get(&format!("{}_Y_0", prefix));
        let y1 = get(&format!("{}_Y_1", prefix));
        writeln!(out, "// {}", name).unwrap();
        write_bytes(out, &format!("{}_X0", name), &x0);
        write_bytes(out, &format!("{}_X1", name), &x1);
        write_bytes(out, &format!("{}_Y0", name), &y0);
        write_bytes(out, &format!("{}_Y1", name), &y1);
    };

    // Write Program Hash
    writeln!(out, "// VK_PROGRAM_HASH (Digest of the Guest Program)").unwrap();
    write_bytes(&mut out, "VK_PROGRAM_HASH", &vk_hash);

    // Write Alpha
    write_point_g1(&mut out, "VK_ALPHA", "ALPHA_X", "ALPHA_Y");
    
    // Write Beta Neg
    write_point_g2(&mut out, "VK_BETA_NEG", "BETA_NEG");

    // Write Gamma Neg
    write_point_g2(&mut out, "VK_GAMMA_NEG", "GAMMA_NEG");

    // Write Delta Neg
    write_point_g2(&mut out, "VK_DELTA_NEG", "DELTA_NEG");
    
    // Write IC (Gamma ABC)
    // IC[0] = CONSTANT
    writeln!(out, "// VK_IC[0] (Constant Term)").unwrap();
    write_bytes(&mut out, "VK_IC0_X", &get("CONSTANT_X"));
    write_bytes(&mut out, "VK_IC0_Y", &get("CONSTANT_Y"));
    
    // IC[1] = PUB_0
    writeln!(out, "// VK_IC[1] (Program VKey Base)").unwrap();
    write_bytes(&mut out, "VK_IC1_X", &get("PUB_0_X"));
    write_bytes(&mut out, "VK_IC1_Y", &get("PUB_0_Y"));

    // IC[2] = PUB_1
    writeln!(out, "// VK_IC[2] (Public Values Digest Base)").unwrap();
    write_bytes(&mut out, "VK_IC2_X", &get("PUB_1_X"));
    write_bytes(&mut out, "VK_IC2_Y", &get("PUB_1_Y"));

    writeln!(out, "").unwrap();
    writeln!(out, "#endif // ZK_VERIFIER_CONSTANTS_H").unwrap();
    println!("Header generated at {:?}", args.output_path);
}

fn write_bytes(out: &mut File, name: &str, val: &BigUint) {
    let mut bytes = val.to_bytes_be();
    // Pad to 32 bytes
    let mut padded = vec![0u8; 32];
    if bytes.len() > 32 {
        panic!("Value {} is too large for 32 bytes", name);
    }
    let offset = 32 - bytes.len();
    padded[offset..].copy_from_slice(&bytes);
    
    writeln!(out, "const uint8_t {}[32] = {{", name).unwrap();
    write!(out, "    ").unwrap();
    for (i, b) in padded.iter().enumerate() {
        write!(out, "0x{:02x}", b).unwrap();
        if i < 31 { write!(out, ", ").unwrap(); }
    }
    writeln!(out, "\n}};").unwrap();
}
