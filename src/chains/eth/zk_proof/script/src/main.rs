use clap::Parser;
use eth_sync_common::merkle::create_root_hash;
use eth_sync_common::{ProofData, RecursionData, SP1GuestInput, VerificationOutput};
use k256::elliptic_curve::sec1::ToEncodedPoint;
use k256::SecretKey;
use sha2::{Digest, Sha256};
use sha3::{Digest as Sha3Digest, Keccak256};
use sp1_sdk::{HashableKey, ProverClient, SP1ProofWithPublicValues, SP1Stdin, SP1VerifyingKey};
use std::fs::File;
use std::io::Read;
use std::time::Instant;

const PROOF_OFFSET: usize = 49358;

#[derive(Parser, Debug)]
#[clap(author, version, about, long_about = None)]
struct Args {
    #[clap(long)]
    execute: bool,

    #[clap(long)]
    prove: bool,

    #[clap(long)]
    groth16: bool,

    #[clap(long, default_value = "zk_input.ssz")]
    input_file: String,

    #[clap(long)]
    prev_proof: Option<String>,

    #[clap(long)]
    prev_vk: Option<String>,
}

fn read_proof_data(filename: &str) -> ProofData {
    let mut file = File::open(filename).expect("Failed to open file");
    let mut buffer = Vec::new();
    file.read_to_end(&mut buffer).expect("Failed to read file");

    let current_keys_offset = 18;
    let current_keys_len = 512 * 48;
    let current_keys = buffer[current_keys_offset..current_keys_offset + current_keys_len].to_vec();

    let new_keys_offset = current_keys_offset + current_keys_len;
    let new_keys_len = 512 * 48;
    let new_keys = buffer[new_keys_offset..new_keys_offset + new_keys_len].to_vec();

    let sig_bits_offset = new_keys_offset + new_keys_len;
    let sig_bits_len = 64;
    let signature_bits = buffer[sig_bits_offset..sig_bits_offset + sig_bits_len].to_vec();

    let sig_offset = sig_bits_offset + sig_bits_len;
    let sig_len = 96;
    let signature = buffer[sig_offset..sig_offset + sig_len].to_vec();

    // gidx is 8 bytes in C (get_uint64_le)
    let gidx_offset = sig_offset + sig_len;
    let gidx_bytes: [u8; 8] = buffer[gidx_offset..gidx_offset + 8].try_into().unwrap();
    let gidx = u64::from_le_bytes(gidx_bytes) as u32;

    let slot_offset = gidx_offset + 8;
    let slot_bytes: [u8; 8] = buffer[slot_offset..slot_offset + 8].try_into().unwrap();

    let proposer_offset = slot_offset + 8;
    let proposer_bytes: [u8; 8] = buffer[proposer_offset..proposer_offset + 8]
        .try_into()
        .unwrap();

    let proof_offset = PROOF_OFFSET;
    if proof_offset >= buffer.len() {
        panic!("Buffer too short for proof offset");
    }
    let proof_len = buffer.len() - proof_offset - 1;
    let proof = buffer[proof_offset..proof_offset + proof_len].to_vec();

    // Calculate roots
    let mut current_keys_root = [0u8; 32];
    create_root_hash(&current_keys, &mut current_keys_root);

    let mut next_keys_root = [0u8; 32];
    create_root_hash(&new_keys, &mut next_keys_root);

    let next_period = (u64::from_le_bytes(slot_bytes) >> 13) + 1;

    ProofData {
        current_keys_root,
        next_keys_root,
        next_period,
        current_keys,
        signature_bits,
        signature,
        slot_bytes,
        proposer_bytes,
        proof,
        gidx,
    }
}

