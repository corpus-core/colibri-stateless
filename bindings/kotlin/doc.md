: Bindings

:: Kotlin/Java

The Colibri bindings for Kotlin/Java are built using CMake and Gradle. It can be used as AAR (Android Archive) or JAR (Java Archive).

> 💡 **Quick Start**: Check out the [Example Android App](https://github.com/corpus-core/colibri-stateless/tree/dev/bindings/kotlin/example) for a complete working implementation!

## Installation

The Colibri Kotlin/Java bindings are published to [GitHub Packages](https://github.com/corpus-core/colibri-stateless/packages) and are **publicly available without authentication**.

### Adding the Repository

Add the GitHub Packages repository to your project:

**Groovy (build.gradle):**
```groovy
repositories {
    mavenCentral()
    maven {
        url = uri("https://maven.pkg.github.com/corpus-core/colibri-stateless")
    }
}
```

**Kotlin DSL (build.gradle.kts):**
```kotlin
repositories {
    mavenCentral()
    maven {
        url = uri("https://maven.pkg.github.com/corpus-core/colibri-stateless")
    }
}
```

> **Note:** The packages are public and no authentication is required for downloading.

### Versioning

The packages are automatically published with semantic versioning:

- **Release versions** (e.g., `1.0.0`, `1.2.3`): Created from Git tags like `v1.0.0`
- **Snapshot versions** (e.g., `1.0.0-SNAPSHOT`): Built from the `dev` branch on every push

For production use, always pin to a specific release version. Use SNAPSHOT versions only for development and testing.

## Usage

### Java (JAR)

Add the JAR dependency to your `build.gradle` file:
```groovy
dependencies {
    implementation 'com.corpuscore:colibri-jar:1.0.0'
}
```

Use it like this:
```java
import com.corpuscore.colibri.Colibri;
import com.corpuscore.colibri.ColibriException;

public class Example {
    public static void main(String[] args) {
        // Initialize Colibri (uses default Ethereum Mainnet configuration)
        Colibri colibri = new Colibri();

        try {
            // Call RPC method to get current block number
            byte[] result = colibri.rpc("eth_blockNumber", new Object[]{});
            String blockNumberHex = new String(result);
            System.out.println("Current block number: " + blockNumberHex);
            
            // Example with parameters - get account balance
            byte[] balanceResult = colibri.rpc("eth_getBalance", 
                new Object[]{"0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5", "latest"});
            String balance = new String(balanceResult);
            System.out.println("Account balance: " + balance);
            
        } catch (ColibriException e) {
            System.err.println("Colibri error: " + e.getMessage());
        } catch (Exception e) {
            System.err.println("Unexpected error: " + e.getMessage());
        }
    }
}
```

### Kotlin

#### For Android (AAR)

For Android projects, use the AAR artifact that includes native libraries for all Android ABIs (armeabi-v7a, arm64-v8a, x86, x86_64):

```kotlin
dependencies {
    implementation("com.corpuscore:colibri-aar:1.0.0")
}
```

#### For JVM/Server (JAR)

For server-side Kotlin or JVM projects, use the JAR artifact that includes native libraries for Linux, macOS (ARM64), and Windows:

```kotlin
dependencies {
    implementation("com.corpuscore:colibri-jar:1.0.0")
}
```

Use it like this:

```kotlin
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.corpuscore.colibri.Colibri
import com.corpuscore.colibri.ColibriException
import kotlinx.coroutines.launch

class MainActivity : AppCompatActivity() {
    private lateinit var blockNumberText: TextView
    private lateinit var refreshButton: Button
    private lateinit var statusText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        blockNumberText = findViewById(R.id.blockNumberText)
        refreshButton = findViewById(R.id.refreshButton)
        statusText = findViewById(R.id.statusText)

        // Initialize Colibri (uses default Ethereum Mainnet configuration)
        val colibri = Colibri()

        refreshButton.setOnClickListener {
            lifecycleScope.launch {
                try {
                    statusText.text = "Fetching block number..."
                    refreshButton.isEnabled = false
                    
                    // Call RPC method to get current block number
                    val result = colibri.rpc("eth_blockNumber", arrayOf())
                    val blockNumberHex = String(result)
                    val blockNumber = blockNumberHex.removePrefix("0x").toLong(16)
                    
                    blockNumberText.text = "#$blockNumber"
                    statusText.text = "Updated successfully"
                    
                } catch (e: ColibriException) {
                    statusText.text = "Colibri error: ${e.message}"
                } catch (e: Exception) {
                    statusText.text = "Error: ${e.message}"
                } finally {
                    refreshButton.isEnabled = true
                }
            }
        }
        
        // Fetch block number on startup
        refreshButton.performClick()
    }
}
```

## Configuration

### Prover Mode

Controls how proofs are built and verified. Set via `proverMode` in the constructor:

- **`ProverMode.LOCAL`** -- Proofs are built entirely on the client. Requires access to a Beacon API and execution layer RPC. Fully trustless, but slower and needs more infrastructure.
- **`ProverMode.REMOTE`** -- Proofs are fetched from a remote Colibri prover server. Fastest option but relies on the prover server for proof generation. The verifier still cryptographically checks every proof.
- **`ProverMode.HYBRID`** -- The consensus-layer proof (BlockHeaderProof) comes from the Colibri server, while execution-layer data (account proofs, storage, etc.) is fetched directly from the RPC provider. Best balance of performance and scalability -- the Colibri server only serves lightweight, cacheable header proofs while the heavy RPC load goes to your existing provider.
- **`ProverMode.PROXY`** -- Like remote, but the client sends its own RPC and Beacon API URLs to the prover server. The server uses these endpoints instead of its own. Useful when the client has access to private or premium RPC providers.
- **`ProverMode.LIGHT_CLIENT`** -- Like hybrid, with additional background polling of block headers to keep the cache warm. Call `startLightClient()` / `stopLightClient()` to control polling (default interval: 12s). By default only the compact `eth_getBlockHeader` is fetched; pass `fullBlock = true` to fetch the full block (useful when many `eth_getTransactionByHash` / `eth_getTransactionReceipt` calls follow).

```kotlin
// Hybrid mode: header proofs from Colibri, execution data from RPC provider
val colibri = Colibri(
    chainId = BigInteger.ONE,
    proverMode = ProverMode.HYBRID
)

// Light client mode with background header polling
val lightClient = Colibri(
    chainId = BigInteger.ONE,
    proverMode = ProverMode.LIGHT_CLIENT
)
lightClient.startLightClient()                    // polls eth_getBlockHeader every 12s
lightClient.startLightClient(fullBlock = true)    // or fetch the full block
```

Default: `ProverMode.REMOTE` when prover URLs are configured, `ProverMode.LOCAL` otherwise.

### Privacy (PAP)

**PAP (Pragmatic Adaptive Privacy)** reduces intent leakage towards RPC/prover by using cached data when available and verifying afterwards.

- `privacyMode` – `PrivacyMode.NONE` (default) or `PrivacyMode.BASIC`. With `BASIC`, the verifier sets the PAP flag so that method-type and verification can use cached storage for optimistic execution (e.g. for `eth_call`); method type may depend on params.

```kotlin
val colibri = Colibri(
    chainId = BigInteger.ONE,
    privacyMode = PrivacyMode.BASIC
)
```

### Privacy-preserving `eth_call` (oblivious + PAP + hybrid)

For an `eth_call` with full storage privacy, use hybrid prover mode, PAP, and oblivious nodes (`obliviousNodes` default is empty).

- **HYBRID:** only the block proof from the prover; storage values from RPC/oblivious node, verified locally.
- **PAP (`PrivacyMode.BASIC`):** no `eth_createAccessList` on the prover; optimistic local EVM, only `eth_getProof` leaves the client.
- **Oblivious:** TEE RPC for `eth_getProof`; enables OBLIVIOUS + PAP verify flags automatically. See [Oblivious Labs](https://www.obliviouslabs.com/) for TEE/ORAM background.

```kotlin
// https://rpc.safe-node.com/ requires an API key for testing
val colibri = Colibri(
    chainId = BigInteger.ONE,
    privacyMode = PrivacyMode.BASIC,
    proverMode = ProverMode.HYBRID,
    obliviousNodes = arrayOf("https://rpc.safe-node.com/"),
)
```

## Example Android App

A complete working example is available in the [example directory](https://github.com/corpus-core/colibri-stateless/tree/main/bindings/kotlin/example). This minimal Android app demonstrates:

- **Real-world usage**: How to integrate Colibri in an Android application
- **RPC calls**: Using `eth_blockNumber` to fetch the current Ethereum block number
- **Error handling**: Proper exception handling for network and Colibri errors
- **Async operations**: Using Kotlin coroutines for non-blocking RPC calls
- **UI integration**: Updating Android UI components based on RPC results

### Running the Example

```bash
cd bindings/kotlin/example
./gradlew build
./gradlew installDebug  # Install on connected Android device/emulator
```

The example app includes:
- Simple UI with block number display and refresh button
- Automatic block number fetching on startup
- Error states and loading indicators
- Public Ethereum RPC endpoint configuration (no API keys required)

## Resources

- 📦 **[GitHub Packages](https://github.com/corpus-core/colibri-stateless/packages)** - All published versions (JAR & AAR)
- 📖 **[Kotlin/Java Documentation](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/kotlin-java)** - This complete documentation
- 🔗 **[Supported RPC Methods](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/supported-rpc-methods)** - Full list of available Ethereum RPC calls
- 🏗️ **[Building Guide](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/building)** - Build from source instructions
- 🧪 **[Example Android App](https://github.com/corpus-core/colibri-stateless/tree/main/bindings/kotlin/example)** - Complete working implementation

## Building
Make sure you have the Java SDK, Cmake and Swig installed.

### JAR

While the CI is building the native libs for multiple platforms, you can build the JAR locally with:
```bash
cmake -S . -B build -DGENERATE_JAVA_SOURCES=ON -DCMAKE_BUILD_TYPE=Release -DCURL=false -DKOTLIN=true --build --target c4_java
cd bindings/kotlin
./gradlew -b build-jar.gradle.kts build
```

### AAR

Of course you need to install the Android SDK and NDK first.

```bash
cd bindings/kotlin
./gradlew -b build-aar.gradle.kts build
```

