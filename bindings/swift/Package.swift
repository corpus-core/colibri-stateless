// swift-tools-version:5.3
import PackageDescription

let package = Package(
    name: "Colibri",
     platforms: [.iOS(.v13)],
    products: [
        .library(name: "Colibri", targets: ["ColibriSwift"])
    ],
    targets: [
        .target(
            name: "ColibriSwift",
            dependencies: ["c4_swift"],
            path: "src"
        ),
        .binaryTarget(name: "c4_swift", path: "../../build/libc4_swift.a")
    ]
)