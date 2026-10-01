// swift-tools-version: 5.9

import PackageDescription

let package = Package(
  name: "colibri_flutter",
  platforms: [
    .macOS("10.15")
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
      path: "Frameworks/libcolibri.xcframework"
    ),
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
