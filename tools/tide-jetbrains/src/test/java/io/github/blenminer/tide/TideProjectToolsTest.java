package io.github.blenminer.tide;

import junit.framework.TestCase;
import java.nio.file.Files;
import java.nio.file.Path;

public final class TideProjectToolsTest extends TestCase {
    private static void put(Path root, String file, String text) throws Exception {
        final Path path = root.resolve(file);
        Files.createDirectories(path.getParent());
        Files.writeString(path, text);
    }

    public void testPinsChooseBothToolsAndNeverFallBackToAnotherVersion() throws Exception {
        final Path root = Files.createTempDirectory("tide-project-tools-");
        final Path game = root.resolve("game"), installed = root.resolve("installed"), versions = root.resolve("versions");
        put(game, "tide.packages", "tide = 0.5.0 # this branch\n");
        put(versions, "0.5.0/VERSION", "0.5.0\n");
        put(installed, "VERSION", "0.6.0");
        for (final String name : new String[]{"tide", "tidels"}) {
            put(versions, "0.5.0/bin/" + Tide.exe(name), "test discovery only");
            put(installed, "bin/" + Tide.exe(name), "test discovery only");
            assertEquals(versions.resolve("0.5.0/bin").resolve(Tide.exe(name)),
                TideProjectTools.find(game, name, installed, versions).file());
        }
        put(game, "tide.packages", "tide = 0.7.0\n");
        assertTrue(TideProjectTools.find(game, "tidels", installed, versions).error().contains("0.7.0 is required"));
        put(game, "tide.packages", "tide = ../../outside\n");
        assertTrue(TideProjectTools.find(game, "tidels", installed, versions).error().contains("Invalid Tide version pin"));
    }

    public void testUnityCacheIsBoundToThePackageCommitEvenFromAssets() throws Exception {
        final Path root = Files.createTempDirectory("tide-project-tools-");
        final Path command = root.resolve("custom/bin").resolve(Tide.exe("tide"));
        put(root, "custom/bin/" + Tide.exe("tide"), "test discovery only");
        put(root, "custom/bin/" + Tide.exe("tidels"), "test discovery only");
        put(root, "Assets/tide.packages", "");
        final String lock = "{\"dependencies\":{\"com.tide.unity\":{\"source\":\"git\",\"version\":\"url#dev\",\"hash\":\"%s\"}}}";
        put(root, "Packages/packages-lock.json", lock.formatted("one"));
        put(root, "Library/Tide/toolchain.json", "{\"commit\":\"one\",\"tide\":\"" + command.toString().replace("\\", "\\\\") + "\"}");
        assertEquals(command, TideProjectTools.find(root.resolve("Assets/Game"), "tide", null, null).file());
        put(root, "Packages/packages-lock.json", lock.formatted("two"));
        assertTrue(TideProjectTools.find(root, "tidels", null, null).error().contains("Open this project in Unity"));
        put(root, "Library/Tide/version.json", "{\"commit\":\"two\",\"version\":\"0.5.0\"}");
        put(root, "versions/0.5.0/VERSION", "0.5.0");
        put(root, "versions/0.5.0/bin/" + Tide.exe("tidels"), "test discovery only");
        assertEquals(root.resolve("versions/0.5.0/bin").resolve(Tide.exe("tidels")),
            TideProjectTools.find(root, "tidels", null, root.resolve("versions")).file());
    }

    public void testLocalUnityPackageSelectionIsBoundToTheLockBytes() throws Exception {
        final Path root = Files.createTempDirectory("tide-project-tools-");
        final String lock = "{\"dependencies\":{\"com.tide.unity\":{\"source\":\"local\",\"version\":\"file:../engine\"}}}";
        put(root, "Packages/packages-lock.json", lock);
        final String hash = java.util.HexFormat.of().formatHex(java.security.MessageDigest.getInstance("SHA-256")
            .digest(lock.getBytes(java.nio.charset.StandardCharsets.UTF_8)));
        final Path command = root.resolve("custom/bin").resolve(Tide.exe("tide"));
        put(root, "custom/bin/" + Tide.exe("tidels"), "test discovery only");
        put(root, "Library/Tide/toolchain.json", "{\"lockHash\":\"" + hash + "\",\"tide\":\"" + command.toString().replace("\\", "\\\\") + "\"}");
        assertEquals(command.getParent().resolve(Tide.exe("tidels")), TideProjectTools.find(root, "tidels", null, null).file());
        put(root, "Packages/packages-lock.json", lock.replace("local", "git"));
        assertTrue(TideProjectTools.find(root, "tidels", null, null).error().contains("Open this project in Unity"));
    }
}
