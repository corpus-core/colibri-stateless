/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

package com.corpuscore.colibri

import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.util.Locale

object NativeLoader {
    private var loaded = false

    // Helper function matching the build script logic for directory naming
    private fun getOsArchIdentifier(): String {
        val osName = System.getProperty("os.name", "generic").lowercase(Locale.ENGLISH)
        val osArch = System.getProperty("os.arch", "generic").lowercase(Locale.ENGLISH)
        // Match the directory names used in build-jar.gradle.kts and CI
        return when {
            osName.contains("mac") && osArch == "aarch64" -> "darwin-aarch64"
            osName.contains("mac") && osArch == "x86_64" -> "darwin-x86_64" // Added x86_64 mac
            osName.contains("linux") && osArch == "amd64" -> "linux-x86_64"
            osName.contains("linux") && osArch == "aarch64" -> "linux-aarch64"
            osName.contains("win") -> "win32-x86_64" // Assuming x64 only for now
            else -> "unknown-${osName}-${osArch}" // More informative unknown
        }
    }

    fun loadLibrary() {
        if (loaded) return
        println("NativeLoader: Trying System.loadLibrary based on java.library.path...")
        try {
            System.loadLibrary("c4_java") // Relies on java.library.path being set correctly
            loaded = true
            println("NativeLoader: System.loadLibrary('c4_java') successful.")
        } catch (e: UnsatisfiedLinkError) {
             println("NativeLoader: System.loadLibrary('c4_java') FAILED: ${e.message}")
             // Optional: Fallback to JAR extraction logic if needed, but likely error is path setup
             // throw e // Re-throw if you only want to use java.library.path
             // --- JAR EXTRACTION LOGIC (Re-activating fallback) ---
             
             val osArchIdentifier = getOsArchIdentifier()
             if (osArchIdentifier.startsWith("unknown")) {
                 throw UnsatisfiedLinkError("NativeLoader: Unsupported OS/Arch combination: $osArchIdentifier")
             }

             val libExtension = when {
                 osArchIdentifier.startsWith("win") -> "dll"
                 osArchIdentifier.startsWith("darwin") -> "jnilib"
                 else -> "so"
             }
             val libPrefix = if (osArchIdentifier.startsWith("win")) "" else "lib"
             val libraryName = "${libPrefix}c4_java.$libExtension"

             // Construct the correct path using the osArchIdentifier
             val libraryPath = "/native/$osArchIdentifier/$libraryName"
             println("NativeLoader: Attempting to load library from JAR resource path: $libraryPath")

             // Create a temporary directory using createTempFile
             val tmpFile = File.createTempFile("libc4_java-", ".$libExtension") // Use createTempFile for safety
             // val tmpDir = tmpFile.parentFile // Get parent dir if needed for cleanup?
             val tmpLib = tmpFile // Use the created temp file directly

             // tmpLib.delete() // Delete the empty file created by createTempFile - **Incorrect, keep the file handle**
             println("NativeLoader: created tempfile: $tmpLib")
             println("NativeLoader: reading: $libraryPath")

             try {
                 NativeLoader::class.java.getResourceAsStream(libraryPath)?.use { input ->
                     FileOutputStream(tmpLib).use { output ->
                         input.copyTo(output)
                     }
                     println("NativeLoader: Extracted library to temporary file: ${tmpLib.absolutePath}")
                 } ?: throw UnsatisfiedLinkError("NativeLoader: Could not find native library resource at path: $libraryPath")
                 println("NativeLoader: copied successfully: $libraryPath")
                 
                 // Ensure the extracted file exists before loading
                 if (!tmpLib.exists() || tmpLib.length() == 0L) { // Also check if file is empty
                     throw UnsatisfiedLinkError("NativeLoader: Failed to extract library to ${tmpLib.absolutePath}")
                 }

                 println("NativeLoader: Library loading... from ${tmpLib.absolutePath}")
                 System.load(tmpLib.absolutePath)
                 println("NativeLoader: Library loaded successfully from ${tmpLib.absolutePath}")
                 loaded = true
                 
                 // Clean up the temporary files when the JVM exits
                 tmpLib.deleteOnExit()
             } catch (extractLoadError: Throwable) {
                  println("NativeLoader: JAR Extraction/Load FAILED: ${extractLoadError.message}")
                  // Chain the original error with the extraction error for better context
                  extractLoadError.addSuppressed(e)
                  throw extractLoadError // Throw the extraction/load error
             }
             
             // throw e // Removed: Don't re-throw original if fallback was attempted
        }
    }
} 