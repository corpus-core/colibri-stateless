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
            sources: [],  // No sources - use static libraries for macOS
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("include"),
                .define("C4_DEVELOPMENT_BUILD")
            ],
            linkerSettings: [
                .linkedLibrary("c++"),
                .unsafeFlags([
                    "../../build_macos_arm/src/util/libutil.a",
                    "../../build_macos_arm/src/proofer/libproofer.a",
                    "../../build_macos_arm/src/verifier/libverifier.a",
                    "../../build_macos_arm/src/chains/eth/libeth_verifier.a",
                    "../../build_macos_arm/src/chains/eth/libeth_proofer.a",
                    "../../build_macos_arm/src/chains/eth/precompiles/libeth_precompiles.a",
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
            path: "Tests"
        )
    ]
)
