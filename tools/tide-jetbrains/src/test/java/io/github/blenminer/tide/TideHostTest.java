package io.github.blenminer.tide;

import com.intellij.execution.configurations.ConfigurationTypeUtil;
import com.intellij.execution.process.ProcessHandler;
import com.intellij.openapi.util.Disposer;
import com.intellij.openapi.wm.RegisterToolWindowTask;
import com.intellij.openapi.wm.ToolWindowManager;
import com.intellij.testFramework.PlatformTestUtil;
import com.intellij.testFramework.fixtures.BasePlatformTestCase;
import com.intellij.ui.jcef.JBCefApp;
import com.intellij.ui.jcef.JBCefBrowser;
import com.redhat.devtools.lsp4ij.LanguageServerManager;
import org.eclipse.lsp4j.CodeLensParams;
import org.eclipse.lsp4j.DidOpenTextDocumentParams;
import org.eclipse.lsp4j.HoverParams;
import org.eclipse.lsp4j.Position;
import org.eclipse.lsp4j.TextDocumentIdentifier;
import org.eclipse.lsp4j.TextDocumentItem;

import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;
import com.intellij.util.ui.UIUtil;

// Opt-in real process/browser boundaries, in the isolated IntelliJ application.
public final class TideHostTest extends BasePlatformTestCase {
    public void testNativePrefabCanvasUsesTheRealBackend() throws Exception {
        final Path folder=Files.createTempDirectory("tide-jb-prefab-");
        final Path file=folder.resolve("Level.tprefab");
        Files.writeString(folder.resolve("tide.packages"),"");
        Files.writeString(file,"prefab Level { Transform; }\n");
        final var panel=new TidePrefabEditor(Path.of(System.getProperty("tide.test.editor")),file,folder);
        try {
            panel.setSize(1440,900); panel.request();
            final var pixels=new java.awt.image.BufferedImage(1440,900,java.awt.image.BufferedImage.TYPE_INT_RGB);
            PlatformTestUtil.waitWithEventsDispatching("native prefab canvas",() -> {
                final var graphics=pixels.createGraphics();panel.paint(graphics);graphics.dispose();
                return (pixels.getRGB(20,140)&0xFFFFFF)!=0x131A20;
            },30);
            assertTrue(panel.process.isAlive());
            assertTrue(pixels.getRGB(20,140)!=pixels.getRGB(260,140));
        } finally { panel.dispose(); }
        PlatformTestUtil.waitWithEventsDispatching("native prefab EOF shutdown",() -> !panel.process.isAlive(),30);
        assertEquals(0,panel.process.exitValue());
    }
    @Override
    protected void setUp() throws Exception {
        super.setUp();
        // Light projects are reused across tests; their original temporary
        // base directory may have been removed by the preceding fixture.
        Files.createDirectories(Path.of(getProject().getBasePath()));
    }
    private static <T> T await(CompletableFuture<T> future) throws Exception {
        PlatformTestUtil.waitWithEventsDispatching("host response", future::isDone, 30);
        return future.get(1, TimeUnit.SECONDS);
    }

    public void testRealLsp4ijClientStartsRestartsAndNavigates() throws Exception {
        final String server = System.getProperty("tide.test.server");
        assertNotNull(server);
        final var settings = TideSettings.get();
        final String previous = settings.tidels();
        final var manager = LanguageServerManager.getInstance(getProject());
        settings.setTidels(server);
        try {
            manager.start(TidelsFactory.SERVER);
            final var item = await(manager.getLanguageServer(TidelsFactory.SERVER));
            final var remote = await(item.getInitializedServer());
            final String text = "component Body { float radius; }\nscene Main {}\nsystem Move(mut Body body) { body.radius = 1; }\n";
            final String uri = Files.createTempDirectory("tide-jb-host-").resolve("main.tide").toUri().toString();
            remote.getTextDocumentService().didOpen(new DidOpenTextDocumentParams(new TextDocumentItem(uri, "tide", 1, text)));
            final var hover = await(remote.getTextDocumentService().hover(new HoverParams(new TextDocumentIdentifier(uri), new Position(2, 35))));
            assertNotNull(hover);
            final var lenses = await(remote.getTextDocumentService().codeLens(new CodeLensParams(new TextDocumentIdentifier(uri))));
            assertTrue(lenses.stream().anyMatch(l -> l.getCommand() != null && l.getCommand().getCommand().equals("editor.action.showReferences")));
            final long pid = item.getServerWrapper().getCurrentProcessId();
            await(item.getServerWrapper().stop());
            assertFalse(ProcessHandle.of(pid).map(ProcessHandle::isAlive).orElse(false));
            manager.start(TidelsFactory.SERVER);
            final var replacement = await(manager.getLanguageServer(TidelsFactory.SERVER));
            await(replacement.getInitializedServer());
            assertTrue(pid != replacement.getServerWrapper().getCurrentProcessId().longValue());
            await(replacement.getServerWrapper().stop());
        } finally {
            manager.stop(TidelsFactory.SERVER);
            new TidelsFactory().setEnabled(true, getProject());
            settings.setTidels(previous);
        }
    }

