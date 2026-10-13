package io.github.blenminer.tide;

import com.intellij.execution.configurations.ConfigurationTypeUtil;
import com.intellij.execution.configurations.RuntimeConfigurationError;
import com.intellij.execution.actions.ConfigurationContext;
import com.intellij.openapi.actionSystem.ActionManager;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.actionSystem.ActionPlaces;
import com.intellij.openapi.actionSystem.CommonDataKeys;
import com.intellij.openapi.actionSystem.impl.SimpleDataContext;
import com.intellij.execution.filters.Filter;
import com.intellij.execution.filters.OpenFileHyperlinkInfo;
import com.intellij.openapi.vfs.LocalFileSystem;
import com.intellij.psi.PsiManager;
import com.intellij.testFramework.fixtures.BasePlatformTestCase;

import java.nio.file.Files;
import java.nio.file.Path;

// Real IntelliJ settings, VFS, console hyperlinks and configuration storage.
public final class TidePlatformTest extends BasePlatformTestCase {
    public void testNativePrefabActionIsRegisteredAndOnlyOfferedForPrefabs() throws Exception {
        final var action = ActionManager.getInstance().getAction("Tide.OpenPrefab");
        assertNotNull(action);
        final Path folder = Files.createTempDirectory("tide-platform-");
        for (final String name : new String[]{"Level.tprefab", "game.tide"}) {
            final Path file = folder.resolve(name); Files.writeString(file, "");
            final var virtual = LocalFileSystem.getInstance().refreshAndFindFileByNioFile(file);
            final var data = SimpleDataContext.builder().add(CommonDataKeys.PROJECT, getProject())
                .add(CommonDataKeys.VIRTUAL_FILE, virtual).build();
            final var event = AnActionEvent.createFromAnAction(action, null, ActionPlaces.UNKNOWN, data);
            action.update(event);
            assertEquals(name.endsWith(".tprefab"), event.getPresentation().isEnabledAndVisible());
        }
    }
    public void testConsoleHyperlinkUsesFileAndOneBasedCoordinates() throws Exception {
        final Path folder = Files.createTempDirectory("tide-platform-");
        Files.writeString(folder.resolve("game file.tide"), "scene Main {}\n// target\n");
        final var file = LocalFileSystem.getInstance().refreshAndFindFileByNioFile(folder.resolve("game file.tide"));
        final String line = "game file.tide:2:3: error: test\n";
        final Filter.Result result = new TideErrorFilter(getProject(), folder).applyFilter(line, 100 + line.length());
        assertNotNull(result);
        assertEquals(100, result.getResultItems().getFirst().getHighlightStartOffset());
        final var link = (OpenFileHyperlinkInfo) result.getResultItems().getFirst().getHyperlinkInfo();
        assertEquals(file, link.getDescriptor().getFile());
        // The platform turns existing document coordinates into an offset.
        assertEquals("scene Main {}\n".length() + 2, link.getDescriptor().getOffset());
        assertNull(new TideErrorFilter(getProject(), folder).applyFilter("absent.tide:1:1: error: test\n", 40));
        assertNull(new TideErrorFilter(getProject(), folder).applyFilter("game file.tide:99999999999:1: error: test\n", 60));
    }

    public void testRunConfigurationCloneKeepsOptionsAndValidation() throws Exception {
        final var type = ConfigurationTypeUtil.findConfigurationType(TideRunConfigurationType.class);
        final var factory = type.getConfigurationFactories()[0];
        final var configuration = (TideRunConfiguration) factory.createTemplateConfiguration(getProject());
        configuration.getOptions().setFolder(Files.createTempDirectory("tide-platform-").toString());
        configuration.getOptions().setWeb(true);
        configuration.checkConfiguration();
        final var copy = (TideRunConfiguration) configuration.clone();
        assertEquals(configuration.getOptions().getFolder(), copy.getOptions().getFolder());
        assertTrue(copy.getOptions().isWeb());
        copy.getOptions().setAndroid(true);
        assertFalse(configuration.getOptions().isAndroid());
        assertTrue(copy.suggestedName().endsWith(" (Android)"));
        copy.getOptions().setProblem("Use the CMake target for this game");
        try {
            copy.checkConfiguration();
            fail("Must preserve the producer's explanation");
        } catch (RuntimeConfigurationError error) {
            assertEquals("Use the CMake target for this game", error.getMessageHtml().toString());
        }
        copy.getOptions().setProblem("");
        copy.getOptions().setFolder("bad\0folder");
        try {
            copy.checkConfiguration();
            fail("Invalid paths must give a configuration error");
        } catch (RuntimeConfigurationError error) {
            assertTrue(error.getMessageHtml().toString().contains("valid"));
        }
    }

