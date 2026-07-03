use clap::Parser;
use eth_sync_common::merkle::create_root_hash;
use eth_sync_common::{ProofData, RecursionData, SP1GuestInput, VerificationOutput};
use k256::elliptic_curve::sec1::ToEncodedPoint;
use k256::SecretKey;
use sha2::{Digest, Sha256};
use sha3::{Digest as Sha3Digest, Keccak256};
use sp1_sdk::{
    EnvProver, ExecutionReport, HashableKey, Prover, ProverClient, SP1Proof,
    SP1ProofWithPublicValues, SP1ProvingKey, SP1PublicValues, SP1Stdin, SP1VerifyingKey,
    SP1_CIRCUIT_VERSION,
};
#[cfg(feature = "cuda")]
use sp1_sdk::CudaProver;
use sp1_stark::SP1ProverOpts;
use std::fs::File;
use std::io::Read;
use std::time::Instant;

const PROOF_OFFSET: usize = 49358;

/// Environment variable that selects an externally hosted Moongate (GPU
/// prover) server for the SP1 CUDA prover. When set (and the `cuda` feature is
/// enabled at build time) the host uses `ProverClient::builder().cuda().server(...)`
/// so that the SDK does not spawn its own Moongate Docker container.
///
/// This is what allows the GPU prover to run inside a container that does not
/// have docker-in-docker (e.g. the RunPod prover image where `moongate-server`
/// runs natively next to the host binary).
#[cfg_attr(not(feature = "cuda"), allow(dead_code))]
const MOONGATE_ENV: &str = "SP1_MOONGATE_ENDPOINT";

/// Prover client selection. The default path (`Env`) preserves the existing
/// behaviour (`ProverClient::from_env()`, respecting `SP1_PROVER`), which is
/// what all local and network proving flows use.
///
/// The `Cuda` variant is only reachable when the crate is compiled with the
/// `cuda` feature AND `SP1_MOONGATE_ENDPOINT` is set at runtime. It builds a
/// `CudaProver` that connects to an externally started `moongate-server` at
/// the given endpoint, which is the docker-free way to drive the GPU prover.
enum Client {
    Env(EnvProver),
    #[cfg(feature = "cuda")]
    Cuda(CudaProver),
}

fn build_client() -> Client {
    #[cfg(feature = "cuda")]
    {
        if let Ok(raw) = std::env::var(MOONGATE_ENV) {
            let ep = raw.trim();
            if !ep.is_empty() {
                println!(
                    "Building SP1 CUDA prover with external Moongate endpoint: {}",
                    ep
                );
                let prover = ProverClient::builder().cuda().server(ep).build();
                return Client::Cuda(prover);
            }
        }
    }
    Client::Env(ProverClient::from_env())
}

impl Client {
    /// Runs the executor and returns the public values + execution report.
    fn execute(
        &self,
        elf: &[u8],
        stdin: &SP1Stdin,
    ) -> Result<(SP1PublicValues, ExecutionReport), String> {
        match self {
            Client::Env(p) => p.execute(elf, stdin).run().map_err(|e| e.to_string()),
            #[cfg(feature = "cuda")]
            Client::Cuda(p) => p.execute(elf, stdin).run().map_err(|e| e.to_string()),
        }
    }

    fn setup(&self, elf: &[u8]) -> (SP1ProvingKey, SP1VerifyingKey) {
        match self {
            Client::Env(p) => p.setup(elf),
            #[cfg(feature = "cuda")]
            Client::Cuda(p) => p.setup(elf),
        }
    }

    /// Generates a compressed SP1 proof. Compressed proofs are the input for
    /// the manual `shrink -> wrap_bn254 -> wrap_groth16_bn254` pipeline below
    /// and are also the recursion input for the next period.
    fn prove_compressed(
        &self,
        pk: &SP1ProvingKey,
        stdin: &SP1Stdin,
    ) -> Result<SP1ProofWithPublicValues, String> {
        match self {
            Client::Env(p) => p
                .prove(pk, stdin)
                .compressed()
                .run()
                .map_err(|e| e.to_string()),
            #[cfg(feature = "cuda")]
            Client::Cuda(p) => p
                .prove(pk, stdin)
                .compressed()
                .run()
                .map_err(|e| e.to_string()),
        }
    }

    fn verify(
        &self,
        bundle: &SP1ProofWithPublicValues,
        vkey: &SP1VerifyingKey,
    ) -> Result<(), sp1_sdk::SP1VerificationError> {
        match self {
            Client::Env(p) => p.verify(bundle, vkey),
            #[cfg(feature = "cuda")]
            Client::Cuda(p) => p.verify(bundle, vkey),
        }
    }

    /// Returns the inner `SP1Prover` used for the manual Groth16 wrap pipeline
    /// (`shrink`, `wrap_bn254`, `wrap_groth16_bn254`). The default type
    /// parameter `C = CpuProverComponents` applies to both variants.
    fn inner(&self) -> &sp1_prover::SP1Prover {
        match self {
            Client::Env(p) => p.inner(),
            #[cfg(feature = "cuda")]
            Client::Cuda(p) => p.inner(),
        }
    }
}

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
    let client = build_client();
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
        let (mut output, report) = client.execute(elf, &stdin).unwrap();
        println!("Execution finished in {:?}", start.elapsed());
        println!("Cycles: {}", report.total_instruction_count());

        let result: VerificationOutput = output.read();
        println!("Result: Valid transition to period {}", result.next_period);
        println!("Next Keys Root: {}", hex::encode(result.next_keys_root));

        // Save Public Values
        let public_values_path =
            std::env::var("PUBLIC_VALUES_FILE").unwrap_or("public_values.bin".to_string());
        let pv_bytes = bincode::serialize(&result).expect("Failed to serialize public values");
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
            let compressed = client.prove_compressed(&pk, &stdin).unwrap();

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

            // 2. Wrap in Groth16 by reusing the compressed proof.
            //    Avoids running core+compress a second time (which would otherwise
            //    happen inside `client.prove(...).groth16().run()`).
            println!("2. Wrapping in Groth16 (shrink -> wrap_bn254 -> groth16)...");

            let public_values = compressed.public_values.clone();
            let reduce_proof = match compressed.proof {
                SP1Proof::Compressed(boxed) => *boxed,
                _ => panic!("Expected a Compressed proof from compressed().run()"),
            };

            let prover = client.inner();
            let opts = SP1ProverOpts::default();

            let shrink_proof = prover
                .shrink(reduce_proof, opts)
                .expect("shrink failed");
            let outer_proof = prover
                .wrap_bn254(shrink_proof, opts)
                .expect("wrap_bn254 failed");

            let groth16_dir = if sp1_prover::build::sp1_dev_mode() {
                sp1_prover::build::try_build_groth16_bn254_artifacts_dev(
                    &outer_proof.vk,
                    &outer_proof.proof,
                )
            } else {
                sp1_sdk::install::try_install_circuit_artifacts("groth16")
            };
            let groth16 = prover.wrap_groth16_bn254(outer_proof, &groth16_dir);

            let proof = SP1ProofWithPublicValues {
                proof: SP1Proof::Groth16(groth16),
                public_values,
                sp1_version: SP1_CIRCUIT_VERSION.to_string(),
                tee_proof: None,
            };

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
            let proof = client.prove_compressed(&pk, &stdin).unwrap();
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
