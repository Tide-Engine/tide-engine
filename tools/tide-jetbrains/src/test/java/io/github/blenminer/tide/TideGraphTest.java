package io.github.blenminer.tide;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.intellij.openapi.editor.Document;
import com.intellij.openapi.fileEditor.FileDocumentManager;
import com.intellij.openapi.fileEditor.ex.FileEditorProviderManager;
import com.intellij.openapi.fileTypes.FileTypeManager;
import com.intellij.openapi.ide.CopyPasteManager;
import com.intellij.openapi.util.Disposer;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.testFramework.PlatformTestUtil;
import com.intellij.testFramework.fixtures.BasePlatformTestCase;
import com.intellij.ui.jcef.JBCefApp;
import org.eclipse.lsp4j.Diagnostic;
import org.eclipse.lsp4j.DiagnosticSeverity;
import org.eclipse.lsp4j.Position;
import org.eclipse.lsp4j.PublishDiagnosticsParams;
import org.eclipse.lsp4j.Range;

import java.awt.datatransfer.DataFlavor;
import java.awt.datatransfer.StringSelection;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;

// .tidegraph files: their file type, the graph editor's page as the plugin carries it, the editor where JCEF is and
// the notice where it isn't, and the bridge between the page and the IDE (its messages, as the page sends them,
// with tidels replaced).
public final class TideGraphTest extends BasePlatformTestCase {
    private static final String GRAPH = """
        {
          "tidegraph": 1,
          "nextId": 4,
          "decls": [
            {
              "kind": "system",
              "id": 1,
              "name": "Grow",
              "body": {
                "entry": null,
                "nodes": [
                  {"id": 3, "k": "int", "x": 0, "y": 0, "text": "1"}
                ]
              }
            }
          ]
        }
        """;

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        // No real tidels for the files these tests open: one this machine has installed would start in the test's
        // folder and keep it from being deleted. The bridge's tidels is a stand-in.
        new TidelsFactory().setEnabled(false, getProject());
    }

    public void testGraphFilesAreTheirOwnTypeAndOpenInTheGraphEditorOrAsTextWithANotice() {
        final VirtualFile file = myFixture.configureByText("grow.tidegraph", GRAPH).getVirtualFile();
        assertSame(TideGraphFileType.INSTANCE, FileTypeManager.getInstance().getFileTypeByFile(file));
        final boolean graph = FileEditorProviderManager.getInstance().getProviderList(getProject(), file).stream()
            .anyMatch(p -> p instanceof TideGraphEditor.Provider);
        assertEquals("The graph editor opens it where there's JCEF", JBCefApp.isSupported(), graph);
        final var notice = new TideGraphBanner().collectNotificationData(getProject(), file);
        assertEquals("Without JCEF, a notice says why it's text", !JBCefApp.isSupported(), notice != null);
        assertNull(new TideGraphBanner().collectNotificationData(getProject(), myFixture.configureByText("a.tide", "").getVirtualFile()));
    }

    public void testThePageHasEveryFileOfTheEditorAndTheSchema() {
        final String page = TideGraphEditor.page();
        final JsonObject files = JsonParser.parseString(new String(readResource("files.json"))).getAsJsonObject();
        for (final var script : files.getAsJsonArray("scripts")) {
            assertTrue(script.getAsString(), page.contains(new String(readResource(script.getAsString())).lines().findFirst().orElse("")));
        }
        assertTrue(page.contains("TideGraph.start("));
        final JsonElement schema = TideGraphBridge.schemaCopy();
        assertNotNull(schema);
        assertTrue(schema.getAsJsonObject().getAsJsonArray("nodes").size() > 40);
        final JsonObject theme = TideGraphEditor.theme();
        assertEquals("theme", theme.get("type").getAsString());
        assertTrue(theme.has("dark"));
        assertFalse("the page's colours are the schema's, not the IDE's", theme.has("colors"));
        assertTrue("the fonts are in the page", page.contains("data:font/ttf;base64,"));
    }

    public void testTheBridgeLoadsEditsAsksTidelsAndPassesDiagnostics() throws Exception {
        final VirtualFile file = myFixture.configureByText("grow.tidegraph", GRAPH).getVirtualFile();
        final Document document = FileDocumentManager.getInstance().getDocument(file);
        final List<JsonObject> sent = new ArrayList<>();
        final List<String> asked = new ArrayList<>();
        final String uri = "file:///game/grow.tidegraph";
        final TideGraphBridge bridge = new TideGraphBridge(getProject(), file, document,
            json -> { synchronized (sent) { sent.add(JsonParser.parseString(json).getAsJsonObject()); } },
            () -> CompletableFuture.completedFuture(new TideGraphBridge.Connection(uri, (method, params) -> {
                asked.add(method + " " + document.getText().length());
                final JsonObject text = new JsonObject();
                text.addProperty("text", "system Grow() { }");
                return CompletableFuture.completedFuture(text);
            })));
        Disposer.register(getTestRootDisposable(), bridge);

        bridge.receive("{\"type\": \"ready\"}");
        final JsonObject load = waitFor(sent, "load");
        assertEquals(GRAPH, load.get("text").getAsString());
        assertEquals(uri, load.get("uri").getAsString());
        assertEquals("grow.tidegraph", load.get("name").getAsString());
        assertTrue(load.getAsJsonObject("schema").has("nodes"));

        // An edit is a command on the document; the request after it is asked of what the edit made
        final String edited = GRAPH.replace("\"Grow\"", "\"Grown\"");
        bridge.receive(message("edit", "text", edited));
        bridge.receive("{\"type\": \"request\", \"id\": 5, \"method\": \"tide/graphText\", \"params\": {\"textDocument\": {\"uri\": \"" + uri + "\"}}}");
        final JsonObject answer = waitFor(sent, "response");
        assertEquals(5, answer.get("id").getAsInt());
        assertEquals("system Grow() { }", answer.getAsJsonObject("result").get("text").getAsString());
        assertEquals(edited, document.getText());
        assertEquals(List.of("tide/graphText " + edited.length()), asked);
        assertFalse("The page's own edit isn't sent back to it", sent.stream().anyMatch(m -> m.get("type").getAsString().equals("text")));

        // A change from elsewhere is
        com.intellij.openapi.command.WriteCommandAction.runWriteCommandAction(getProject(), () -> document.setText(GRAPH));
        assertEquals(GRAPH, waitFor(sent, "text").get("text").getAsString());

        // Only the graph's requests go to tidels; the clipboard is the IDE's
        bridge.receive("{\"type\": \"request\", \"id\": 6, \"method\": \"workspace/symbol\"}");
        assertTrue(waitFor(sent, "response", 6).get("error").getAsString().contains("no request"));
        bridge.receive(message("clipboardWrite", "text", "{\"tidegraphClip\": 1, \"nodes\": []}"));
        PlatformTestUtil.dispatchAllEventsInIdeEventQueue();
        assertEquals("{\"tidegraphClip\": 1, \"nodes\": []}", CopyPasteManager.getInstance().getContents(DataFlavor.stringFlavor));
        CopyPasteManager.getInstance().setContents(new StringSelection("copied"));
        bridge.receive("{\"type\": \"request\", \"id\": 7, \"method\": \"host/clipboardRead\"}");
        assertEquals("copied", waitFor(sent, "response", 7).get("result").getAsString());

        // tidels's diagnostics for the graph reach the page with their node
        final Diagnostic d = new Diagnostic(new Range(new Position(12, 4), new Position(12, 9)), "no such member");
        d.setSeverity(DiagnosticSeverity.Error);
        final JsonObject data = new JsonObject();
        data.addProperty("node", 3);
        d.setData(data);
        TideGraphDiagnostics.of(getProject()).publish(new PublishDiagnosticsParams(uri, List.of(d)));
        final JsonObject published = waitFor(sent, "diagnostics", -1, m -> m.getAsJsonArray("diagnostics").size() == 1);
        final JsonObject first = published.getAsJsonArray("diagnostics").get(0).getAsJsonObject();
        assertEquals(1, first.get("severity").getAsInt());
        assertEquals(3, first.getAsJsonObject("data").get("node").getAsInt());
        assertEquals(12, first.getAsJsonObject("range").getAsJsonObject("start").get("line").getAsInt());

        // The IDE's undo and paste are the page's commands
        bridge.command("undo");
        assertEquals("undo", sent.get(sent.size() - 1).get("name").getAsString());
    }

    private static String message(String type, String key, String value) {
        final JsonObject m = new JsonObject();
        m.addProperty("type", type);
        m.addProperty(key, value);
        return m.toString();
    }

    private static JsonObject waitFor(List<JsonObject> sent, String type) throws Exception {
        return waitFor(sent, type, -1, m -> true);
    }

    private static JsonObject waitFor(List<JsonObject> sent, String type, int id) throws Exception {
        return waitFor(sent, type, id, m -> true);
    }

    // The last message of a type (and id) the bridge sent, once it's there; the EDT runs meanwhile
    private static JsonObject waitFor(List<JsonObject> sent, String type, int id, java.util.function.Predicate<JsonObject> also) throws Exception {
        final long end = System.currentTimeMillis() + 10_000;
        while (System.currentTimeMillis() < end) {
            PlatformTestUtil.dispatchAllEventsInIdeEventQueue();
            synchronized (sent) {
                for (int i = sent.size() - 1; i >= 0; i--) {
                    final JsonObject m = sent.get(i);
                    if (!m.get("type").getAsString().equals(type)) continue;
                    if (id >= 0 && (!m.has("id") || m.get("id").getAsInt() != id)) continue;
                    if (also.test(m)) return m;
                }
            }
            Thread.sleep(10);
        }
        fail("The bridge sent no " + type + ": " + sent);
        return null;
    }

    private static byte[] readResource(String name) {
        try (var in = TideGraphTest.class.getResourceAsStream("/tide-graph/" + name)) {
            assertNotNull("The plugin carries " + name, in);
            return in.readAllBytes();
        } catch (java.io.IOException e) {
            throw new AssertionError(e);
        }
    }
}
