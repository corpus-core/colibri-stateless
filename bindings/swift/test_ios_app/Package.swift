// swift-tools-version:5.4
import PackageDescription

let package = Package(
    name: "ColibriTestApp",
    platforms: [.iOS(.v13)],
    products: [
        .executable(name: "ColibriTestApp", targets: ["ColibriTestApp"])
    ],
    dependencies: [
        // Use iOS XCFramework from ios_package (created by build_ios.sh)
        .package(path: "../ios_package")
    ],
    targets: [
        .executableTarget(
            name: "ColibriTestApp",
            dependencies: [
                .product(name: "Colibri", package: "ios_package")
            ],
            path: "Sources"
        ),
        .testTarget(
            name: "ColibriTestAppTests",
            dependencies: [
                "ColibriTestApp",
                .product(name: "Colibri", package: "ios_package")
            ],
            path: "Tests"
        )
    ]
)