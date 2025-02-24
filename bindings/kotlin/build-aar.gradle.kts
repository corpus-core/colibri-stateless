plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
    id("maven-publish")
}

group = "com.corpuscore"
version = "1.0.0" // Adjust as needed

android {
    compileSdk = 33
    defaultConfig {
        minSdk = 21
        targetSdk = 33
    }
    externalNativeBuild {
        cmake {
            path = file("CMakeLists.txt")
            // Pass argument to skip Java source generation, as they are pre-generated
            arguments.add("-DGENERATE_JAVA_SOURCES=OFF")
            arguments.add("-DKOTLIN=true")
            arguments.add("-DCURL=false")
            // Specify ABIs to build for
            abiFilters.addAll(listOf("armeabi-v7a", "arm64-v8a", "x86", "x86_64"))
        }
    }
    ndkVersion = "23.1.7779620" // Adjust to your NDK version
    sourceSets {
        getByName("main") {
            java.srcDirs(file("${projectDir}/generated/java"))
        }
    }
}

dependencies {
    implementation("org.jetbrains.kotlin:kotlin-stdlib")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.6.4") // Adjust version as needed
    implementation("io.ktor:ktor-client-core:2.3.5") // Adjust version as needed
    implementation("io.ktor:ktor-client-cio:2.3.5") // Adjust version as needed
}

publishing {
    publications {
        create<MavenPublication>("aar") {
            from(components["release"])
            artifactId = "colibri-aar"
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