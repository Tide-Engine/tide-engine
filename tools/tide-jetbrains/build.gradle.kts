// The Tide plugin for JetBrains IDEs: the grammar from ../tide-syntax
// for highlighting, and tidels through LSP4IJ for everything else.
//
//   ./gradlew buildPlugin   the plugin, in build/distributions
//   ./gradlew runIde        an IDE with the plugin, to try it
//
// -PpluginVersion=<version> sets its version (CI passes tide's), and
// -PpluginChannel=nightly publishes to Marketplace's nightly channel.

import org.jetbrains.intellij.platform.gradle.IntelliJPlatformType
import org.jetbrains.intellij.platform.gradle.TestFrameworkType

plugins {
    id("java")
    id("org.jetbrains.intellij.platform") version "2.19.0"
}

version = providers.gradleProperty("pluginVersion").getOrElse("0.0.0-dev")

repositories {
    mavenCentral()
    intellijPlatform {
        defaultRepositories()
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
    intellijPlatform {
        // The oldest platform LSP4IJ supports, so the plugin runs from there on.
        intellijIdeaCommunity("2024.2.6")
        bundledPlugin("org.jetbrains.plugins.textmate")
        plugin("com.redhat.devtools.lsp4ij", "0.21.0")
        testFramework(TestFrameworkType.Platform)
    }
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(21) // Platform 2024.2's
    }
}

intellijPlatform {
    pluginConfiguration {
        ideaVersion {
            sinceBuild = "242"
            untilBuild = provider { null }
        }
    }
    // verifyPlugin checks it against the oldest IDE it supports and the newest
    // CLion, or with -PverifyIde=<folder>, against an installed IDE.
    pluginVerification {
        ides {
            val installed = providers.gradleProperty("verifyIde").orNull
            if (installed != null) {
                local(installed)
            } else {
                current()
                latest { types = listOf(IntelliJPlatformType.CLion) }
            }
        }
    }
    publishing {
        token = providers.environmentVariable("JETBRAINS_MARKETPLACE_TOKEN")
        channels = providers.gradleProperty("pluginChannel").map { listOf(it) }.orElse(listOf("default"))
    }
    buildSearchableOptions = false
    instrumentCode = false
}

tasks {
    compileTestJava {
        options.compilerArgs.add("-Xlint:deprecation")
    }
    // Packaging (including the manually requested CI build) runs the adapter
    // regressions too, including the isolated IntelliJ test application.
    buildPlugin {
        dependsOn(test)
    }
    test {
        // Optional real processes/JCEF. The ordinary platform tests remain
        // headless and do not download or launch a game or external browser.
        providers.gradleProperty("tideTestNode").orNull?.let { systemProperty("tide.test.node", it) }
        providers.gradleProperty("tideTestServer").orNull?.let { systemProperty("tide.test.server", it) }
        filter {
            if (!providers.gradleProperty("tideTestNode").isPresent) excludeTestsMatching("*.TideHostTest.testRealRunConfigurationKillsDescendants")
            if (!providers.gradleProperty("tideTestServer").isPresent) excludeTestsMatching("*.TideHostTest.testRealLsp4ijClientStartsRestartsAndNavigates")
            if (providers.gradleProperty("tideTestJcef").orNull != "true") excludeTestsMatching("*.TideHostTest.testRealJcefLoadsAndReusesPage")
        }
        if (providers.gradleProperty("tideTestJcef").orNull == "true") {
            systemProperty("tide.test.jcef", "true")
            systemProperty("ide.browser.jcef.headless.enabled", "true")
            systemProperty("ide.browser.jcef.extra.args", "--mute-audio")
        }
    }
    // The test IDE opens projects without asking.
    runIde {
        jvmArgs("-Didea.trust.all.projects=true", "-Djb.consents.confirmation.enabled=false")
    }
    // The grammar goes next to the plugin's jar, where the IDE reads it (see TideTextMateBundle).
    withType<org.jetbrains.intellij.platform.gradle.tasks.PrepareSandboxTask>().configureEach {
        from(layout.projectDirectory.dir("../tide-syntax")) {
            into("tide/textmate")
            exclude("test/**")
        }
    }
}
