use rand::{RngCore, SeedableRng};
use rand_chacha::ChaCha20Rng;
use sha2::{Digest, Sha256};
use std::env;

// CSPRNG function that generates deterministic output based on seed and index.
fn csprng(seed: &[u8], index: u32, block_bit_length: u32) -> Vec<u8> {
    // Create a unique seed by combining the original seed with the index.
    let mut hasher = Sha256::new();
    hasher.update(seed);
    hasher.update(&index.to_le_bytes());
    let combined_seed = hasher.finalize();
    
    // Initialize ChaCha20 RNG with the combined seed.
    let mut rng = ChaCha20Rng::from_seed(combined_seed.into());
    
    // Calculate the number of bytes needed.
    let block_bytes = (block_bit_length + 7) / 8;
    let mut output = vec![0u8; block_bytes as usize];
    
    // Generate random bytes.
    rng.fill_bytes(&mut output);
    
    // If block_bit_length is not a multiple of 8, mask the extra bits.
    let extra_bits = block_bit_length % 8;
    if extra_bits > 0 {
        let mask = (1u8 << extra_bits) - 1;
        if let Some(last_byte) = output.last_mut() {
            *last_byte &= mask;
        }
    }
    
    output
}

fn main() {
    let args: Vec<String> = env::args().collect();
    
    if args.len() != 5 {
        eprintln!("Usage: {} <seed_hex> <epoch_length> <sub_epoch_length> <block_bit_length>", args[0]);
        eprintln!("  seed_hex: Hexadecimal seed from TRANSEC key");
        eprintln!("  epoch_length: Total epoch duration in seconds");
        eprintln!("  sub_epoch_length: Sub-epoch duration in seconds");
        eprintln!("  block_bit_length: Number of bits per code block");
        std::process::exit(1);
    }
    
    // Parse seed from hex string.
    let seed_hex = &args[1];
    let seed = match hex::decode(seed_hex) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error parsing seed hex: {}", e);
            std::process::exit(1);
        }
    };
    
    // Parse numeric arguments.
    let epoch_length: u32 = match args[2].parse() {
        Ok(n) => n,
        Err(e) => {
            eprintln!("Error parsing epoch_length: {}", e);
            std::process::exit(1);
        }
    };
    
    let sub_epoch_length: u32 = match args[3].parse() {
        Ok(n) => n,
        Err(e) => {
            eprintln!("Error parsing sub_epoch_length: {}", e);
            std::process::exit(1);
        }
    };
    
    let block_bit_length: u32 = match args[4].parse() {
        Ok(n) => n,
        Err(e) => {
            eprintln!("Error parsing block_bit_length: {}", e);
            std::process::exit(1);
        }
    };
    
    // Validate inputs.
    if sub_epoch_length == 0 {
        eprintln!("Error: sub_epoch_length must be greater than 0");
        std::process::exit(1);
    }
    
    if block_bit_length == 0 {
        eprintln!("Error: block_bit_length must be greater than 0");
        std::process::exit(1);
    }
    
    // Calculate N = floor(epoch_length / sub_epoch_length).
    let n = epoch_length / sub_epoch_length;
    
    println!("Generating {} code blocks...", n);
    println!("Parameters:");
    println!("  Seed: {}", seed_hex);
    println!("  Epoch length: {} seconds", epoch_length);
    println!("  Sub-epoch length: {} seconds", sub_epoch_length);
    println!("  Block bit length: {} bits", block_bit_length);
    println!();
    
    // Generate code blocks.
    let mut codes = Vec::new();
    for i in 0..n {
        let code = csprng(&seed, i, block_bit_length);
        codes.push(code);
    }
    
    // Output the generated codes.
    println!("Generated codes:");
    for (i, code) in codes.iter().enumerate() {
        println!("  code[{}] = {}", i, hex::encode(code));
    }
    
    // Concatenate all codes.
    let concatenated: Vec<u8> = codes.into_iter().flatten().collect();
    println!("\nConcatenated code: {}", hex::encode(&concatenated));
}