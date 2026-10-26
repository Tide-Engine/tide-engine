// The Tide plugin for JetBrains IDEs: the shared or bundled TextMate grammar
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

// Engine builds use the live shared grammar; standalone public clones use the
// snapshot refreshed before exporting tools/tide-jetbrains.
val sharedTextmate = layout.projectDirectory.dir("../tide-syntax")
val textmate = if (sharedTextmate.file("package.json").asFile.isFile) sharedTextmate
    else layout.projectDirectory.dir("textmate")

repositories {
    mavenCentral()
    intellijPlatform {
        defaultRepositories()
    }
}

// CLion's native debugger's breakpoint extension point, to compile TideBreakpoints against: the IDE the
// plugin builds on (IntelliJ Community) hasn't that debugger, and CLion supplies the real interface at run time
// (tide-cidr.xml). Never packaged; verifyPlugin checks the plugin against CLion's.
val cidrStubs: SourceSet = sourceSets.create("cidrStubs")

dependencies {
    compileOnly(cidrStubs.output)
    testImplementation(cidrStubs.output) // For TideBreakpoints in the IDE the tests run, which has no CLion
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
    // Marketplace's archive stays a single plugin. Tide's offline installer
    // also carries the pinned LSP4IJ dependency, already in the sandbox.
    register<Zip>("buildCliInstaller") {
        dependsOn(buildPlugin)
        archiveFileName = "tide-jetbrains-installer.zip"
        destinationDirectory = layout.buildDirectory.dir("cli")
        from(resources.text.fromString("${project.version}\n")) { rename { "VERSION" } }
        from("cli-licenses") { into("lsp4ij/licenses") }
        from(prepareSandbox.flatMap { it.pluginDirectory }) { into("tide") }
        val sandboxDirectory = prepareSandbox.flatMap { it.defaultDestinationDirectory }
        from(sandboxDirectory) {
            include("lsp4ij/**")
        }
        doFirst {
            val plugins = sandboxDirectory.get().asFile
            check(plugins.resolve("lsp4ij/lib").isDirectory) { "Missing LSP4IJ in the CLI installer" }
        }
    }
    processResources {
        from("LICENSE") { into("META-INF") }
    }
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
        providers.gradleProperty("tideTestEditor").orNull?.let { systemProperty("tide.test.editor", it) }
        filter {
            if (!providers.gradleProperty("tideTestNode").isPresent) excludeTestsMatching("*.TideHostTest.testRealRunConfigurationKillsDescendants")
            if (!providers.gradleProperty("tideTestServer").isPresent) excludeTestsMatching("*.TideHostTest.testRealLsp4ijClientStartsRestartsAndNavigates")
            if (!providers.gradleProperty("tideTestEditor").isPresent) excludeTestsMatching("*.TideHostTest.testNativePrefabCanvasUsesTheRealBackend")
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
        from(textmate) {
            into("tide/textmate")
            exclude("test/**")
        }
    }
}
