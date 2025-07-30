// swift-tools-version:5.3
import PackageDescription

let package = Package(
    name: "Colibri",
    platforms: [.iOS(.v13), .macOS(.v10_15)],
    products: [
        .library(name: "Colibri", targets: ["Colibri"])
    ],
    targets: [
        .target(
            name: "Colibri",
            dependencies: ["c4_swift"],
            path: "src",
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("include")
            ],
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]),            
        .binaryTarget(
            name: "c4_swift", 
            path: "../../build/c4_swift.xcframework"
        ),
        .testTarget(
            name: "ColibriTests",
            dependencies: ["Colibri"],
            path: "Tests"
        )
    ]
)