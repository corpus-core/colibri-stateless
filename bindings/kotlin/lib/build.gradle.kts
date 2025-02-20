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

// Define the directories for SWIG-generated classes and shared libraries
val swigClassesDir = file("$customBuildDir/bindings/kotlin/classes")
val swigLibDir = file("$customBuildDir/bindings/kotlin/java")

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
    }
}

// Task to copy the shared library to the build/libs directory
val copySharedLib by tasks.registering(Copy::class) {
    from(swigLibDir)
    include("libc4_java.so") // Adjust the file extension if needed (e.g., .dll for Windows, .dylib for macOS)
    into(layout.buildDirectory.dir("libs"))
}

// Ensure the shared library is copied before building
tasks.named("build") {
    dependsOn(copySharedLib)
}

// Package the SWIG-generated classes and shared library with the JAR
tasks.named<Jar>("jar") {
    dependsOn(copySharedLib) // Ensure the shared library is copied before creating the JAR
    from(swigClassesDir)
    from(layout.buildDirectory.dir("libs")) {
        include("libc4_java.so") // Adjust the file extension if needed
    }
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