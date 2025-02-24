rootProject.name = "colibri-jar"

pluginManagement {
    repositories {
        gradlePluginPortal()
        google()
        mavenCentral()
    }
    plugins {
        id("com.android.library") version "8.1.0"
        id("org.jetbrains.kotlin.android") version "1.9.24"
    }
}