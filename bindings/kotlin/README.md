
<img src="../emscripten/c4_logo.png" alt="C4 Logo" width="300"/>

# C4 (corpus core colibri client)

![ETH2.0_Spec_Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)
[![CI on multiple platforms](https://github.com/corpus-core/c4/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/corpus-core/c4/actions/workflows/cmake-multi-platform.yml)


# Kotlin/Java Bindings for Colibri

The Colibri bindings for Kotlin/Java are built using CMake and Gradle. It can be used as AAR (Android Archive) or JAR (Java Archive).

> 💡 **Quick Start**: Check out the [Example Android App](./example) for a complete working implementation!

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

