package io.github.blenminer.tide;

import com.google.gson.Gson;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonNull;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.intellij.notification.NotificationGroupManager;
import com.intellij.notification.NotificationType;
import com.intellij.openapi.Disposable;
import com.intellij.openapi.application.ApplicationManager;
import com.intellij.openapi.command.WriteCommandAction;
import com.intellij.openapi.editor.Document;
import com.intellij.openapi.editor.event.DocumentEvent;
import com.intellij.openapi.editor.event.DocumentListener;
import com.intellij.openapi.fileEditor.FileDocumentManager;
import com.intellij.openapi.ide.CopyPasteManager;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.vfs.VirtualFile;
import com.redhat.devtools.lsp4ij.LanguageServerManager;
import com.redhat.devtools.lsp4ij.client.features.FileUriSupport;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.awt.datatransfer.DataFlavor;
import java.awt.datatransfer.StringSelection;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CompletableFuture;
import java.util.function.Consumer;
import java.util.function.Supplier;

// The host's side of Tide's graph editor page (tools/tide-graph, app.js has its messages), for one .tidegraph: its
// edits become the IDE document's text (a command, so the IDE's undo and local history have them), its requests go
// to tidels through LSP4IJ, and the document's changes and tidels's diagnostics go to it. Without JCEF in it, so that
// tests can drive it.
final class TideGraphBridge implements Disposable {
    // tidels for this file: its URI as LSP4IJ gives it to tidels, and a way to ask it
    record Connection(@NotNull String uri, @NotNull Server server) {}

    interface Server {
        // The answer to a tide/graph* request, or null for a request it hasn't
        @Nullable CompletableFuture<JsonElement> request(@NotNull String method, @Nullable JsonElement params);
    }

    private static final Gson GSON = new Gson();

    private final Project project;
    private final VirtualFile file;
    private final Document document;
    private final Consumer<String> toPage;
    private final Supplier<CompletableFuture<Connection>> connect;
    private @Nullable String fromPage; // The text the page last sent, which needn't go back to it
    private @Nullable String uri;
    private volatile boolean disposed;
    volatile boolean loaded; // The page has been given the document

    TideGraphBridge(@NotNull Project project, @NotNull VirtualFile file, @NotNull Document document, @NotNull Consumer<String> toPage,
                    @Nullable Supplier<CompletableFuture<Connection>> connect) {
        this.project = project;
        this.file = file;
        this.document = document;
        this.toPage = toPage;
        this.connect = connect != null ? connect : this::lsp4ij;
        document.addDocumentListener(new DocumentListener() {
            @Override
            public void documentChanged(@NotNull DocumentEvent event) {
                final String text = document.getText();
                if (text.equals(fromPage)) return;
                fromPage = null;
                final JsonObject m = message("text");
                m.addProperty("text", text);
                post(m);
            }
        }, this);
        TideGraphDiagnostics.of(project).listen((key, list) -> {
            if (uri != null && key.equals(TideGraphDiagnostics.key(uri))) sendDiagnostics(list);
        }, this);
    }

    // tidels through LSP4IJ's public API: the server by its id, started if it isn't. LSP4IJ connects the file
    // (didOpen) as the IDE opens it, the Text tab beside this one being a text editor, so tidels has the document as
    // the IDE has it, unsaved edits included
    private CompletableFuture<Connection> lsp4ij() {
        return LanguageServerManager.getInstance(project).getLanguageServer(TidelsFactory.SERVER).thenApply(item -> {
            if (item == null || !(item.getServer() instanceof TidelsServer tidels)) return null;
            final String u = FileUriSupport.toString(file, item.getClientFeatures());
            return new Connection(u, (method, params) -> TidelsServer.request(tidels, method, params));
        });
    }

    // A message from the page, as JSON
    void receive(@NotNull String json) {
        if (disposed) return;
        final JsonObject m;
        try {
            m = JsonParser.parseString(json).getAsJsonObject();
        } catch (RuntimeException e) {
            return;
        }
        final String type = string(m, "type");
        if (type == null) return;
        switch (type) {
            case "ready" -> ready();
            case "edit" -> edit(string(m, "text"));
            case "request" -> request(m);
            case "clipboardWrite" -> {
                final String text = string(m, "text");
                if (text != null) CopyPasteManager.getInstance().setContents(new StringSelection(text));
            }
            case "save" -> ApplicationManager.getApplication().invokeLater(() -> {
                if (!disposed) FileDocumentManager.getInstance().saveDocument(document);
            });
            case "showTide" -> TideGraphActions.showTide(project, file, string(m, "text"));
            case "error" -> NotificationGroupManager.getInstance().getNotificationGroup("Tide")
                .createNotification("Tide graph: " + string(m, "message"), NotificationType.ERROR).notify(project);
            default -> {}
        }
    }

