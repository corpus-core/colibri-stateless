plugins {
    id("java-library")
    id("org.jetbrains.kotlin.jvm")
    id("maven-publish")
}

group = "com.corpuscore"
version = "1.0.0" // Adjust as needed

sourceSets {
    main {
        java {
            srcDirs = listOf("src/main/java", "${projectDir}/generated/java")
        }
        kotlin {
            srcDirs = listOf("lib/src/main/kotlin")
        }
    }
}

tasks.jar {
    // Include native libraries from native-libs/ into the JAR under the native directory
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
                username = project.findProperty("mavenUsername")?.toString()
                password = project.findProperty("mavenPassword")?.toString()
            }
        }
    }
}