    public void testServerSettingsAndEnablementUsePlatformServices() throws Exception {
        final var settings = TideSettings.get();
        final String previous = settings.tidels();
        try {
            final Path exe = Files.createTempDirectory("tide-platform-").resolve("tidels.exe");
            Files.writeString(exe, "test discovery only");
            settings.setTidels("  " + exe + "  ");
            assertEquals(exe, TidelsFactory.find(getProject()).server());
            final var factory = new TidelsFactory();
            factory.setEnabled(true, getProject());
            assertTrue(factory.isEnabled(getProject()));
            factory.setEnabled(false, getProject());
            assertFalse(factory.isEnabled(getProject()));
            factory.setEnabled(true, getProject());
            assertTrue(factory.isEnabled(getProject()));
            settings.setTidels(exe + ".missing");
            assertNull(TidelsFactory.find(getProject()).server());
            assertTrue(TidelsFactory.find(getProject()).problem().contains("There's no tidels"));
        } finally {
            settings.setTidels(previous);
        }
    }

    public void testGameFolderContractsWithRealFiles() throws Exception {
        final Path root = Files.createTempDirectory("tide-platform-");
        final Path game = Files.createDirectories(root.resolve("a game"));
        Files.writeString(game.resolve("tide.packages"), "# empty game manifest\n");
        assertEquals(game, TideGames.of(game.resolve("main.tide"), root).folder());
        final Path build = Files.createDirectories(root.resolve("build/tools"));
        Files.writeString(build.resolve("games.txt"), "game\t" + game.toString().replace('\\', '/') + "/\n");
        assertEquals(game, TideGames.of(game.resolve("nested/main.tide"), root).folder());
        assertNotNull(TideGames.of(root.resolve("standalone.tide"), root).error());
        Files.writeString(build.resolve("games.txt"), "game\t" + game.resolve("main.tide") + "\n");
        assertTrue(TideGames.of(game.resolve("main.tide"), root).error().contains("CMake"));
    }

    public void testRunMenuProducesAllThreeTargetsForTheActualTideFileType() throws Exception {
        final Path folder = Files.createTempDirectory("tide-platform-");
        final Path file = folder.resolve("main.tide");
        Files.writeString(file, "scene Main {}\n");
        final var virtual = LocalFileSystem.getInstance().refreshAndFindFileByNioFile(file);
        final var psi = PsiManager.getInstance(getProject()).findFile(virtual);
        assertNotNull(psi);
        final var context = new ConfigurationContext(psi);
        final var configurations = context.getConfigurationsFromContext().stream()
            .map(c -> c.getConfiguration()).filter(c -> c instanceof TideRunConfiguration)
            .map(c -> (TideRunConfiguration) c).toList();
        assertEquals(3, configurations.size());
        assertEquals(1, configurations.stream().filter(c -> c.getOptions().isWeb()).count());
        assertEquals(1, configurations.stream().filter(c -> c.getOptions().isAndroid()).count());
        assertEquals(1, configurations.stream().filter(c -> !c.getOptions().isWeb() && !c.getOptions().isAndroid()).count());
        for (final var configuration : configurations) {
            assertEquals(folder.toString(), configuration.getOptions().getFolder());
            configuration.checkConfiguration();
        }
    }
}