#[tokio::main]
async fn main() {
    println!("Starting eth-sync-script...");
    let args = Args::parse();
    let skip_local_verify = std::env::var("SP1_SKIP_VERIFY").is_ok();

    log_network_identity();
    let client = ProverClient::from_env();
    let mut stdin = SP1Stdin::new();

    println!("Reading proof data from {}", args.input_file);
    let proof_data = read_proof_data(&args.input_file);

    // Build Recursion Data
    let mut recursion_data = None;

    if let Some(prev_proof_path) = args.prev_proof {
        let prev_vk_path = args
            .prev_vk
            .expect("prev-vk is required when prev-proof is provided");

        println!("Loading previous proof from {}", prev_proof_path);
        let proof_file = File::open(&prev_proof_path).expect("Failed to open prev proof");
        let proof: SP1ProofWithPublicValues =
            bincode::deserialize_from(proof_file).expect("Failed to deserialize proof");

        println!("Loading previous VK from {}", prev_vk_path);
        let vk_file = File::open(&prev_vk_path).expect("Failed to open prev vk");
        let vk: SP1VerifyingKey =
            bincode::deserialize_from(vk_file).expect("Failed to deserialize vk");

        // Extract public values digest (SHA256)
        let mut hasher = Sha256::new();
        hasher.update(proof.public_values.as_slice());
        let hash_result = hasher.finalize();

        // Use direct bytes for digest
        let digest: [u8; 32] = hash_result.into();

        // Write proof to stdin for recursion
        // Extract the inner compressed proof (SP1ReduceProof)
        let compressed_proof = match proof.proof {
            sp1_sdk::SP1Proof::Compressed(p) => p,
            _ => panic!("Previous proof must be a Compressed proof for recursion!"),
        };

        stdin.write_proof(*compressed_proof, vk.vk.clone());

        recursion_data = Some(RecursionData {
            vkey_hash: vk.vk.hash_u32(),
            public_values_digest: digest,
            public_values: proof.public_values.to_vec(),
        });

        println!("Recursion enabled. Validating chain from prev period.");
    }

    // Write the wrapped input
    let guest_input = SP1GuestInput {
        proof_data,
        recursion_data,
    };
    stdin.write(&guest_input);

    let elf_path = std::env::var("ELF_PATH").unwrap_or_else(|_| {
        "../program/target/riscv32im-succinct-zkvm-elf/release/eth-sync-program".to_string()
    });
    println!("Loading ELF from: {}", elf_path);
    let elf_bytes = std::fs::read(&elf_path).expect(&format!("Failed to read ELF at {}", elf_path));
    let elf = elf_bytes.as_slice();

    if args.execute {
        println!("Executing program...");
        let start = Instant::now();
        let (mut output, report) = client.execute(elf, &stdin).run().unwrap();
        println!("Execution finished in {:?}", start.elapsed());
        println!("Cycles: {}", report.total_instruction_count());

        let result: VerificationOutput = output.read();
        println!("Result: Valid transition to period {}", result.next_period);
        println!("Next Keys Root: {}", hex::encode(result.next_keys_root));

        // Save Public Values
        let public_values_path =
            std::env::var("PUBLIC_VALUES_FILE").unwrap_or("public_values.bin".to_string());
        let mut pv_bytes = Vec::new();
        pv_bytes.extend_from_slice(&result.next_keys_root);
        pv_bytes.extend_from_slice(&result.next_period.to_le_bytes());
        std::fs::write(&public_values_path, &pv_bytes).expect("Failed to save public values");
        println!("Saved public values to {}", public_values_path);
    } else {
        println!("Generating proof...");
        let (pk, vk) = client.setup(elf);

        let start = Instant::now();

        if args.groth16 {
            println!("Generating Groth16 proof (requires Docker if local)...");

            // 1. Generate Compressed Proof first (needed for recursion chain)
            println!("1. Generating Compressed Proof (for recursion)...");
            let compressed = client.prove(&pk, &stdin).compressed().run().unwrap();

            // Save Compressed Proof
            let proof_output =
                std::env::var("PROOF_OUTPUT_FILE").unwrap_or("proof_groth16.bin".to_string());
            // derive compressed path or use explicit env var
            let compressed_path = std::env::var("PROOF_COMPRESSED_OUTPUT_FILE")
                .unwrap_or_else(|_| proof_output.replace("_groth16.bin", ".bin"));

            println!("Saving Compressed Proof to {}", compressed_path);
            compressed
                .save(&compressed_path)
                .expect("Failed to save compressed proof");

            // Save VK for Compressed (needed for next step input)
            let vk_output = std::env::var("VK_OUTPUT_FILE").unwrap_or("vk_groth16.bin".to_string());
            let vk_compressed_path = std::env::var("VK_COMPRESSED_OUTPUT_FILE")
                .unwrap_or_else(|_| vk_output.replace("_groth16.bin", ".bin"));

            let vk_bytes = bincode::serialize(&vk).expect("Failed to serialize VK");
            std::fs::write(&vk_compressed_path, vk_bytes).expect("Failed to save VK");

            // 2. Wrap in Groth16
            println!("2. Wrapping in Groth16...");
            // We verify the same input again, but requesting Groth16 mode.
            // SP1 should reuse cached artifacts for the core proof.
            let proof = client.prove(&pk, &stdin).groth16().run().unwrap();

            println!(
                "Groth16 Proof generated successfully in {:?}",
                start.elapsed()
            );

            // Save SP1 proof wrapper
            println!("Saving SP1 Groth16 proof to {}", proof_output);
            proof.save(&proof_output).expect("Failed to save proof");

            // Save RAW bytes for C-Verifier
            let raw_proof = proof.bytes();
            println!("Raw Proof size: {} bytes", raw_proof.len());
            let raw_output = std::env::var("PROOF_RAW_FILE").unwrap_or("proof_raw.bin".to_string());
            println!("Saving raw Groth16 proof bytes to {}", raw_output);
            std::fs::write(&raw_output, &raw_proof).expect("Failed to save raw proof");

            // Save Public Values (DIRECT from proof to be safe)
            let public_values_path =
                std::env::var("PUBLIC_VALUES_FILE").unwrap_or("public_values.bin".to_string());
            println!("Saving raw public values to {}", public_values_path);
            std::fs::write(&public_values_path, &proof.public_values.as_slice())
                .expect("Failed to save public values");

            // Save VK (Program Hash) for Groth16 mode
            println!("Saving VK to {}", vk_output);
            let vk_bytes = bincode::serialize(&vk).expect("Failed to serialize VK");
            std::fs::write(&vk_output, vk_bytes).expect("Failed to save VK");

            if skip_local_verify {
                println!("Skipping local verification (SP1_SKIP_VERIFY is set).");
            } else {
                client.verify(&proof, &vk).expect("Verification failed");
                println!("Proof verified successfully.");
            }
        } else {
            println!("Generating Core/Compressed proof (default)...");
            let proof = client.prove(&pk, &stdin).compressed().run().unwrap();
            println!("Proof generated successfully in {:?}", start.elapsed());

            let proof_output =
                std::env::var("PROOF_OUTPUT_FILE").unwrap_or("proof.bin".to_string());
            println!("Saving proof to {}", proof_output);
            proof.save(&proof_output).expect("Failed to save proof");

            let vk_output = std::env::var("VK_OUTPUT_FILE").unwrap_or("vk.bin".to_string());
            println!("Saving VK to {}", vk_output);
            let vk_bytes = bincode::serialize(&vk).expect("Failed to serialize VK");
            std::fs::write(&vk_output, vk_bytes).expect("Failed to save VK");

            if skip_local_verify {
                println!("Skipping local verification (SP1_SKIP_VERIFY is set).");
            } else {
                client.verify(&proof, &vk).expect("Verification failed");
                println!("Proof verified successfully.");
            }
        }
    }

    // Best-effort: update a balance file for monitoring after execution/proving.
    // This reflects remaining credits after this run when using SP1 prover network.
    update_network_balance_file().await;
}

