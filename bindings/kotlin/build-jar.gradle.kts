plugins {
    id("java-library")
    id("org.jetbrains.kotlin.jvm") version "1.9.24"
    id("maven-publish")
}

repositories {
    mavenCentral()
}

group = "com.corpuscore"
version = "1.0.0" // Adjust as needed

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
}

tasks.jar {
    from("${projectDir}/native-libs") {
        into("native")
    }
}

publishing {
    publications {
        create<MavenPublication>("jar") {
            from(components["java"])
            artifactId = "colibri-jar"
        }
    }
    repositories {
        maven {
            url = uri("https://your.maven.repo") // Replace with your Maven repository URL
            credentials {
                username = project.findProperty("mavenUsername") as String?
                password = project.findProperty("mavenPassword") as String?
            }
        }
    }
}