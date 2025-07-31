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
        .package(path: "../swift_package")
        
        // For developers: Replace above line with GitHub URL:
        // .package(url: "https://github.com/corpus-core/colibri-stateless-swift.git", from: "1.0.0")
        // and change package references below from "swift_package" to "colibri-stateless-swift"
    ],
    targets: [
        .executableTarget(
            name: "ColibriTestApp",
            dependencies: [
                .product(name: "Colibri", package: "swift_package")
            ],
            path: "Sources"
        ),
        .testTarget(
            name: "ColibriTestAppTests",
            dependencies: [
                "ColibriTestApp",
                .product(name: "Colibri", package: "swift_package")
            ],
            path: "Tests"
        )
    ]
)