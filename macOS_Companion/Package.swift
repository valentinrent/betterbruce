// swift-tools-version: 5.7
import PackageDescription

let package = Package(
    name: "MacCompanion",
    platforms: [
        .macOS(.v13)
    ],
    targets: [
        .executableTarget(
            name: "MacCompanion",
            dependencies: [],
            path: "Sources")
    ]
)
