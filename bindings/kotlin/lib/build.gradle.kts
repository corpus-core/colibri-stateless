import java.util.jar.JarFile

plugins {
    alias(libs.plugins.kotlin.jvm)
    `java-library`
}

repositories {
    mavenCentral()
}

dependencies {
    api(libs.commons.math3)
    implementation(libs.guava)
    implementation("io.ktor:ktor-client-core:2.0.0")
    implementation("io.ktor:ktor-client-cio:2.0.0") // CIO engine for asynchronous requests
    implementation("io.ktor:ktor-client-json:2.0.0")
    implementation("io.ktor:ktor-client-serialization:2.0.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.6.0")
    implementation("org.json:json:20210307")

}

testing {
    suites {
        val test by getting(JvmTestSuite::class) {
            useKotlinTest("2.0.21")
        }
    }
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(21)
    }
}

// Define a property for the build directory with a default value
val customBuildDir: String = findProperty("customBuildDir") as? String ?: "${projectDir}/../../../build"
val swigClassesDir = file("$customBuildDir/bindings/kotlin/classes")
val swigLibDir = file("$customBuildDir/bindings/kotlin")

configurations {
    create("swigImplementation")
}

dependencies {
    "swigImplementation"(files(swigClassesDir))
}

// Add SWIG-generated classes to the source sets
sourceSets {
    main {
        compileClasspath += configurations["swigImplementation"]
        runtimeClasspath += configurations["swigImplementation"]
        resources {
            srcDir(layout.buildDirectory.dir("libs"))
        }
        output.resourcesDir = layout.buildDirectory.dir("resources/main").get().asFile
    }
}

// Define platform-specific library names
val sharedLibraryName = when {
    org.gradle.internal.os.OperatingSystem.current().isWindows -> "c4_java.dll"
    org.gradle.internal.os.OperatingSystem.current().isMacOsX -> "libc4_java.jnilib"
    else -> "libc4_java.so"
}

// Define native library directory structure
val nativeLibraryPath = when {
    org.gradle.internal.os.OperatingSystem.current().isWindows -> "win32-x86-64"
    org.gradle.internal.os.OperatingSystem.current().isMacOsX -> {
        val arch = System.getProperty("os.arch").lowercase()
        when (arch) {
            "aarch64", "arm64" -> "darwin-aarch64"
            else -> "darwin-x86-64"
        }
    }
    else -> {
        val arch = System.getProperty("os.arch").lowercase()
        when (arch) {
            "aarch64", "arm64" -> "linux-aarch64"
            else -> "linux-x86-64"
        }
    }
}

// Create necessary directories
tasks.register("createDirectories") {
    val directories = listOf(
        layout.buildDirectory.dir("classes/java/main"),
        layout.buildDirectory.dir("resources/main"),
        layout.buildDirectory.dir("libs/native/$nativeLibraryPath")
    )
    
    outputs.dirs(directories)
    
    doLast {
        directories.forEach { dir ->
            dir.get().asFile.mkdirs()
        }
    }
}

// Task to copy the shared libraries
val copySharedLib by tasks.registering(Copy::class) {
    dependsOn("createDirectories")
    outputs.upToDateWhen { false }
    
    doFirst {
        logger.lifecycle("Copying shared library from: $swigLibDir")
        logger.lifecycle("Native directory exists: ${file("$swigLibDir/native").exists()}")
    }
    
    val nativeDir = file("$swigLibDir/native")
    if (nativeDir.exists()) {
        from(nativeDir)
        into(layout.buildDirectory.dir("libs/native"))
    } else {
        from(swigLibDir)
        include(sharedLibraryName)
        into(layout.buildDirectory.dir("libs/native/$nativeLibraryPath"))
    }
    
    doLast {
        logger.lifecycle("Copied shared library to: ${outputs.files.singleFile}")
    }
}

// Configure source sets and task dependencies
sourceSets {
    main {
        resources {
            srcDir(layout.buildDirectory.dir("libs"))
        }
    }
}

tasks.processResources {
    dependsOn(copySharedLib)
    // Handle duplicates in processResources
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
}

