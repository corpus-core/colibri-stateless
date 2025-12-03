// swift-tools-version:5.3
import PackageDescription

let package = Package(
    name: "Colibri",
    platforms: [.macOS(.v10_15)],
    products: [
        .library(name: "Colibri", targets: ["Colibri"])
    ],
    targets: [
        .target(
            name: "CColibriMacOS",
            path: "Sources/CColibri",
            sources: ["swift_storage_bridge.c"],  // Storage bridge with correct ABI
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("include"),
                .define("C4_DEVELOPMENT_BUILD")
            ],
            linkerSettings: [
                .linkedLibrary("c"),
                .unsafeFlags([
                    "../../build_macos_arm/src/util/libutil.a",
                    "../../build_macos_arm/src/prover/libprover.a",
                    "../../build_macos_arm/src/verifier/libverifier.a",
                    "../../build_macos_arm/src/chains/eth/libeth_verifier.a",
                    "../../build_macos_arm/src/chains/eth/libeth_prover.a",
                    "../../build_macos_arm/src/chains/eth/precompiles/libeth_precompiles.a",
                    "../../build_macos_arm/src/chains/eth/bn254/libeth_bn254.a",
                    "../../build_macos_arm/src/chains/eth/zk_verifier/libzk_verifier.a",
                    // OP Stack support (NEW - these were missing!)
                    "../../build_macos_arm/src/chains/op/libop_verifier.a",
                    "../../build_macos_arm/src/chains/op/libop_prover.a",
                    "../../build_macos_arm/libs/zstd/zstd_build/lib/libzstd.a",
                    "../../build_macos_arm/libs/evmone/libevmone_wrapper.a",
                    "../../build_macos_arm/_deps/evmone_external-build/libevmone.a",
                    "../../build_macos_arm/libs/intx/libintx_wrapper.a",
                    "../../build_macos_arm/_deps/ethhash_external-build/libkeccak.a",
                    "../../build_macos_arm/libs/crypto/libcrypto.a",
                    "../../build_macos_arm/libs/blst/libblst.a",
                    "../../build_macos_arm/bindings/swift/libc4_swift_binding.a"
                ])
            ]
        ),
        .target(
            name: "Colibri",
            dependencies: ["CColibriMacOS"],
            path: "Sources/Colibri",
            sources: ["Colibri.swift"]
        ),
        .testTarget(
            name: "ColibriTests",
            dependencies: ["Colibri"],
            path: "Tests",
            sources: ["ColibriTests.swift", "GeneratedIntegrationTests.swift"]
        )
    ]
)
