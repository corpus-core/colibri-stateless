package com.corpuscore.colibri

import java.io.File
import java.io.FileOutputStream
import java.nio.file.Files

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
                val tmpDir = createTempDirectory("native-lib")
                val tmpLib = tmpDir.resolve(libraryName)
                
                NativeLoader::class.java.getResourceAsStream(libraryPath)?.use { input ->
                    tmpLib.outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
                
                System.load(tmpLib.absolutePath)
                loaded = true
            } catch (e2: Exception) {
                throw UnsatisfiedLinkError("Failed to load native library: ${e2.message}")
            }
        }
    }
} 