fn log_network_identity() {
    let prover_mode = std::env::var("SP1_PROVER").unwrap_or_default();
    if prover_mode != "network" {
        return;
    }

    match get_network_private_key() {
        Some(pk) => match derive_eth_address(&pk) {
            Some(addr) => println!("SP1 Network account: {}", addr),
            None => eprintln!(
                "⚠️  Unable to derive SP1 network account address from provided private key"
            ),
        },
        None => eprintln!(
            "⚠️  SP1 network mode enabled but SP1_PRIVATE_KEY/NETWORK_PRIVATE_KEY is missing"
        ),
    }
}

fn get_network_private_key() -> Option<String> {
    std::env::var("SP1_PRIVATE_KEY")
        .or_else(|_| std::env::var("NETWORK_PRIVATE_KEY"))
        .ok()
}

fn derive_eth_address(private_key_hex: &str) -> Option<String> {
    let key_clean = private_key_hex.trim();
    let key_hex = key_clean.strip_prefix("0x").unwrap_or(key_clean);
    let key_bytes = hex::decode(key_hex).ok()?;
    if key_bytes.len() != 32 {
        return None;
    }
    let secret = SecretKey::from_slice(&key_bytes).ok()?;
    let verifying_key = secret.public_key();
    let encoded = verifying_key.to_encoded_point(false);
    let pub_bytes = encoded.as_bytes();
    if pub_bytes.len() != 65 {
        return None;
    }
    let hash = <Keccak256 as Sha3Digest>::digest(&pub_bytes[1..]);
    let address = &hash[hash.len() - 20..];
    Some(format!("0x{}", hex::encode(address)))
}

fn write_atomic_text(path: &str, contents: &str) -> std::io::Result<()> {
    use std::io::Write;
    use std::path::Path;

    let p = Path::new(path);
    if let Some(parent) = p.parent() {
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent)?;
        }
    }

    let tmp_path = format!("{path}.tmp");
    {
        let mut f = std::fs::File::create(&tmp_path)?;
        f.write_all(contents.as_bytes())?;
        f.sync_all().ok(); // best-effort
    }

    // On Windows, rename fails if destination exists.
    if std::fs::metadata(p).is_ok() {
        let _ = std::fs::remove_file(p);
    }
    std::fs::rename(&tmp_path, p)?;
    Ok(())
}

async fn update_network_balance_file() {
    let prover_mode = std::env::var("SP1_PROVER").unwrap_or_default();
    if prover_mode != "network" {
        return;
    }

    // If unset, do nothing (opt-in, so we don't write random files by default).
    let balance_file = match std::env::var("SP1_BALANCE_FILE") {
        Ok(p) if !p.trim().is_empty() => p,
        _ => return,
    };

    // Best-effort: derive and print address, then fetch balance via network RPC.
    let addr = get_network_private_key()
        .as_deref()
        .and_then(derive_eth_address);

    // sp1-sdk is built with `features = ["network"]` in this workspace, so this is always available.
    let prover = ProverClient::builder().network().build();
    match prover.get_balance().await {
        Ok(balance) => {
            if let Some(a) = &addr {
                println!("SP1 Network account: {}", a);
            }
            println!("SP1 Network balance: {}", balance);

            // File format: decimal credits as a single line.
            let _ = write_atomic_text(&balance_file, &format!("{balance}\n"));
        }
        Err(err) => {
            eprintln!("⚠️  Unable to fetch SP1 network balance: {err}");
        }
    }
}
