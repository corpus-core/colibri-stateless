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

sourceSets {
    main {
        java {
            srcDir("src/main/java")
            srcDir("${projectDir}/generated/java")
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