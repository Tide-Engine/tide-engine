package io.github.blenminer.tide;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.intellij.ide.ui.LafManagerListener;
import com.intellij.openapi.actionSystem.ActionManager;
import com.intellij.openapi.actionSystem.AnAction;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.actionSystem.CustomShortcutSet;
import com.intellij.openapi.actionSystem.IdeActions;
import com.intellij.openapi.editor.Document;
import com.intellij.openapi.fileEditor.FileDocumentManager;
import com.intellij.openapi.fileEditor.FileEditor;
import com.intellij.openapi.fileEditor.FileEditorLocation;
import com.intellij.openapi.fileEditor.FileEditorPolicy;
import com.intellij.openapi.fileEditor.FileEditorProvider;
import com.intellij.openapi.fileEditor.FileEditorState;
import com.intellij.openapi.project.DumbAware;
import com.intellij.openapi.project.DumbAwareAction;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.util.Disposer;
import com.intellij.openapi.util.UserDataHolderBase;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.ui.JBColor;
import com.intellij.ui.jcef.JBCefApp;
import com.intellij.ui.jcef.JBCefBrowser;
import com.intellij.ui.jcef.JBCefBrowserBase;
import com.intellij.ui.jcef.JBCefJSQuery;
import org.cef.browser.CefBrowser;
import org.cef.browser.CefFrame;
import org.cef.handler.CefLoadHandlerAdapter;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.JComponent;
import java.awt.GraphicsEnvironment;
import java.beans.PropertyChangeListener;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;

// .tidegraph files in Tide's graph editor (tools/tide-graph, a web page, the same as VS Code's) in JCEF. The document
// stays the IDE's: the page's edits are commands on it (TideGraphBridge), so saving, undo, local history and the
// Text tab beside it are the IDE's. Without JCEF, the file opens as text, and TideGraphBanner says why.
public final class TideGraphEditor extends UserDataHolderBase implements FileEditor, DumbAware {
    static final String PAGE_URL = "https://tide-graph.local/index.html";

    private final Project project;
    private final VirtualFile file;
    private final JBCefBrowser browser;
    private final TideGraphBridge bridge;
    private final List<String> queued = new ArrayList<>(); // For the page, until it's loaded
    private boolean loaded;

    TideGraphEditor(@NotNull Project project, @NotNull VirtualFile file) {
        this.project = project;
        this.file = file;
        final Document document = FileDocumentManager.getInstance().getDocument(file);
        browser = JBCefBrowser.createBuilder().setOffScreenRendering(GraphicsEnvironment.isHeadless()).build();
        Disposer.register(this, browser);
        final JBCefJSQuery query = JBCefJSQuery.create((JBCefBrowserBase) browser);
        Disposer.register(this, query);
        bridge = new TideGraphBridge(project, file, document != null ? document : com.intellij.openapi.editor.EditorFactory.getInstance().createDocument(""), this::toPage, null);
        Disposer.register(this, bridge);
        query.addHandler(message -> {
            bridge.receive(message);
            return null;
        });
        browser.getJBCefClient().addLoadHandler(new CefLoadHandlerAdapter() {
            @Override
            public void onLoadEnd(CefBrowser cef, CefFrame frame, int status) {
                if (!frame.isMain()) return;
                // The page's messages come to the bridge; it sent its first ones (ready) to a queue of its own
                cef.executeJavaScript("window.tideHostPost = function (m) { " + query.inject("m") + " };"
                    + "(window.tideHostQueue || []).splice(0).forEach(window.tideHostPost);", PAGE_URL, 0);
                synchronized (queued) {
                    loaded = true;
                    for (final String m : queued) receiveOnPage(m);
                    queued.clear();
                }
            }
        }, browser.getCefBrowser());
        project.getMessageBus().connect(this).subscribe(LafManagerListener.TOPIC, (LafManagerListener) source -> toPage(theme().toString()));
        toPage(theme().toString());
        browser.loadHTML(page(), PAGE_URL);
        registerCommands();
    }

    // The IDE's own shortcuts for undo, redo, copy, cut, paste, delete, duplicate and select all go to the page
    // while it has the focus: its history and its selection are the graph's
    private void registerCommands() {
        final String[][] commands = {
            {IdeActions.ACTION_UNDO, "undo"}, {IdeActions.ACTION_REDO, "redo"}, {IdeActions.ACTION_COPY, "copy"},
            {IdeActions.ACTION_CUT, "cut"}, {IdeActions.ACTION_PASTE, "paste"}, {IdeActions.ACTION_DELETE, "delete"},
            {IdeActions.ACTION_EDITOR_DUPLICATE, "duplicate"}, {IdeActions.ACTION_SELECT_ALL, "selectAll"},
        };
        final JComponent component = browser.getComponent();
        for (final String[] c : commands) {
            final AnAction ide = ActionManager.getInstance().getAction(c[0]);
            if (ide == null) continue;
            final String name = c[1];
            final AnAction action = new DumbAwareAction() {
                @Override
                public void actionPerformed(@NotNull AnActionEvent e) {
                    bridge.command(name);
                }
            };
            action.registerCustomShortcutSet(new CustomShortcutSet(ide.getShortcutSet().getShortcuts()), component, this);
        }
    }

