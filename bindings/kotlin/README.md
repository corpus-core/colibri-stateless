
<img src="https://github.com/corpus-core/colibri-stateless/raw/dev/c4_logo.png" alt="C4 Logo" width="300"/>

# Kotlin/Java Bindings for Colibri

The Colibri bindings for Kotlin/Java are built using CMake and Gradle. It can be used as AAR (Android Archive) or JAR (Java Archive).

> 💡 **Quick Start**: Check out the [Example Android App](./example) for a complete working implementation!

## Installation

### Adding the Repository

Add the GitHub Packages repository to your `build.gradle` or `build.gradle.kts`:

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

> **Note:** The packages are public and **no authentication is required** for downloading.

## Usage

### Java (JAR)

Add the dependency to your `build.gradle` file:
```groovy
dependencies {
    implementation 'com.corpuscore:colibri-jar:1.0.0'
}
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
            
        } catch (ColibriException e) {
            System.err.println("Colibri error: " + e.getMessage());
        } catch (Exception e) {
            System.err.println("Unexpected error: " + e.getMessage());
        }
    }
}
```

### Kotlin/Android (AAR)

For Android projects, use the AAR artifact:

```kotlin
dependencies {
    implementation("com.corpuscore:colibri-aar:1.0.0")
}
```

Or for server-side Kotlin, use the JAR:

```kotlin
dependencies {
    implementation("com.corpuscore:colibri-jar:1.0.0")
}
```

**Available Versions:**
- Release versions: `1.0.0`, `1.0.1`, etc. (from Git tags like `v1.0.0`)
- Snapshot versions: `1.0.0-SNAPSHOT` (from dev branch, updated on each push)

Use it like this:

```kotlin
import com.corpuscore.colibri.Colibri
import com.corpuscore.colibri.ColibriException
import kotlinx.coroutines.*

suspend fun main() {
    // Initialize Colibri (uses default Ethereum Mainnet configuration)
    val colibri = Colibri()
    
    try {
        // Call RPC method to get current block number
        val result = colibri.rpc("eth_blockNumber", arrayOf())
        val blockNumberHex = String(result)
        val blockNumber = blockNumberHex.removePrefix("0x").toLong(16)
        
        println("Current block number: #$blockNumber")
        
    } catch (e: ColibriException) {
        println("Colibri error: ${e.message}")
    } catch (e: Exception) {
        println("Unexpected error: ${e.message}")
    }
}
```

For Android integration, see the [complete example app](./example).

## Example Android App

A minimal working Android app is available in the [`example/`](./example) directory. It demonstrates:

- Fetching Ethereum block numbers using `eth_blockNumber`
- Proper error handling and async operations  
- Android UI integration with Kotlin coroutines

Run it with:
```bash
cd example && ./gradlew build && ./gradlew installDebug
```

## Resources

- 📦 **[GitHub Packages](https://github.com/corpus-core/colibri-stateless/packages)** - All published versions
- 📖 **[Complete Documentation](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/kotlin-java)** - Detailed API reference and guides
- 🔗 **[Supported RPC Methods](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/supported-rpc-methods)** - Full list of available Ethereum RPC calls
- 🏗️ **[Building Guide](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/building)** - Build from source instructions

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