    private void ready() {
        connect.get().exceptionally(e -> null).thenAccept(connection -> {
            uri = connection != null ? connection.uri() : FileUriSupport.toString(file, FileUriSupport.DEFAULT);
            final String text = ApplicationManager.getApplication().runReadAction(
                (com.intellij.openapi.util.Computable<String>) document::getText);
            final JsonObject load = message("load");
            load.addProperty("text", text);
            load.addProperty("uri", uri);
            load.addProperty("name", file.getName());
            final JsonElement schema = schemaCopy();
            if (schema != null) load.add("schema", schema);
            post(load);
            loaded = true;
            sendDiagnostics(TideGraphDiagnostics.of(project).of(uri));
            if (connection == null) {
                final JsonObject server = message("server");
                server.addProperty("available", false);
                final String problem = TidelsFactory.find(project).problem();
                server.addProperty("message", problem != null ? problem : "tidels isn't running");
                post(server);
            }
        });
    }

    private void edit(@Nullable String text) {
        if (text == null) return;
        ApplicationManager.getApplication().invokeLater(() -> {
            if (disposed || text.equals(document.getText())) return;
            fromPage = text;
            WriteCommandAction.runWriteCommandAction(project, "Edit Tide Graph", "tide.graph", () -> document.setText(text));
        });
    }

    private void request(@NotNull JsonObject m) {
        final JsonElement id = m.get("id");
        final String method = string(m, "method");
        final JsonElement params = m.has("params") ? m.get("params") : JsonNull.INSTANCE;
        if ("host/clipboardRead".equals(method)) {
            String text = null;
            try {
                text = CopyPasteManager.getInstance().getContents(DataFlavor.stringFlavor);
            } catch (RuntimeException ignored) {
            }
            answer(id, text != null ? GSON.toJsonTree(text) : JsonNull.INSTANCE, null);
            return;
        }
        if (method == null || !method.startsWith("tide/graph")) {
            answer(id, null, "no request " + method);
            return;
        }
        // After the edits before it: they're in the document once the EDT has run them
        ApplicationManager.getApplication().invokeLater(() -> connect.get().whenComplete((connection, error) -> {
            if (connection == null) {
                answer(id, null, error != null ? error.getMessage() : "tidels isn't running");
                return;
            }
            final CompletableFuture<JsonElement> answer = connection.server().request(method, params);
            if (answer == null) {
                answer(id, null, "no request " + method);
                return;
            }
            answer.whenComplete((result, failure) -> {
                if (failure != null) answer(id, null, failure.getMessage() != null ? failure.getMessage() : failure.toString());
                else answer(id, result != null ? result : JsonNull.INSTANCE, null);
            });
        }));
    }

    private void answer(@Nullable JsonElement id, @Nullable JsonElement result, @Nullable String error) {
        final JsonObject r = message("response");
        r.add("id", id);
        if (error != null) r.addProperty("error", error);
        else r.add("result", result);
        post(r);
    }

    private void sendDiagnostics(@NotNull JsonArray list) {
        final JsonObject m = message("diagnostics");
        m.add("diagnostics", list);
        post(m);
    }

    // A command for the page: undo, paste and the like, from the IDE's actions
    void command(@NotNull String name) {
        final JsonObject m = message("command");
        m.addProperty("name", name);
        post(m);
    }

    void post(@NotNull JsonObject message) {
        if (!disposed) toPage.accept(GSON.toJson(message));
    }

    private static JsonObject message(String type) {
        final JsonObject m = new JsonObject();
        m.addProperty("type", type);
        return m;
    }

    private static @Nullable String string(JsonObject o, String key) {
        final JsonElement e = o.get(key);
        return e != null && e.isJsonPrimitive() ? e.getAsString() : null;
    }

    // The schema the page starts with, until tidels gives its own: the compiler's, packaged with the page
    static @Nullable JsonElement schemaCopy() {
        try (InputStream in = TideGraphBridge.class.getResourceAsStream("/tide-graph/graph_schema.json")) {
            return in != null ? JsonParser.parseString(new String(in.readAllBytes(), StandardCharsets.UTF_8)) : null;
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    @Override
    public void dispose() {
        disposed = true;
    }
}
