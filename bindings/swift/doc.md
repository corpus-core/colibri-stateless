# Colibri Swift Bindings

Colibri Swift Bindings bieten eine moderne, typsichere Swift-API für die Colibri C-Bibliothek.

## Funktionen

- **Async/Await Support**: Alle API-Aufrufe sind asynchron und verwenden Swift's moderne Concurrency-Features
- **Memory Safety**: Sichere Speicherverwaltung mit automatischer Ressourcenfreigabe
- **Type Safety**: Vollständig typisierte Swift-API mit Enum-basierten Methodentypen
- **Error Handling**: Umfassendes Error-Handling mit lokalisierten Fehlermeldungen
- **Multi-Platform**: Unterstützt iOS 13+ und macOS 10.15+

## Installation

### Swift Package Manager

Fügen Sie das Package zu Ihrer `Package.swift` hinzu:

```swift
dependencies: [
    .package(url: "https://github.com/corp-us/colibri-stateless-swift", from: "1.0.0")
]
```

### Xcode

1. File → Add Package Dependencies
2. URL eingeben: `https://github.com/corp-us/colibri-stateless-swift`
3. Version auswählen und hinzufügen

## Schnellstart

```swift
import Colibri

// Colibri-Instance erstellen
let colibri = Colibri()

// Konfiguration
colibri.chainId = 1 // Ethereum Mainnet
colibri.eth_rpcs = ["https://mainnet.infura.io/v3/YOUR-PROJECT-ID"]
colibri.beacon_apis = ["https://beaconcha.in/api/v1"]
colibri.proofers = ["https://c4.incubed.net"]

// RPC-Aufruf durchführen
do {
    let result = try await colibri.rpc(
        method: "eth_getBalance", 
        params: "[\"0x742d35Cc6634C0532925a3b844Bc454e4438f44e\", \"latest\"]"
    )
    print("Balance: \(result)")
} catch {
    print("Fehler: \(error.localizedDescription)")
}
```

## API-Referenz

### Klasse: `Colibri`

Die Hauptklasse für alle Colibri-Operationen.

#### Konfiguration

- `chainId: UInt64` - Chain-ID (Standard: 1 für Ethereum Mainnet)
- `eth_rpcs: [String]` - Liste der Ethereum RPC-Endpunkte
- `beacon_apis: [String]` - Liste der Beacon Chain API-Endpunkte  
- `proofers: [String]` - Liste der Proofer-Endpunkte
- `trustedBlockHashes: [String]` - Vertrauenswürdige Block-Hashes
- `includeCode: Bool` - Ob Contract-Code in Proofs eingeschlossen werden soll

#### Methoden

##### `rpc(method:params:) async throws -> Any`

Führt einen RPC-Aufruf durch. Automatische Bestimmung ob Proof benötigt wird.

**Parameter:**
- `method: String` - RPC-Methodenname (z.B. "eth_getBalance")
- `params: String` - JSON-Array der Parameter als String

**Rückgabe:** `Any` - Ergebnis des RPC-Aufrufs

**Fehler:** `ColibriError` bei Fehlern

##### `getMethodSupport(method:) async throws -> MethodType`

Bestimmt den Unterstützungsgrad einer RPC-Methode.

**Parameter:**
- `method: String` - RPC-Methodenname

**Rückgabe:** `MethodType` - Art der Methodenunterstützung

##### `createProof(method:params:) async throws -> Data`

Erstellt einen kryptographischen Proof für einen RPC-Aufruf.

**Parameter:**
- `method: String` - RPC-Methodenname
- `params: String` - JSON-Parameter

**Rückgabe:** `Data` - Binärer Proof

##### `verifyProof(proof:method:params:) async throws -> Any`

Verifiziert einen Proof und gibt das Ergebnis zurück.

**Parameter:**
- `proof: Data` - Zu verifizierender Proof
- `method: String` - RPC-Methodenname
- `params: String` - JSON-Parameter

**Rückgabe:** `Any` - Verifiziertes Ergebnis

### Enum: `MethodType`

Beschreibt den Unterstützungsgrad einer RPC-Methode:

- `.PROOFABLE` - Methode unterstützt kryptographische Proofs
- `.UNPROOFABLE` - Methode wird unterstützt, aber ohne Proofs
- `.NOT_SUPPORTED` - Methode wird nicht unterstützt
- `.LOCAL` - Methode wird lokal ausgeführt
- `.UNKNOWN` - Methodentyp unbekannt

### Enum: `ColibriError`

Alle möglichen Fehler mit lokalisierten Beschreibungen:

- `.invalidInput` - Ungültige Eingabeparameter
- `.executionFailed` - Ausführung fehlgeschlagen
- `.invalidJSON` - Ungültiges JSON-Format
- `.proofError(String)` - Proof-spezifischer Fehler
- `.unknownStatus(String)` - Unbekannter Status
- `.invalidURL` - Ungültige URL
- `.rpcError(String)` - RPC-Fehler
- `.httpError(statusCode:details:)` - HTTP-Fehler
- `.methodNotSupported(String)` - Methode nicht unterstützt
- `.unknownMethodType(String)` - Unbekannter Methodentyp
- `.memoryAllocationFailed` - Speicherallokation fehlgeschlagen
- `.nullPointerReceived` - Null-Pointer erhalten
- `.contextCreationFailed` - Kontext-Erstellung fehlgeschlagen

## Beispiele

### Einfacher Balance-Aufruf

```swift
let colibri = Colibri()
colibri.eth_rpcs = ["https://mainnet.infura.io/v3/YOUR-PROJECT-ID"]

let balance = try await colibri.rpc(
    method: "eth_getBalance",
    params: "[\"0x742d35Cc6634C0532925a3b844Bc454e4438f44e\", \"latest\"]"
)
```

### Mit Proof-Verifikation

```swift
let colibri = Colibri()
colibri.proofers = ["https://c4.incubed.net"]

// Proof erstellen
let proof = try await colibri.createProof(
    method: "eth_getBalance",
    params: "[\"0x742d35Cc6634C0532925a3b844Bc454e4438f44e\", \"latest\"]"
)

// Proof verifizieren
let result = try await colibri.verifyProof(
    proof: proof,
    method: "eth_getBalance", 
    params: "[\"0x742d35Cc6634C0532925a3b844Bc454e4438f44e\", \"latest\"]"
)
```

### Error Handling

```swift
do {
    let result = try await colibri.rpc(method: "eth_getBalance", params: "[\"invalid\"]")
} catch ColibriError.invalidJSON {
    print("JSON-Format ungültig")
} catch ColibriError.methodNotSupported(let method) {
    print("Methode \(method) nicht unterstützt")
} catch {
    print("Unerwarteter Fehler: \(error.localizedDescription)")
}
```

## Technische Details

### Memory Management

Die Swift-Bindings handhaben automatisch:
- C-String Allokation und Freigabe
- Unsafe Pointer Management
- Automatische Ressourcenfreigabe über `defer`-Blöcke

### Concurrency

Alle API-Aufrufe sind vollständig async/await-kompatibel und thread-safe.

### Platform Support

- **iOS**: 13.0+
- **macOS**: 10.15+
- **Architekturen**: arm64, x86_64

Das XCFramework enthält optimierte Binaries für alle unterstützten Plattformen.