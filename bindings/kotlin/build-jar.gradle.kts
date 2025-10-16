import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.TaskAction
import org.gradle.api.tasks.InputDirectory
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.tasks.Exec
import org.gradle.api.DefaultTask
import java.io.File
import java.util.Locale

plugins {
    id("java-library")
    id("org.jetbrains.kotlin.jvm") version "1.9.24"
    id("maven-publish")
}

// --- Host Native Build Configuration ---
// Define the directory for the host native build
val nativeTestBuildDir = project.layout.buildDirectory.dir("native-test")

// Task to create the host build directory
// Make configuration cache compatible by declaring outputs
abstract class CreateNativeTestDirTask : DefaultTask() { // Use abstract class
    @get:OutputDirectory // Declare the output directory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun createDir() {
        outputDir.get().asFile.mkdirs()
        println("Created directory for host native build: ${outputDir.get().asFile.absolutePath}")
    }
}

val createNativeTestDir by tasks.register<CreateNativeTestDirTask>("createNativeTestDir") {
    outputDir.set(nativeTestBuildDir) // Assign the provider to the property
}

// Task to configure the native build using CMake (for host)
// Assumes root CMakeLists.txt can handle non-Android builds.
val configureNativeTestBuild by tasks.register<Exec>("configureNativeTestBuild") {
    dependsOn(createNativeTestDir)
    // Declare the output directory of the previous task as an input using inputs.files
    inputs.files(createNativeTestDir)
    workingDir(nativeTestBuildDir.get().asFile)
    val cmakeListsDir = project.projectDir.resolve("../..").normalize()
    // Arguments for HOST build (NO ANDROID args)
    // Adjust flags as needed for your CMake setup (e.g., CMAKE_BUILD_TYPE)
    commandLine(
        "cmake",
        "-DKOTLIN=true", // Keep necessary flags
        "-DCURL=false",
        // "-DCMAKE_BUILD_TYPE=Debug", // Example
        cmakeListsDir.absolutePath
    )
    doFirst { 
        println("Configuring host native build in: ${workingDir.absolutePath}")
        println("CMake command: ${commandLine.joinToString(" ")}")
    }
}

// Task to run the actual native build (using cmake --build)
val buildNativeTestLib by tasks.register<Exec>("buildNativeTestLib") {
    dependsOn(configureNativeTestBuild)
    workingDir(nativeTestBuildDir.get().asFile)
    commandLine("cmake", "--build", ".", "--parallel", "${Runtime.getRuntime().availableProcessors()}")
     doFirst { 
        println("Building host native library in: ${workingDir.absolutePath}")
        println("Build command: ${commandLine.joinToString(" ")}")
    }
}
// --- End Host Native Build Configuration ---

// Configure JVM Toolchain to ensure Java and Kotlin use the same JDK version
kotlin {
    jvmToolchain(21) // Use Java 21 for both Kotlin and Java compilation
}

repositories {
    mavenCentral()
}

group = "com.corpuscore"

// Dynamic version based on Git tag or branch
fun getProjectVersion(): String {
    return try {
        val tagVersion = providers.exec {
            commandLine("git", "describe", "--tags", "--exact-match", "HEAD")
            isIgnoreExitValue = true
        }.standardOutput.asText.get().trim()
        
        if (tagVersion.isNotEmpty() && !tagVersion.contains("fatal")) {
            tagVersion.removePrefix("v")
        } else {
            // Not on a tag, check if on dev branch
            val branchName = providers.exec {
                commandLine("git", "rev-parse", "--abbrev-ref", "HEAD")
                isIgnoreExitValue = true
            }.standardOutput.asText.get().trim()
            
            val baseVersion = "1.0.0"
            if (branchName == "dev") {
                "$baseVersion-SNAPSHOT"
            } else {
                "$baseVersion-${branchName}-SNAPSHOT"
            }
        }
    } catch (e: Exception) {
        println("Warning: Could not determine version from git: ${e.message}")
        "1.0.0-SNAPSHOT"
    }
}

version = getProjectVersion()
println("Building JAR with version: $version")

// Configure the path to generated Java sources
val generatedSourcesPath = project.findProperty("generatedSourcesPath")?.toString() 
    ?: "${projectDir}/../../build/bindings/kotlin/java"

dependencies {
    api(libs.commons.math3)
    implementation(libs.guava)
    implementation("io.ktor:ktor-client-core:2.0.0")
    implementation("io.ktor:ktor-client-cio:2.0.0") // CIO engine for asynchronous requests
    implementation("io.ktor:ktor-client-json:2.0.0")
    implementation("io.ktor:ktor-client-serialization:2.0.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.6.0")
    implementation("org.json:json:20210307")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.junit.jupiter:junit-jupiter-api:5.9.2")
    testImplementation("org.junit.jupiter:junit-jupiter-engine:5.9.2")
    testImplementation("org.junit.jupiter:junit-jupiter-params:5.9.2") // For @ParameterizedTest
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.6.0")
    testImplementation("org.json:json:20210307") // Also needed for test compilation
}

sourceSets {
    main {
        java {
            srcDir(file(generatedSourcesPath))
        }
        kotlin {
            srcDir("lib/src/main/kotlin")
        }
    }
    test {
        kotlin {
            srcDir("lib/src/test/kotlin")
        }
        // If tests need resources, configure them here too
        // resources {
        //     srcDir("lib/src/test/resources") 
        // }
    }
}

