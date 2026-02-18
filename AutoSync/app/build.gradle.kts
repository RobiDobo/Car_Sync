import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import java.util.Properties
import java.io.FileInputStream

// Function to load properties from local.properties
fun getLocalProperty(key: String, projectDir: java.io.File): String {
    val properties = Properties()
    val localPropertiesFile = projectDir.resolve("local.properties")
    if (localPropertiesFile.exists()) {
        properties.load(FileInputStream(localPropertiesFile))
    }
    return properties.getProperty(key, "")
}

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization) // <-- ADD THIS
}

android {
    namespace = "com.example.autosync"
    compileSdk {
        version = release(36)
    }

    defaultConfig {
        applicationId = "com.example.autosync"
        minSdk = 24
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            buildConfigField("String", "WEB_CLIENT_ID", getLocalProperty("WEB_CLIENT_ID", rootDir))
            buildConfigField("String", "GITHUB_PAT", getLocalProperty("GITHUB_PAT", rootDir))
            buildConfigField("String", "REPO_OWNER", getLocalProperty("REPO_OWNER", rootDir))
            buildConfigField("String", "REPO_NAME", getLocalProperty("REPO_NAME", rootDir))
        }
        debug {
            buildConfigField("String", "WEB_CLIENT_ID", getLocalProperty("WEB_CLIENT_ID", rootDir))
            buildConfigField("String", "GITHUB_PAT", getLocalProperty("GITHUB_PAT", rootDir))
            buildConfigField("String", "REPO_OWNER", getLocalProperty("REPO_OWNER", rootDir))
            buildConfigField("String", "REPO_NAME", getLocalProperty("REPO_NAME", rootDir))
        }
    }
    packaging {
        resources.excludes.add("META-INF/INDEX.LIST")
        resources.excludes.add("META-INF/DEPENDENCIES")
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    // ADD THIS NEW RECOMMENDED BLOCK
    kotlin {
        compilerOptions {
            jvmTarget.set(JvmTarget.JVM_11) // Set the JVM target using the DSL
        }
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.ui)
    implementation(libs.androidx.ui.graphics)
    implementation(libs.androidx.ui.tooling.preview)
    implementation(libs.androidx.material3)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.ui.test.junit4)
    debugImplementation(libs.androidx.ui.tooling)
    debugImplementation(libs.androidx.ui.test.manifest)


    // Networking (Retrofit, Serialization, Converter)
    implementation(libs.retrofit.core)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.retrofit.kotlinx.converter)

    // Google Sign-In (OAuth)
    implementation(libs.google.gms.auth)

    // Lifecycle KTX for ViewModel scopes
    implementation(libs.androidx.lifecycle.viewmodel.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)

    // YouTube client library
    implementation(libs.google.api.client.android)
    implementation(libs.google.api.services.youtube)

}