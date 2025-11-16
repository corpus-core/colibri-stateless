repositories {
    google()
    mavenCentral()
}

plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
    id("maven-publish")
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
println("Building AAR with version: $version")

// Read README for package description
val readmeFile = project.projectDir.resolve("README.md")
val packageDescription = if (readmeFile.exists()) {
    readmeFile.readText()
} else {
    "Colibri Stateless - Kotlin/Java Bindings (AAR)"
}

// Configure the path to generated Java sources
val generatedSourcesPath = project.findProperty("generatedSourcesPath")?.toString() 
    ?: "${projectDir}/../../build/bindings/kotlin/java"

android {
    namespace = "com.corpuscore.colibri"
    compileSdk = 33
    defaultConfig {
        minSdk = 21
        
        externalNativeBuild {
            cmake {
                // Pass arguments to CMake
                arguments(
                    "-DGENERATE_JAVA_SOURCES=OFF", 
                    "-DKOTLIN=true", 
                    "-DCURL=false",
                    // Force C++20 with concepts
                    "-DCMAKE_CXX_STANDARD=20", 
                    "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
                    // Additional flags to help with Android NDK C++20 compilation
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_TOOLCHAIN=clang"
                )
                // Add compiler flags for C++20 support
                cppFlags += "-std=c++20"
                // Work around NDK C++20 issues with explicit flags
                cppFlags += "-Wno-c++20-extensions -Wno-c++20-designator"
                // Specify ABIs to build for
                abiFilters("armeabi-v7a", "arm64-v8a", "x86", "x86_64")
            }
        }
    }
    
    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
        }
    }
    ndkVersion = "26.1.10909125" // Latest stable version

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    kotlinOptions {
        jvmTarget = "1.8"
    }
    
    lint {
        disable += "DuplicatePlatformClasses"  // Disable lint check for JSON library conflict
    }
    
    sourceSets {
        getByName("main") {
            java {
                srcDirs(file(generatedSourcesPath))
            }
            kotlin {
                srcDirs(file("${projectDir}/lib/src/main/kotlin"))
            }
        }
    }

    publishing {
        singleVariant("release") {
            withSourcesJar()
            withJavadocJar()
        }
    }
}

dependencies {
    api(libs.commons.math3)
    implementation(libs.guava)
    implementation("io.ktor:ktor-client-core:2.0.0")
    implementation("io.ktor:ktor-client-cio:2.0.0") // CIO engine for asynchronous requests
    implementation("io.ktor:ktor-client-json:2.0.0")
    implementation("io.ktor:ktor-client-serialization:2.0.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.6.0")
    implementation("org.jetbrains.kotlin:kotlin-stdlib")
    implementation("org.json:json:20210307")  // Add explicit JSON dependency to match JAR build
}

afterEvaluate {
    publishing {
        publications {
            register<MavenPublication>("release") {
                from(components["release"])
                groupId = "com.corpuscore"
                artifactId = "colibri-aar"
                version = project.version.toString()
                
                pom {
                    name.set("Colibri Stateless - Kotlin/Java Bindings (AAR)")
                    description.set(packageDescription)
                    url.set("https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/kotlin-java")
                    
                    licenses {
                        license {
                            name.set("MIT License")
                            url.set("https://opensource.org/licenses/MIT")
                        }
                    }
                    
                    developers {
                        developer {
                            id.set("corpus-core")
                            name.set("Corpus Core Team")
                            email.set("simon@corpus.io")
                        }
                    }
                    
                    scm {
                        connection.set("scm:git:git://github.com/corpus-core/colibri-stateless.git")
                        developerConnection.set("scm:git:ssh://github.com/corpus-core/colibri-stateless.git")
                        url.set("https://github.com/corpus-core/colibri-stateless")
                    }
                }
            }
        }
        repositories {
            maven {
                name = "GitHubPackages"
                url = uri("https://maven.pkg.github.com/corpus-core/colibri-stateless")
                credentials {
                    username = System.getenv("GITHUB_ACTOR") ?: project.findProperty("githubActor")?.toString()
                    password = System.getenv("GITHUB_TOKEN") ?: project.findProperty("githubToken")?.toString()
                }
            }
            // Optional: Maven Central (for later)
            maven {
                name = "MavenCentral"
                url = uri("https://your.maven.repo") // Replace with your Maven repository URL
                credentials {
                    username = project.findProperty("mavenUsername")?.toString()
                    password = project.findProperty("mavenPassword")?.toString()
                }
            }
        }
    }
}