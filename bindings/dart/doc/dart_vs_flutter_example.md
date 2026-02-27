# Analyse: Dart-Beispiel vs. Flutter-Beispiel

Da die **Proofs identisch** sind (gleiche Bytes von gleichem Prover), muss der Unterschied, der bei Flutter zu „invalid zk_proof!“ führt, außerhalb der Proof-Daten liegen.

## 1. Konfiguration / Colibri-Parameter

| Aspekt | Dart (`basic_usage.dart`) | Flutter (`main.dart`) |
|--------|---------------------------|------------------------|
| **chainId** | 1 | 1 |
| **provers** | `resolveProvers()` | `resolveProvers()` |
| **ethRpcs** | `resolveEthRpcs(fallback: …)` | `resolveEthRpcs(fallback: …)` |
| **zkProof** | `resolveZkProof()` | `wantZk` = `zkFromEnv \|\| (witnessKeys gesetzt)` |
| **checkpointWitnessKeys** | `resolveCheckpointWitnessKeys()` | `resolveCheckpointWitnessKeys()` |
| **logProverRequests** | `resolveZkDebug()` | `resolveZkDebug()` |
| **onDebug** | `(msg) => print(msg)` | `_addLog` (UI) |

Inhaltlich gleich: gleicher Prover, gleiche ZK-/Witness-Config → **identischer Proof** (bestätigt).

---

## 2. .env laden

| | Dart | Flutter |
|---|------|--------|
| **Quelle** | `File('.env')` im aktuellen Arbeitsverzeichnis (sync) | `rootBundle.loadString('assets/.env')` aus App-Assets (async) |
| **Bei Fehler** | Datei nicht da → leere Werte, Fallback auf `Platform.environment` | Asset fehlt/Fehler → `catch` ignoriert, nur `Platform.environment` |
| **Laufzeit** | `dart run` → CWD = `bindings/dart/example` oder Projektroot | App-Bundle → CWD irrelevant, nur Asset-Pfad zählt |

Mögliche Folge unter Flutter: Wenn `assets/.env` nicht im Bundle ist oder anders gelesen wird, könnten theoretisch andere Werte ankommen – **aber**: Proofs sind identisch, also wird gleicher Prover, gleiche Methode/Parameter und gleiche ZK-Anfrage genutzt. Der Unterschied liegt also nicht in der Prover-Anfrage.

---

## 3. Native Library (der entscheidende Unterschied)

| | Dart-Beispiel | Flutter-Beispiel |
|---|----------------|------------------|
| **Laufzeitumgebung** | Desktop (macOS/Linux/Windows), `dart run` | Mobil: Android (oder iOS) / ggf. Desktop |
| **libraryPath** | Explizit: `native/libcolibri.so` (bzw. `.dylib`/`.dll`) aus `bindings/dart/native` | `colibriFlutterLibraryPath` → unter **Android: null** |
| **Geladene Bibliothek** | Desktop-Binary aus lokalem Build (`build.sh` o.ä.) | Unter Android: **`libcolibri.so` aus dem Flutter-Plugin** (z. B. `jniLibs/arm64-v8a/libcolibri.so`), gebaut mit `scripts/build_flutter_binaries.sh` |

**Wie geladen wird** (`native.dart`):

- **Dart (Desktop):** `DynamicLibrary.open(libraryPath)` → z. B. `native/libcolibri.so`.
- **Flutter (Android):** `libraryPath` ist null → `DynamicLibrary.open('libcolibri.so')` → die vom Plugin mitgelieferte **Android-**Bibliothek.

Damit erhält die **Verifikation** in beiden Fällen die **gleichen Proof-Bytes**, aber:

- **Dart:** Verifikation läuft in der **Desktop-**Lib (z. B. x86_64/arm64 macOS/Linux).
- **Flutter (Android):** Verifikation läuft in der **Android-**Lib (z. B. arm64-v8a).

---

## 4. Fazit

- **Proof-Erzeugung/Anfrage:** Gleich (identische Proofs).
- **Unterschied:** Nur die **native Bibliothek**, die den Proof verifiziert:
  - **Dart:** Desktop-`libcolibri` (lokal gebaut).
  - **Flutter (Android):** Android-`libcolibri` aus dem Flutter-Plugin (Build aus `build_flutter_binaries.sh`).

Wenn dieselben Bytes im Dart-Beispiel verifizieren und im Flutter-Beispiel „invalid zk_proof!“ liefern, liegt die Ursache sehr wahrscheinlich in der **Android-Binary**, z. B.:

1. **Anderer ZK-Verifier-Stand:** Anderer oder fehlender VK (Verification Key), anderes Programm-Hash/VK-Registry.
2. **Build-Unterschied:** ZK/ETH_ZKPROOF oder VK-Einbindung beim Android-Build anders als beim Desktop-Build.
3. **Plattform-Crypto:** Anderes Verhalten von BN254/Paaring auf ARM/Android (Endianness, andere Implementierung, andere Abweichungen).

**Empfehlung:** Android-Build prüfen (CMake/Options, eingebaute VK, gleiche ZK-Quellen wie Desktop) und sicherstellen, dass die gleiche ZK-Verifier-Logik und dieselben VK-Daten wie auf dem Desktop verwendet werden.
