package io.github.blenminer.tide;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.intellij.openapi.Disposable;
import com.intellij.openapi.components.Service;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.util.Disposer;
import org.eclipse.lsp4j.Diagnostic;
import org.eclipse.lsp4j.PublishDiagnosticsParams;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.net.URI;
import java.nio.file.Path;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.function.BiConsumer;

// The diagnostics tidels last published for each graph, as the page takes them (LSP's JSON, `data.node` kept), and
// the graph editors that want them.
@Service(Service.Level.PROJECT)
public final class TideGraphDiagnostics {
    private final Map<String, JsonArray> last = new ConcurrentHashMap<>();
    private final List<BiConsumer<String, JsonArray>> listeners = new CopyOnWriteArrayList<>();

    static @NotNull TideGraphDiagnostics of(@NotNull Project project) {
        return project.getService(TideGraphDiagnostics.class);
    }

    void publish(@NotNull PublishDiagnosticsParams params) {
        final String key = key(params.getUri());
        if (key == null || !key.endsWith(".tidegraph")) return;
        final JsonArray list = new JsonArray();
        for (final Diagnostic d : params.getDiagnostics()) list.add(json(d));
        last.put(key, list);
        for (final var l : listeners) l.accept(key, list);
    }

    @NotNull JsonArray of(@NotNull String uri) {
        final String key = key(uri);
        final JsonArray list = key != null ? last.get(key) : null;
        return list != null ? list : new JsonArray();
    }

    // Calls `listener` with each graph's new diagnostics, until `parent` goes
    void listen(@NotNull BiConsumer<String, JsonArray> listener, @NotNull Disposable parent) {
        listeners.add(listener);
        Disposer.register(parent, () -> listeners.remove(listener));
    }

    // A document's URI as a key: the same file however the URI spells it (a drive's case, encoding)
    static @Nullable String key(@Nullable String uri) {
        if (uri == null) return null;
        try {
            final String path = Path.of(URI.create(uri)).toAbsolutePath().normalize().toString();
            return com.intellij.openapi.util.SystemInfo.isFileSystemCaseSensitive ? path : path.toLowerCase(Locale.ROOT);
        } catch (RuntimeException e) {
            return uri;
        }
    }

    // As LSP writes it, by lsp4j's own JSON (its message is a String or an Either, by lsp4j's version): what the page
    // reads of it
    static @NotNull JsonObject json(@NotNull Diagnostic d) {
        final JsonObject all = LSP_JSON.toJsonTree(d).getAsJsonObject();
        final JsonObject o = new JsonObject();
        o.addProperty("severity", all.has("severity") ? all.get("severity").getAsInt() : 1);
        final JsonElement message = all.get("message");
        o.addProperty("message", message != null && message.isJsonPrimitive() ? message.getAsString()
            : message != null && message.isJsonObject() && message.getAsJsonObject().has("value") ? message.getAsJsonObject().get("value").getAsString() : "");
        if (all.has("range")) o.add("range", all.get("range"));
        if (all.has("data")) o.add("data", all.get("data"));
        return o;
    }

    private static final com.google.gson.Gson LSP_JSON = new org.eclipse.lsp4j.jsonrpc.json.MessageJsonHandler(java.util.Map.of()).getGson();
}
