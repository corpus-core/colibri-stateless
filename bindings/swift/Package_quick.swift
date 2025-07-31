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
            name: "CColibriReal",
            path: "Sources/CColibri", 
            sources: [],  // No source files, just headers
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("include"),
                .headerSearchPath("../../../../bindings"),
                .define("C4_DEVELOPMENT_BUILD")
            ],
            linkerSettings: [
                .linkedLibrary("c++"),
                .unsafeFlags([
                    "../../../../build_quick/bindings/swift/libc4_swift_binding.a",
                    "../../../../build_quick/src/util/libutil.a",
                    "../../../../build_quick/src/proofer/libproofer.a", 
                    "../../../../build_quick/src/verifier/libverifier.a",
                    "../../../../build_quick/src/chains/eth/libeth_verifier.a",
                    "../../../../build_quick/src/chains/eth/libeth_proofer.a",
                    "../../../../build_quick/libs/crypto/libcrypto.a",
                    "../../../../build_quick/libs/blst/libblst.a",
                    "../../../../build_quick/libs/intx/libintx_wrapper.a",
                    "../../../../build_quick/src/chains/eth/precompiles/libeth_precompiles.a",
                    "../../../../build_quick/libs/evmone/libevmone_wrapper.a"
                ])
            ]),
        .target(
            name: "Colibri",
            dependencies: ["CColibriReal"],
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
