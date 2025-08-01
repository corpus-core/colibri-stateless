: Bindings

:: Kotlin/Java

The Colibri bindings for Kotlin/Java are built using CMake and Gradle. It can be used as AAR (Android Archive) or JAR (Java Archive).

> 💡 **Quick Start**: Check out the [Example Android App](https://github.com/corpus-core/colibri-stateless/tree/main/bindings/kotlin/example) for a complete working implementation!

## Usage

### Java

Add the dependency to your `build.gradle` file:
```groovy
implementation 'com.corpuscore.colibri:colibri-java:0.1.0'
```

use it like this:
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

Add the dependency to your `build.gradle` file:

```groovy
repositories {
    google()
    mavenCentral()
    // Add the repository where Colibri is published
    maven {
        url = uri("https://your.maven.repo")
    }
}

dependencies {
    implementation("com.corpuscore:colibri-aar:1.0.0")
}
```

use it like this:

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

