package com.corpuscore.colibri

import java.io.File
import java.io.FileOutputStream
import java.nio.file.Files

object NativeLoader {
    init {
        loadNativeLibrary()
    }

    private fun loadNativeLibrary() {
        val libraryName = when {
            System.getProperty("os.name").lowercase().contains("windows") -> "c4_java.dll"
            System.getProperty("os.name").lowercase().contains("mac") -> "libc4_java.jnilib"
            else -> "libc4_java.so"
        }

        val arch = System.getProperty("os.arch").lowercase()
        val osName = when {
            System.getProperty("os.name").lowercase().contains("windows") -> "win32-x86-64"
            System.getProperty("os.name").lowercase().contains("mac") -> {
                // Handle macOS architecture
                val archSuffix = when (arch) {
                    "aarch64", "arm64" -> "aarch64"
                    else -> "x86-64"
                }
                "darwin-$archSuffix"
            }
            else -> {
                // Handle Linux architecture
                val archSuffix = when (arch) {
                    "aarch64", "arm64" -> "aarch64"
                    else -> "x86-64"
                }
                "linux-$archSuffix"
            }
        }

        val resourcePath = "native/$osName/$libraryName"
        
        // Create temporary directory for the native library
        val tempDir = Files.createTempDirectory("colibri-native-").toFile()
        tempDir.deleteOnExit()

        // Extract library to temporary directory
        val libraryFile = File(tempDir, libraryName)
        libraryFile.deleteOnExit()

        // Copy library from JAR to temporary directory
        javaClass.classLoader.getResourceAsStream(resourcePath)?.use { input ->
            FileOutputStream(libraryFile).use { output ->
                input.copyTo(output)
            }
        } ?: throw UnsatisfiedLinkError(
            "Native library $libraryName not found in JAR for platform $osName. " +
            "Available architectures: x86-64 and aarch64 for macOS, x86-64 for Windows and Linux"
        )

        // Load the library
        System.load(libraryFile.absolutePath)
    }
} 