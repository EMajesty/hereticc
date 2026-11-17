use std::env;
use std::path::PathBuf;
use std::process::Command;

fn run(command: &mut Command) {
    let status = command.status().expect("start native sequence build tool");
    assert!(
        status.success(),
        "native sequence build command failed: {command:?}"
    );
}

fn main() {
    println!("cargo:rerun-if-changed=native/stock_sequence.cpp");
    println!("cargo:rerun-if-changed=native/stock_sequence_abi.S");

    let target = env::var("TARGET").expect("TARGET");
    if target != "aarch64-unknown-linux-gnu"
        || env::var_os("CARGO_FEATURE_NATIVE_SEQUENCE_ADAPTER").is_none()
    {
        return;
    }

    let out = PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR"));
    let cpp = out.join("stock_sequence.o");
    let abi = out.join("stock_sequence_abi.o");
    let archive = out.join("libneurogenic_stock_sequence_native.a");
    let zig_cache = out.join("zig-cache");

    let configured_cxx = env::var_os("AARCH64_CXX");
    let mut cxx = Command::new(configured_cxx.as_deref().unwrap_or_else(|| "zig".as_ref()));
    if configured_cxx.is_none() {
        cxx.args(["c++", "-target", "aarch64-linux-gnu"]);
    }
    cxx.env("ZIG_GLOBAL_CACHE_DIR", &zig_cache)
        .args([
            "-std=c++17",
            "-fPIC",
            "-fexceptions",
            "-fno-rtti",
            "-fno-sanitize=all",
            "-fno-stack-protector",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-c",
            "native/stock_sequence.cpp",
            "-o",
        ])
        .arg(&cpp);
    run(&mut cxx);

    if let Some(compiler) = configured_cxx {
        let output = Command::new(&compiler)
            .arg("-print-file-name=libstdc++.so")
            .output()
            .expect("locate AArch64 libstdc++");
        assert!(output.status.success(), "locate AArch64 libstdc++");
        let library = PathBuf::from(
            String::from_utf8(output.stdout)
                .expect("libstdc++ path")
                .trim(),
        );
        let directory = library.parent().expect("libstdc++ directory");
        println!("cargo:rustc-link-search=native={}", directory.display());
        println!("cargo:rustc-link-lib=dylib=stdc++");
    }

    let mut assembler = Command::new("zig");
    assembler
        .env("ZIG_GLOBAL_CACHE_DIR", &zig_cache)
        .args([
            "cc",
            "-target",
            "aarch64-linux-gnu",
            "-fPIC",
            "-c",
            "native/stock_sequence_abi.S",
            "-o",
        ])
        .arg(&abi);
    run(&mut assembler);

    run(Command::new("zig")
        .arg("ar")
        .arg("rcs")
        .arg(&archive)
        .arg(&cpp)
        .arg(&abi));
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=neurogenic_stock_sequence_native");
}
