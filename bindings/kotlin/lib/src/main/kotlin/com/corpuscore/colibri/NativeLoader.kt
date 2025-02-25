package com.corpuscore.colibri

import java.io.File
import java.io.FileOutputStream
import java.io.InputStream

object NativeLoader {
    private var loaded = false

    fun loadLibrary() {
        if (loaded) return
        
        try {
            // Try Android (AAR) way first
            System.loadLibrary("c4_java")
            loaded = true
            return
        } catch (e: UnsatisfiedLinkError) {
            // If AAR loading fails, try JAR way
            try {
                val osName = System.getProperty("os.name").lowercase()
                val libraryName = when {
                    osName.contains("win") -> "c4_java.dll"
                    osName.contains("mac") -> "libc4_java.dylib"
                    else -> "libc4_java.so"
                }
                
                val libraryPath = "/native/$osName/$libraryName"
                // Create a temporary directory using createTempFile
                val tmpDir = File(System.getProperty("java.io.tmpdir"), "native-lib-${System.nanoTime()}")
                tmpDir.mkdir()
                val tmpLib = File(tmpDir, libraryName)
                
                NativeLoader::class.java.getResourceAsStream(libraryPath)?.use { input ->
                    FileOutputStream(tmpLib).use { output ->
                        input.copyTo(output)
                    }
                } ?: throw UnsatisfiedLinkError("Could not find native library: $libraryPath")
                
                System.load(tmpLib.absolutePath)
                loaded = true
                
                // Clean up the temporary files when the JVM exits
                tmpLib.deleteOnExit()
                tmpDir.deleteOnExit()
            } catch (e2: Exception) {
                throw UnsatisfiedLinkError("Failed to load native library: ${e2.message}")
            }
        }
    }
} 