// --- Task to copy locally built native library --- 
// Detect Host OS and Arch to determine library name and path
val hostOs = System.getProperty("os.name").lowercase(Locale.ENGLISH)
val hostArch = System.getProperty("os.arch")
val libExtension = when {
    hostOs.contains("win") -> "dll"
    hostOs.contains("mac") -> "jnilib"
    else -> "so"
}
val libPrefix = if (hostOs.contains("win")) "" else "lib"
val localLibName = "${libPrefix}c4_java.$libExtension"

// Determine the OS/Arch identifier used in native-libs folder (must match CI structure)
val osArchIdentifier = when {
    hostOs.contains("mac") && hostArch == "aarch64" -> "darwin-aarch64"
    hostOs.contains("mac") && hostArch == "x86_64" -> "darwin-x86_64" // Assuming this might exist
    hostOs.contains("linux") && hostArch == "amd64" -> "linux-x86_64"
    hostOs.contains("linux") && hostArch == "aarch64" -> "linux-aarch64"
    hostOs.contains("win") -> "win32-x86_64"
    else -> "unknown"
}

val copyLocalNativeLib by tasks.register<Copy>("copyLocalNativeLib") {
    if (osArchIdentifier != "unknown") {
        // --- Correct the source path calculation --- 
        // Path needs to be relative to the WORKSPACE ROOT, not the project build dir.
        // project.rootDir points to bindings/kotlin because that's where settings.gradle.kts is.
        // Go up two levels to get to the workspace root.
        val workspaceRoot = project.rootDir.parentFile.parentFile 
        // Determine build directory based on OS (matches CI structure)
        val nativeBuildDirName = when {
             hostOs.contains("mac") -> "build-macos"
             hostOs.contains("linux") -> "build-linux" // Add linux-aarch64? Needs more specific check maybe
             hostOs.contains("win") -> "build-windows"
             else -> "build-unknown" // Fallback
        }
        // Source path within the OS-specific build directory
        val sourcePath = File(workspaceRoot, "$nativeBuildDirName/bindings/kotlin/$localLibName")
        // Special case for Windows Release config path
        if (hostOs.contains("win")) {
             // sourcePath = File(workspaceRoot, "$nativeBuildDirName/bindings/kotlin/Release/$localLibName") // Adjust if needed based on exact windows output
        }
        // --- End path correction ---

        val destPath = project.projectDir.resolve("native-libs/$osArchIdentifier")
        
        println("Configuring copyLocalNativeLib: Copying from ${sourcePath.absolutePath} to $destPath")
        
        from(sourcePath) { // Use File directly now
           rename { localLibName } // Ensure the name is correct in destination
        }
        into(destPath)
        // Only run if the source file actually exists (built locally)
        onlyIf { sourcePath.exists() }
        // Ensure destination directory exists
        doFirst { destPath.mkdirs() }
    } else {
        println("Skipping copyLocalNativeLib: Unknown OS/Arch combination ($hostOs / $hostArch)")
        enabled = false
    }
}
// --- End Task --- 

tasks.jar {
    from("${projectDir}/native-libs") {
        into("native")
    }
    // Make sure the local lib is copied before jarring
    dependsOn(copyLocalNativeLib)
}

publishing {
    publications {
        create<MavenPublication>("jar") {
            from(components["java"])
            artifactId = "colibri-jar"
            groupId = "tech.corpuscore"
            version = project.version.toString()
        }
    }
    repositories {
        maven {
            name = "GitHubPackages"
            url = uri("https://maven.pkg.github.com/corpus-core/colibri-stateless")
            credentials {
                username = System.getenv("GITHUB_ACTOR") ?: project.findProperty("githubActor") as String?
                password = System.getenv("GITHUB_TOKEN") ?: project.findProperty("githubToken") as String?
            }
        }
        // Optional: Maven Central (for later)
        maven {
            name = "MavenCentral"
            url = uri("https://your.maven.repo") // Replace with your Maven repository URL
            credentials {
                username = project.findProperty("mavenUsername") as String?
                password = project.findProperty("mavenPassword") as String?
            }
        }
    }
}

// Configure test task logging
tasks.withType<Test> {
   // Make tests depend on the host native library build
   dependsOn(buildNativeTestLib)

   // Use JUnit 5 platform for parameterized tests alongside JUnit 4
   useJUnitPlatform()

   // Increase max heap size for the test JVM
   maxHeapSize = "4g" // Example: 4 gigabytes. Adjust as needed.

   // Set java.library.path for the test JVM
   // Point to where the host build outputs the library (e.g., build/native-test/bindings/kotlin/)
   // *** IMPORTANT: Verify this path after running buildNativeTestLib ***
   val hostLibOutputDir = nativeTestBuildDir.get().dir("bindings/kotlin").asFile.absolutePath 
   // If the lib is directly in nativeTestBuildDir, use:
   // val hostLibOutputDir = nativeTestBuildDir.get().asFile.absolutePath

   val currentPath = System.getProperty("java.library.path", "")
   val pathSeparator = System.getProperty("path.separator")
   systemProperty("java.library.path", listOf(hostLibOutputDir, currentPath).filter { it.isNotEmpty() }.joinToString(pathSeparator))
   println("Configuring test ${name} with java.library.path including: $hostLibOutputDir")

   testLogging {
       showStandardStreams = true
       events("passed", "skipped", "failed", "standardOut", "standardError")
       exceptionFormat = org.gradle.api.tasks.testing.logging.TestExceptionFormat.FULL
   }
   
   // Always run tests (even if up-to-date)
   outputs.upToDateWhen { false }
}