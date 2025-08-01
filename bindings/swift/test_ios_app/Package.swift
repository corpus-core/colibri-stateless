// swift-tools-version:5.4
import PackageDescription

let package = Package(
    name: "ColibriTestApp",
    platforms: [.iOS(.v13)],
    products: [
        .executable(name: "ColibriTestApp", targets: ["ColibriTestApp"])
    ],
    dependencies: [
        // Use iOS XCFramework from swift_package
        .package(path: "../swift_package")
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