// Package the SWIG-generated classes and shared library with the JAR
tasks.jar {
    dependsOn(copySharedLib)
    outputs.upToDateWhen { false }
    
    // Handle duplicates
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    
    doFirst {
        logger.lifecycle("Including SWIG classes from: $swigClassesDir")
        logger.lifecycle("Including native libs from: ${layout.buildDirectory.dir("libs/native")}")
    }
    
    from(swigClassesDir) {
        into("") // Copy directly to root of JAR
    }
    from(layout.buildDirectory.dir("libs")) {
        include("native/**")
    }
    
    doLast {
        logger.lifecycle("JAR file created at: ${archiveFile.get().asFile.absolutePath}")
    }
}

// Add a task to verify the JAR contents
tasks.register("verifyJarContents") {
    dependsOn(tasks.jar)
    outputs.upToDateWhen { false }
    
    doLast {
        val jarFile = layout.buildDirectory.file("libs/lib.jar").get().asFile
        logger.lifecycle("Checking contents of JAR: ${jarFile.absolutePath}")
        if (jarFile.exists()) {
            val jar = JarFile(jarFile)
            try {
                val entries = jar.entries()
                while (entries.hasMoreElements()) {
                    val entry = entries.nextElement()
                    if (!entry.isDirectory) {
                        logger.lifecycle("JAR entry: ${entry.name}")
                    }
                }
            } finally {
                jar.close()
            }
        } else {
            logger.lifecycle("JAR file not found at: ${jarFile.absolutePath}")
        }
    }
}

tasks.test {
    dependsOn(copySharedLib)
    
    val nativeLibPath = layout.buildDirectory.dir("libs/native/$nativeLibraryPath").get().asFile.absolutePath
    systemProperty("java.library.path", nativeLibPath)
    
    environment(
        when {
            org.gradle.internal.os.OperatingSystem.current().isMacOsX -> "DYLD_LIBRARY_PATH"
            org.gradle.internal.os.OperatingSystem.current().isLinux -> "LD_LIBRARY_PATH"
            else -> "PATH"
        },
        nativeLibPath
    )
}

// Print classpath without causing configuration cache issues
tasks.register("printClasspath") {
    doLast {
        println(sourceSets.main.get().runtimeClasspath.files.joinToString(separator = ":"))
    }
}

tasks.register("debugClassDir") {
    doLast {
        val dir = file("/Users/simon/ws/c4/build/bindings/kotlin/classes")
        println("Directory exists: ${dir.exists()}")
        println("Is directory: ${dir.isDirectory}")
        println("Has c4.class: ${file("/Users/simon/ws/c4/build/bindings/kotlin/classes/com/corpuscore/colibri/c4.class").exists()}")
    }
}

// Verify task to check for all required platform libraries (CI only)
tasks.register("verifyNativeLibraries") {
    onlyIf {
        // Only run in CI environment
        System.getenv("CI") == "true"
    }
    doLast {
        val requiredPlatforms = listOf(
            "native/win32-x86-64/c4_java.dll",
            "native/darwin-x86-64/libc4_java.dylib",
            "native/darwin-aarch64/libc4_java.dylib",
            "native/linux-x86-64/libc4_java.so",
            "native/linux-aarch64/libc4_java.so"
        )
        
        val nativeLibsDir = layout.buildDirectory.dir("libs").get().asFile
        requiredPlatforms.forEach { platform ->
            if (!File(nativeLibsDir, platform).exists()) {
                throw GradleException("Missing native library: $platform")
            }
        }
    }
}

// Verify class directory without configuration cache issues
tasks.register("verifyClassDir") {
    dependsOn(copySharedLib)
    outputs.upToDateWhen { false }
    
    doLast {
        val classesExists = file(swigClassesDir).exists()
        val libsExists = layout.buildDirectory.dir("libs").get().asFile.exists()
        
        logger.lifecycle("SWIG classes directory exists: $classesExists")
        logger.lifecycle("Build libs directory exists: $libsExists")
        
        if (classesExists) {
            file(swigClassesDir).walk()
                .filter { it.isFile }
                .forEach { logger.lifecycle("Found class file: ${it.relativeTo(file(swigClassesDir))}") }
        }
        
        if (libsExists) {
            layout.buildDirectory.dir("libs").get().asFile.walk()
                .filter { it.isFile }
                .forEach { logger.lifecycle("Found lib file: ${it.relativeTo(layout.buildDirectory.dir("libs").get().asFile)}") }
        }
    }
}