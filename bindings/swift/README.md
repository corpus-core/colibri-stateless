# Colibri Swift Bindings

Modern Swift bindings für die Colibri C-Bibliothek mit vollständiger iOS und macOS Unterstützung.

## 🚀 Schnellstart

### Für lokale Entwicklung (empfohlen)

```bash
# Schneller Development Build nur für aktuelle Architektur
./bindings/swift/build_dev.sh
```

### Für Production (XCFramework)

```bash
# Vollständiger XCFramework Build für alle Architekturen
./bindings/swift/build_local.sh
```

## 📋 Voraussetzungen

- **macOS** 10.15+ (für Development)
- **Xcode** 12+ mit iOS SDK
- **CMake** 3.20+
- **Swift** 5.3+

### Installation der Abhängigkeiten

```bash
# CMake via Homebrew
brew install cmake

# Oder via Xcode Command Line Tools
xcode-select --install
```

## 🛠️ Build-Optionen

### 1. Development Build (`build_dev.sh`)

**Vorteile:**
- ⚡ Sehr schnell (nur aktuelle Architektur)
- 🔧 Ideal für lokale Entwicklung und Tests  
- 🖥️ Baut für macOS (einfacher)
- 📝 Generiert `Package_dev.swift` für lokale Tests

**Verwendung:**
```bash
./bindings/swift/build_dev.sh
cd bindings/swift
swift test --package-path . --build-path ../../build_dev/swift_build
```

### 2. Production Build (`build_local.sh`)

**Vorteile:**
- 📱 Vollständiges iOS XCFramework
- 🏗️ Unterstützt arm64 (Device) + x86_64 (Simulator)
- 🚢 Production-ready für App Store
- 📦 Kompatibel mit Swift Package Manager

**Verwendung:**
```bash
./bindings/swift/build_local.sh
# XCFramework wird in build/c4_swift.xcframework erstellt
```

### 3. Manuelle CMake-Builds

Dank der auto-detection können Sie jetzt auch direkt CMake verwenden:

```bash
# x86_64 Simulator Build
cmake -DSWIFT=true -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=x86_64 -B build_x86 .
cd build_x86 && make

# arm64 Device Build  
cmake -DSWIFT=true -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DSWIFT_X86_BUILD=$(pwd)/build_x86 -B build .
cd build && make
```

Das `CMAKE_OSX_SYSROOT` wird jetzt automatisch erkannt!

## 📦 Integration in iOS/macOS Projekte

### Swift Package Manager

1. **Mit XCFramework:**
   ```swift
   // Package.swift
   dependencies: [
       .package(url: "https://github.com/corp-us/colibri-stateless-swift", from: "1.0.0")
   ]
   ```

2. **Lokal für Development:**
   ```swift
   // Verwenden Sie Package_dev.swift aus build_dev.sh
   swift build --package-path bindings/swift --build-path build_dev/swift_build
   ```

### Xcode Projekt

1. XCFramework in Projekt ziehen: `build/c4_swift.xcframework`
2. Framework zu "Frameworks, Libraries, and Embedded Content" hinzufügen
3. Import in Swift: `import Colibri`

## 🧪 Testing

### Unit Tests ausführen

```bash
# Development Build
./bindings/swift/build_dev.sh
cd bindings/swift
swift test --package-path . --build-path ../../build_dev/swift_build

# Production Build (mit XCFramework)
./bindings/swift/build_local.sh
cd bindings/swift
swift test
```

### Beispiel-Code

```swift
import Colibri

let colibri = Colibri()
colibri.chainId = 1
colibri.eth_rpcs = ["https://mainnet.infura.io/v3/YOUR-PROJECT-ID"]

do {
    let result = try await colibri.rpc(
        method: "eth_getBalance",
        params: #"["0x742d35Cc6634C0532925a3b844Bc454e4438f44e", "latest"]"#
    )
    print("Balance: \(result)")
} catch {
    print("Fehler: \(error.localizedDescription)")
}
```

## 🐛 Troubleshooting

### Häufige Probleme

**1. SDK nicht gefunden:**
```
Error: iphoneos is not an iOS SDK
```
**Lösung:** Xcode installieren oder SDK-Pfad manuell setzen:
```bash
export CMAKE_OSX_SYSROOT=$(xcrun --sdk iphoneos --show-sdk-path)
```

**2. Architektur-Fehler:**
```
Error: CMAKE_C_COMPILER not set
```
**Lösung:** Verwenden Sie die Build-Scripts oder setzen Sie SYSROOT:
```bash
cmake -DCMAKE_OSX_SYSROOT=$(xcrun --sdk iphonesimulator --show-sdk-path) ...
```

**3. Linker-Fehler:**
```
Undefined symbols for architecture arm64
```
**Lösung:** Alle Dependencies wurden gebaut? Verwenden Sie `build_local.sh`

## 📊 Performance

| Build-Typ | Zeit (M1 Mac) | Output | Verwendung |
|-----------|---------------|---------|------------|
| Development | ~2-3 min | macOS Binary | Lokale Tests |
| Production | ~8-10 min | iOS XCFramework | App Store |
| CI/CD | ~5-6 min | XCFramework | Automated |

## 🔧 Entwicklung

### Project-Struktur

```
bindings/swift/
├── build_dev.sh           # Development Build-Script
├── build_local.sh         # Production Build-Script  
├── CMakeLists.txt         # CMake Konfiguration
├── Package.swift          # Swift Package Manifest
├── README.md              # Diese Datei
├── doc.md                 # API Dokumentation
├── src/
│   ├── Colibri.swift      # Haupt-API
│   └── include/
│       ├── colibri.h      # C Header
│       └── module.modulemap # Module Map
└── Tests/
    └── ColibriTests.swift # Unit Tests
```

### Beitrag leisten

1. Fork das Repository
2. Verwenden Sie `build_dev.sh` für schnelle Iteration
3. Tests hinzufügen in `Tests/ColibriTests.swift`
4. Pull Request erstellen

## 📚 Weitere Dokumentation

- [API Referenz](doc.md) - Vollständige Swift API Dokumentation
- [C API](../colibri.h) - Unterliegende C-Schnittstelle
- [Hauptprojekt](../../README.md) - Colibri C-Bibliothek

## 📄 Lizenz

MIT License - siehe [LICENSE](../../LICENSE) für Details.