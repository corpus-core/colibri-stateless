// swift-tools-version: 5.9

import PackageDescription

let package = Package(
  name: "colibri_flutter",
  platforms: [
    .iOS("13.0")
  ],
  products: [
    .library(name: "colibri-flutter", targets: ["colibri_flutter"])
  ],
  dependencies: [
    .package(name: "FlutterFramework", path: "../FlutterFramework")
  ],
  targets: [
    .binaryTarget(
      name: "ColibriNative",
      path: "Frameworks/c4_swift.xcframework"
    ),
    // Separate C target: SPM forbids mixing Swift and C in one target.
    .target(
      name: "colibri_force_link",
      dependencies: ["ColibriNative"],
      path: "Sources/colibri_force_link",
      publicHeadersPath: "."
    ),
    .target(
      name: "colibri_flutter",
      dependencies: [
        "ColibriNative",
        "colibri_force_link",
        .product(name: "FlutterFramework", package: "FlutterFramework")
      ],
      path: "Sources/colibri_flutter"
    )
  ]
)
