
<img src="../emscripten/c4_logo.png" alt="C4 Logo" width="300"/>

# C4 (corpus core colibri client)

![ETH2.0_Spec_Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)
[![CI on multiple platforms](https://github.com/corpus-core/c4/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/corpus-core/c4/actions/workflows/cmake-multi-platform.yml)


# Kotlin/Java Bindings for Colibri

The Colibri bindings for Kotlin/Java are built using CMake and Gradle. It can be used as AAR (Android Archive) or JAR (Java Archive).

## Usage

### Java

Add the dependency to your `build.gradle` file:
```groovy
implementation 'com.corpuscore.colibri:colibri-java:0.1.0'
```

use it like this:
```java
import com.corpuscore.colibri.Colibri;
import java.math.BigInteger;

public class Example {
    public static void main(String[] args) {
        // Initialize Colibri with Ethereum Mainnet configuration
        Colibri colibri = new Colibri(
            BigInteger.ONE,  // chainId (1 for Ethereum Mainnet)
            new String[]{"https://eth-mainnet.g.alchemy.com/v2/YOUR-API-KEY"},  // ETH RPC
            new String[]{"https://beacon.quicknode.com/YOUR-API-KEY"}   // Beacon API
        );

        try {
            // Create a proof for a specific block
            byte[] proof = colibri.getProof(
                "eth_getBalance",  // method
                new Object[]{"0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5", "latest"}  // RPC-Arguments
            );
            
            System.out.println("Proof created successfully! Length: " + proof.length);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}```

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
import kotlinx.coroutines.launch
import java.math.BigInteger

class MainActivity : AppCompatActivity() {
    private lateinit var statusText: TextView
    private lateinit var createProofButton: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        statusText = findViewById(R.id.statusText)
        createProofButton = findViewById(R.id.createProofButton)

        val colibri = Colibri(
            BigInteger.ONE,  // chainId (1 for Ethereum Mainnet)
            arrayOf("https://eth-mainnet.g.alchemy.com/v2/YOUR-API-KEY"),  // ETH RPC
            arrayOf("https://beacon.quicknode.com/YOUR-API-KEY")   // Beacon API
        )

        createProofButton.setOnClickListener {
            lifecycleScope.launch {
                try {
                    statusText.text = "Creating proof..."
                    val proof = colibri.getProof(
                        "eth_getBalance",  // method
                        arrayOf("0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5","latest")  // block number as argument
                    )
                    statusText.text = "Proof created successfully! Length: ${proof.size}"
                } catch (e: Exception) {
                    statusText.text = "Error: ${e.message}"
                }
            }
        }
    }
}
```

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

