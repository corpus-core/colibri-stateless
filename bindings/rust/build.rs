use std::fs;
use std::path::{Path, PathBuf};

fn main() {
    // Build the C library with CMake if COLIBRI_LIB_DIR is not provided.
    if std::env::var("COLIBRI_LIB_DIR").is_err() {
        let mut cfg = cmake::Config::new("../../");
        cfg.profile("Release");
        cfg.define("SHAREDLIB", "1");
        if let Ok(extra) = std::env::var("COLIBRI_CMAKE_FLAGS") {
            for pair in extra.split_whitespace() {
                let mut it = pair.splitn(2, '=');
                if let (Some(k), Some(v)) = (it.next(), it.next()) {
                    cfg.define(k, v);
                }
            }
        }
        let dst = cfg.build();
        let lib_dirs = discover_lib_dirs(&dst);
        for d in &lib_dirs {
            println!("cargo:rustc-link-search=native={}", d.display());
        }
        println!("cargo:rustc-link-lib=dylib=c4");
    } else {
        let lib_dir = std::env::var("COLIBRI_LIB_DIR").unwrap();
        println!("cargo:rustc-link-search=native={}", lib_dir);
        println!("cargo:rustc-link-lib=dylib=c4");
    }
    #[cfg(target_os = "macos")]
    {
        println!("cargo:rustc-link-arg=-Wl,-rpath,@loader_path");
    }

    // Generate per-directory integration tests from test/data
    generate_dynamic_tests();
}

fn discover_lib_dirs(dst: &Path) -> Vec<PathBuf> {
    let mut res = Vec::new();
    // Common candidates
    let candidates = [
        dst.join("lib"),
        dst.join("build").join("default").join("lib"),
        dst.join("build").join("Release").join("lib"),
        dst.to_path_buf(),
    ];
    for c in candidates {
        if contains_c4(&c) {
            res.push(c);
        }
    }
    // Fallback: recursive search
    if res.is_empty() {
        let _ = walk(dst, &mut res);
    }
    if res.is_empty() {
        println!(
            "cargo:warning=Could not auto-detect libc4.* under {}",
            dst.display()
        );
    }
    res
}

fn contains_c4(dir: &Path) -> bool {
    if !dir.is_dir() {
        return false;
    }
    if let Ok(rd) = fs::read_dir(dir) {
        for e in rd.flatten() {
            let p = e.path();
            if let Some(n) = p.file_name().and_then(|s| s.to_str()) {
                if n.starts_with("libc4")
                    && (n.ends_with(".dylib") || n.ends_with(".so") || n.ends_with(".a"))
                {
                    return true;
                }
            }
        }
    }
    false
}

fn walk(base: &Path, out: &mut Vec<PathBuf>) {
    if let Ok(rd) = fs::read_dir(base) {
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                if contains_c4(&p) {
                    out.push(p.clone());
                }
                walk(&p, out);
            }
        }
    }
}

fn generate_dynamic_tests() {
    // Locate test/data relative to crate
    let manifest_dir = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let repo_root = manifest_dir.join("../../");
    let test_data = repo_root.join("test/data");
    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let out_file = out_dir.join("generated_tests.rs");

    let mut code = String::new();
    if let Ok(rd) = fs::read_dir(&test_data) {
        for e in rd.flatten() {
            let p = e.path();
            if !p.is_dir() {
                continue;
            }
            let test_json = p.join("test.json");
            if !test_json.exists() {
                continue;
            }
            let name = p.file_name().unwrap().to_string_lossy();
            let fn_name = sanitize_ident(&format!("test_{}", name));
            let abs = p.canonicalize().unwrap_or(p.clone());
            let dir_str = abs.to_string_lossy();
            code.push_str(&format!(r#"
#[tokio::test(flavor = "multi_thread")]
#[serial_test::serial]
async fn {fn_name}() -> anyhow::Result<()> {{
    use super::*;
    use std::fs;
    use std::path::PathBuf;
    let base = PathBuf::from("{dir}");
    let test_conf: serde_json::Value = serde_json::from_str(&fs::read_to_string(base.join("test.json"))?)?;
    if test_conf.get("requires_chain_store").and_then(|v| v.as_bool()).unwrap_or(false) {{
        // skip
        return Ok(());
    }}
    if test_conf.get("trusted_blockhash").is_some() {{
        // skip (needs additional impl)
        return Ok(());
    }}
    let method = test_conf.get("method").and_then(|v| v.as_str()).unwrap().to_string();
    let params = test_conf.get("params").cloned().unwrap_or(serde_json::json!([]));
    let chain_id = test_conf.get("chain_id").or(test_conf.get("chain")).and_then(|v| v.as_u64()).unwrap_or(1);
    let cfg = Config {{ chain_id, ..Default::default() }};
    register_storage(MockStorage {{ base: base.clone() }});
    let handler = Arc::new(FileRequestHandler {{ base: base.clone() }});
    let col = Colibri::new(cfg).with_handler(handler);
    let call = RpcCall {{ method, params }};
    let proof = col.create_proof(&call).await?;
    let result = col.verify_proof(&call, &proof).await?;
    let expected = test_conf.get("expected_result").cloned().unwrap_or(serde_json::json!(null));
    assert_eq!(result, expected, "Result mismatch for {{}}", base.display());
    Ok(())
}}
"#, fn_name=fn_name, dir=escape_rust_str(&dir_str)));
        }
    }
    fs::write(&out_file, code).expect("write generated tests");
}

fn sanitize_ident(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for ch in s.chars() {
        if ch.is_ascii_alphanumeric() || ch == '_' {
            out.push(ch);
        } else {
            out.push('_');
        }
    }
    if out
        .chars()
        .next()
        .map(|c| c.is_ascii_digit())
        .unwrap_or(false)
    {
        out.insert(0, '_');
    }
    out
}

fn escape_rust_str(s: &str) -> String {
    s.replace('\\', "\\\\").replace('"', "\\\"")
}
