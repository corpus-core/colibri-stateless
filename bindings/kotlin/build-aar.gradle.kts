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
version = "1.0.0" // Adjust as needed

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
                    "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
                )
                // Add compiler flags for C++20 support
                cppFlags += "-std=c++20"
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
    
    ndkVersion = "23.1.7779620" // Adjust to your NDK version

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
                version = "1.0.0"
            }
        }
        repositories {
            maven {
                url = uri("https://your.maven.repo") // Replace with your Maven repository URL
                credentials {
                    username = project.findProperty("mavenUsername")?.toString()
                    password = project.findProperty("mavenPassword")?.toString()
                }
            }
        }
    }
}