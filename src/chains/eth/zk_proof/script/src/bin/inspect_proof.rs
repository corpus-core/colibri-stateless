use clap::Parser;
use sp1_sdk::{HashableKey, ProverClient, SP1ProofWithPublicValues};
use std::fs::File;
use std::io::Read;

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    #[arg(short, long)]
    proof_path: String,

    #[arg(short, long)]
    elf_path: String,

    #[arg(short, long)]
    save_public_values: Option<String>,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();

    println!("Loading proof from: {}", args.proof_path);
    let mut file = File::open(&args.proof_path)?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes)?;

    let proof: SP1ProofWithPublicValues = bincode::deserialize(&bytes)?;
    println!("Proof deserialized.");

    if let Some(path) = args.save_public_values {
        println!("Saving raw public values to {}", path);
        std::fs::write(path, &proof.public_values.as_slice())?;
    }

    println!("Loading ELF from: {}", args.elf_path);
    let mut elf_file = File::open(&args.elf_path)?;
    let mut elf_bytes = Vec::new();
    elf_file.read_to_end(&mut elf_bytes)?;

    let client = ProverClient::from_env();
    let (_, vk) = client.setup(&elf_bytes);

    println!("Verifying proof with SDK...");
    match client.verify(&proof, &vk) {
        Ok(_) => println!("✅ SDK Verification SUCCESS"),
        Err(e) => println!("❌ SDK Verification FAILED: {}", e),
    }

    println!("VK Hash (bn254): {:?}", vk.hash_bn254());

    Ok(())
}
