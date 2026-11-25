package io.github.blenminer.tide;

import com.google.gson.JsonElement;
import org.eclipse.lsp4j.jsonrpc.services.JsonRequest;
import org.eclipse.lsp4j.services.LanguageServer;

import java.util.concurrent.CompletableFuture;

// tidels's own requests, which the graph editor asks (notes/visual-scripting.md): the graph schema and layout, and of
// an open graph its Tide, types, completion, hover, signature help, a node's members, its calls' parameters, the
// game's own names and the operators a type takes. LSP4IJ makes its proxy of
// tidels with this interface (TidelsFactory.getServerInterface).
public interface TidelsServer extends LanguageServer {
    @JsonRequest("tide/graphSchema")
    CompletableFuture<JsonElement> graphSchema(JsonElement params);

    @JsonRequest("tide/graphLayout")
    CompletableFuture<JsonElement> graphLayout(JsonElement params);

    @JsonRequest("tide/graphText")
    CompletableFuture<JsonElement> graphText(JsonElement params);

    @JsonRequest("tide/graphTypes")
    CompletableFuture<JsonElement> graphTypes(JsonElement params);

    @JsonRequest("tide/graphCompletion")
    CompletableFuture<JsonElement> graphCompletion(JsonElement params);

    @JsonRequest("tide/graphHover")
    CompletableFuture<JsonElement> graphHover(JsonElement params);

    @JsonRequest("tide/graphSignatureHelp")
    CompletableFuture<JsonElement> graphSignatureHelp(JsonElement params);

    @JsonRequest("tide/graphMembers")
    CompletableFuture<JsonElement> graphMembers(JsonElement params);

    @JsonRequest("tide/graphCalls")
    CompletableFuture<JsonElement> graphCalls(JsonElement params);

    @JsonRequest("tide/graphNames")
    CompletableFuture<JsonElement> graphNames(JsonElement params);

    @JsonRequest("tide/graphOperators")
    CompletableFuture<JsonElement> graphOperators(JsonElement params);

    // One of them by its name, or null for a name that isn't one
    static CompletableFuture<JsonElement> request(TidelsServer server, String method, JsonElement params) {
        return switch (method) {
            case "tide/graphSchema" -> server.graphSchema(params);
            case "tide/graphLayout" -> server.graphLayout(params);
            case "tide/graphText" -> server.graphText(params);
            case "tide/graphTypes" -> server.graphTypes(params);
            case "tide/graphCompletion" -> server.graphCompletion(params);
            case "tide/graphHover" -> server.graphHover(params);
            case "tide/graphSignatureHelp" -> server.graphSignatureHelp(params);
            case "tide/graphMembers" -> server.graphMembers(params);
            case "tide/graphCalls" -> server.graphCalls(params);
            case "tide/graphNames" -> server.graphNames(params);
            case "tide/graphOperators" -> server.graphOperators(params);
            default -> null;
        };
    }
}
