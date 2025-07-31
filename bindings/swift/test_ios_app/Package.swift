// swift-tools-version:5.3
import PackageDescription

let package = Package(
    name: "ColibriTestApp",
    platforms: [.iOS(.v13), .macOS(.v10_15)],
    products: [
        .executable(name: "ColibriTestApp", targets: ["ColibriTestApp"])
    ],
    dependencies: [
        // In CI: Use local swift_package build
        // For developers: Replace with GitHub URL : https://github.com/corpus-core/colibri-stateless-swift.git
        .package(path: "../swift_package")
    ],
    targets: [
        .target(
            name: "ColibriTestApp",
            dependencies: ["Colibri"],
            path: "Sources"
        ),
        .testTarget(
            name: "ColibriTestAppTests",
            dependencies: ["ColibriTestApp", "Colibri"],
            path: "Tests"
        )
    ]
)