    TideGraphBridge bridge() {
        return bridge;
    }

    JBCefBrowser browser() {
        return browser;
    }

    // A message for the page: TideGraph.receive(...) in it, once it's there
    private void toPage(@NotNull String json) {
        synchronized (queued) {
            if (!loaded) {
                queued.add(json);
                return;
            }
        }
        receiveOnPage(json);
    }

    private void receiveOnPage(String json) {
        browser.getCefBrowser().executeJavaScript("TideGraph.receive(" + json + ");", PAGE_URL, 0);
    }

    // The editor's page: its files (files.json) from the plugin, inline, and its side of the bridge
    static @NotNull String page() {
        final JsonObject files = JsonParser.parseString(resource("files.json")).getAsJsonObject();
        final StringBuilder html = new StringBuilder("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
            + "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
        for (final var style : files.getAsJsonArray("styles")) html.append("<style>").append(withFonts(resource(style.getAsString()), files)).append("</style>");
        html.append("</head><body>");
        for (final var script : files.getAsJsonArray("scripts")) {
            html.append("<script>").append(resource(script.getAsString()).replace("</script", "<\\/script")).append("</script>");
        }
        html.append("<script>window.tideHostQueue = [];"
            + "TideGraph.start({ post: function (m) { var s = JSON.stringify(m);"
            + " if (window.tideHostPost) window.tideHostPost(s); else window.tideHostQueue.push(s); } });</script>");
        return html.append("</body></html>").toString();
    }

    // A style inline has no folder for its url()s: the fonts it names (files.json's fonts) are put in it as data
    static @NotNull String withFonts(@NotNull String css, @NotNull JsonObject files) {
        if (!files.has("fonts")) return css;
        for (final var font : files.getAsJsonArray("fonts")) {
            final String name = font.getAsString();
            if (!name.endsWith(".ttf") || !css.contains("url(\"" + name + "\")")) continue;
            try (InputStream in = TideGraphEditor.class.getResourceAsStream("/tide-graph/" + name)) {
                if (in == null) throw new IllegalStateException("The plugin is missing the graph editor's " + name);
                css = css.replace("url(\"" + name + "\")", "url(\"data:font/ttf;base64," + Base64.getEncoder().encodeToString(in.readAllBytes()) + "\")");
            } catch (IOException e) {
                throw new IllegalStateException(e);
            }
        }
        return css;
    }

    private static @NotNull String resource(@NotNull String name) {
        try (InputStream in = TideGraphEditor.class.getResourceAsStream("/tide-graph/" + name)) {
            if (in == null) throw new IllegalStateException("The plugin is missing the graph editor's " + name);
            return new String(in.readAllBytes(), StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new IllegalStateException(e);
        }
    }

    // Whether the IDE is dark or light: the page's own colours and font are the graph schema's (its look), the same
    // in every host
    static @NotNull JsonObject theme() {
        final JsonObject m = new JsonObject();
        m.addProperty("type", "theme");
        m.addProperty("dark", !JBColor.isBright());
        return m;
    }

    @Override
    public @NotNull JComponent getComponent() {
        return browser.getComponent();
    }

    @Override
    public @Nullable JComponent getPreferredFocusedComponent() {
        return browser.getComponent();
    }

    @Override
    public @NotNull String getName() {
        return "Graph";
    }

    @Override
    public @NotNull VirtualFile getFile() {
        return file;
    }

    @Override
    public void setState(@NotNull FileEditorState state) {}

    @Override
    public boolean isModified() {
        return FileDocumentManager.getInstance().isFileModified(file);
    }

    @Override
    public boolean isValid() {
        return file.isValid();
    }

    @Override
    public void addPropertyChangeListener(@NotNull PropertyChangeListener listener) {}

    @Override
    public void removePropertyChangeListener(@NotNull PropertyChangeListener listener) {}

    @Override
    public @Nullable FileEditorLocation getCurrentLocation() {
        return null;
    }

    @Override
    public void dispose() {}

    public static final class Provider implements FileEditorProvider, DumbAware {
        @Override
        public boolean accept(@NotNull Project project, @NotNull VirtualFile file) {
            return TideGraphFileType.is(file) && JBCefApp.isSupported();
        }

        @Override
        public @NotNull FileEditor createEditor(@NotNull Project project, @NotNull VirtualFile file) {
            return new TideGraphEditor(project, file);
        }

        @Override
        public @NotNull String getEditorTypeId() {
            return "tide-graph";
        }

        @Override
        public @NotNull FileEditorPolicy getPolicy() {
            return FileEditorPolicy.PLACE_BEFORE_DEFAULT_EDITOR; // The JSON stays a tab beside it
        }
    }
}