    public void testRealRunConfigurationKillsDescendants() throws Exception {
        final String node = System.getProperty("tide.test.node");
        assertNotNull(node);
        final Path folder = Files.createTempDirectory("tide-jb-host-");
        // Bundled copy of the three-level VS Code fixture, so public clones can
        // exercise the same real process boundary without the engine checkout.
        try (final var fixture = getClass().getResourceAsStream("/host-process.cjs")) {
            assertNotNull(fixture);
            Files.copy(fixture, folder.resolve("run"));
        }
        final var type = ConfigurationTypeUtil.findConfigurationType(TideRunConfigurationType.class);
        final var factory = type.getConfigurationFactories()[0];
        final var configuration = (TideRunConfiguration) factory.createTemplateConfiguration(getProject());
        configuration.getOptions().setFolder(folder.toString());
        configuration.getOptions().setTide(node);
        configuration.checkConfiguration();
        // Invoke the adapter's real start method without replacing its handler.
        final var start = TideRunConfiguration.class.getDeclaredMethod("start");
        start.setAccessible(true);
        final var handler = (ProcessHandler) start.invoke(configuration);
        handler.startNotify();
        final var pids = new java.util.ArrayList<Long>();
        final var handles = new java.util.ArrayList<ProcessHandle>();
        try {
            PlatformTestUtil.waitWithEventsDispatching("real process tree", () -> {
                try (var files = Files.list(folder)) {
                    final var file = files.filter(p -> p.getFileName().toString().startsWith("pids-")).findFirst();
                    if (file.isEmpty()) return false;
                    final String text = Files.readString(file.get());
                    for (String pid : text.substring(1, text.length() - 1).split(",")) pids.add(Long.parseLong(pid));
                    return pids.size() == 3;
                } catch (Exception error) { return false; }
            }, 30);
            assertEquals(3, pids.size());
            pids.forEach(pid -> handles.add(ProcessHandle.of(pid).orElseThrow()));
            assertTrue(pids.stream().allMatch(pid -> ProcessHandle.of(pid).map(ProcessHandle::isAlive).orElse(false)));
            handler.destroyProcess();
            PlatformTestUtil.waitWithEventsDispatching("tree termination", () -> handler.isProcessTerminated() && pids.stream().noneMatch(pid -> ProcessHandle.of(pid).map(ProcessHandle::isAlive).orElse(false)), 30);
            assertTrue(handler.isProcessTerminated());
        } finally {
            handler.destroyProcess();
            // Retained ProcessHandles include process identity, unlike a fresh
            // lookup of a numeric PID that could have been reused after exit.
            handles.stream().filter(ProcessHandle::isAlive).forEach(ProcessHandle::destroyForcibly);
        }
    }

    public void testRealJcefLoadsAndReusesPage() throws Exception {
        assertTrue(Boolean.getBoolean("tide.test.jcef"));
        assertTrue("The opted-in JBR must support JCEF", JBCefApp.isSupported());
        final AtomicInteger first = new AtomicInteger(), second = new AtomicInteger();
        final HttpServer server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        server.createContext("/", exchange -> {
            final String path = exchange.getRequestURI().getPath();
            if (path.equals("/ready-one")) first.incrementAndGet();
            if (path.equals("/ready-two")) second.incrementAndGet();
            final byte[] body = ("<!doctype html><title>Tide</title><script>fetch('/ready-" + (path.equals("/two") ? "two" : "one") + "')</script>").getBytes(StandardCharsets.UTF_8);
            exchange.sendResponseHeaders(200, body.length);
            exchange.getResponseBody().write(body);
            exchange.close();
        });
        server.start();
        final var windows = ToolWindowManager.getInstance(getProject());
        final var window = windows.registerToolWindow(RegisterToolWindowTask.notClosable(TideGamePage.WINDOW));
        try {
            final var page = TideGamePage.of(getProject());
            new TideGamePage.Window().createToolWindowContent(getProject(), window);
            final var field = TideGamePage.class.getDeclaredField("browser");
            field.setAccessible(true);
            final JBCefBrowser browser = (JBCefBrowser) field.get(page);
            assertNotNull(browser);
            assertEquals(1, window.getContentManager().getContentCount());
            // Headless tool windows have no first paint to create the browser.
            browser.createImmediately();
            // Exercise show/activation/load twice on the adapter's own browser.
            final var pending = TideGamePage.class.getDeclaredField("pending");
            pending.setAccessible(true);
            final String base = "http://127.0.0.1:" + server.getAddress().getPort();
            final AtomicBoolean current = new AtomicBoolean(true);
            page.show(base + "/one", current::get);
            current.set(false); // The process stops before its UI callback runs
            UIUtil.dispatchAllInvocationEvents();
            assertEquals(0, first.get());
            assertNull(pending.get(page));
            page.show(base + "/one", () -> true);
            PlatformTestUtil.waitWithEventsDispatching("JCEF first page JS", () -> first.get() > 0, 30);
            assertTrue(first.get() > 0);
            page.show(base + "/two", () -> true);
            PlatformTestUtil.waitWithEventsDispatching("JCEF replacement page JS", () -> second.get() > 0, 30);
            assertTrue(second.get() > 0);
            assertNull(pending.get(page));
            assertSame(browser, field.get(page));
            Disposer.dispose(browser);
            field.set(page, null);
        } finally {
            window.remove();
            server.stop(0);
        }
    }
}
