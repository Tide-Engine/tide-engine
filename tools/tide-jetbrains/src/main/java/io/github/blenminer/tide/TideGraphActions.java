package io.github.blenminer.tide;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.intellij.execution.configurations.GeneralCommandLine;
import com.intellij.execution.process.CapturingProcessHandler;
import com.intellij.execution.process.ProcessOutput;
import com.intellij.notification.NotificationGroupManager;
import com.intellij.notification.NotificationType;
import com.intellij.openapi.actionSystem.ActionUpdateThread;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.actionSystem.CommonDataKeys;
import com.intellij.openapi.application.ApplicationManager;
import com.intellij.openapi.fileEditor.FileDocumentManager;
import com.intellij.openapi.fileEditor.FileEditorManager;
import com.intellij.openapi.progress.ProgressIndicator;
import com.intellij.openapi.progress.Task;
import com.intellij.openapi.project.DumbAwareAction;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.ui.Messages;
import com.intellij.openapi.vfs.LocalFileSystem;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.testFramework.LightVirtualFile;
import com.redhat.devtools.lsp4ij.LanguageServerManager;
import com.redhat.devtools.lsp4ij.client.features.FileUriSupport;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

// Convert to Tide Graph (`tide graph` on a .tide file) and Show as Tide (the Tide a graph stands for).
public final class TideGraphActions {
    private TideGraphActions() {}

    // The graph's Tide in a read-only tab: tidels's, of the document as it is, else `tide graph --text` of the saved
    // file. `given` is the page's, when it has it.
    static void showTide(@NotNull Project project, @NotNull VirtualFile graph, @Nullable String given) {
        if (given != null) {
            open(project, graph, given);
            return;
        }
        fromTidels(project, graph).exceptionally(e -> null).thenAccept(text -> {
            if (text != null) {
                ApplicationManager.getApplication().invokeLater(() -> open(project, graph, text));
                return;
            }
            new Task.Backgroundable(project, "Writing the graph as Tide", true) {
                @Override
                public void run(@NotNull ProgressIndicator indicator) {
                    final String saved = tide(project, graph, "graph", "--text", graph.getPath());
                    if (saved != null) ApplicationManager.getApplication().invokeLater(() -> open(project, graph, saved));
                }
            }.queue();
        });
    }

    private static CompletableFuture<String> fromTidels(@NotNull Project project, @NotNull VirtualFile graph) {
        return LanguageServerManager.getInstance(project).getLanguageServer(TidelsFactory.SERVER).thenCompose(item -> {
            if (item == null || !(item.getServer() instanceof TidelsServer tidels)) return CompletableFuture.completedFuture(null);
            final JsonObject params = new JsonObject();
            final JsonObject document = new JsonObject();
            document.addProperty("uri", FileUriSupport.toString(graph, item.getClientFeatures()));
            params.add("textDocument", document);
            return tidels.graphText(params).thenApply(r -> {
                final JsonElement text = r != null && r.isJsonObject() ? r.getAsJsonObject().get("text") : null;
                return text != null && text.isJsonPrimitive() ? text.getAsString() : null;
            });
        });
    }

    private static void open(@NotNull Project project, @NotNull VirtualFile graph, @NotNull String text) {
        if (project.isDisposed()) return;
        final LightVirtualFile tide = new LightVirtualFile(graph.getNameWithoutExtension() + ".tide", text);
        tide.setWritable(false);
        FileEditorManager.getInstance(project).openFile(tide, true);
    }

    // Runs tide with `args` in the file's folder: its output, or null after saying what went wrong
    static @Nullable String tide(@NotNull Project project, @NotNull VirtualFile file, @NotNull String... args) {
        final Path folder = Path.of(file.getParent().getPath());
        final var tools = TideProjectTools.find(folder, "tide");
        final Path tide = tools.file();
        if (tide == null) {
            fail(project, tools.error() != null ? tools.error() : "Couldn't find tide. Install it (" + Tide.INSTALL + ").");
            return null;
        }
        final List<String> command = new ArrayList<>();
        command.add(tide.toString());
        command.addAll(List.of(args));
        try {
            final ProcessOutput out = new CapturingProcessHandler(new GeneralCommandLine(command)
                .withWorkDirectory(folder.toFile()).withCharset(StandardCharsets.UTF_8)).runProcess((int) TimeUnit.MINUTES.toMillis(2));
            if (out.getExitCode() != 0 || out.isTimeout()) {
                fail(project, "tide " + String.join(" ", args) + ": " + (out.isTimeout() ? "took over 2 minutes" : out.getStderr().trim()));
                return null;
            }
            return out.getStdout().replace("\r\n", "\n");
        } catch (com.intellij.execution.ExecutionException e) {
            fail(project, e.getMessage());
            return null;
        }
    }

    private static void fail(@NotNull Project project, @NotNull String message) {
        NotificationGroupManager.getInstance().getNotificationGroup("Tide").createNotification("Tide: " + message, NotificationType.ERROR).notify(project);
    }

    // Convert to Tide Graph: a .tidegraph of a .tide file beside it, opened in the graph editor. The .tide file goes
    // (--replace) or stays, as asked: with both, the game declares everything twice until one goes.
    public static final class Convert extends DumbAwareAction {
        @Override
        public void update(@NotNull AnActionEvent e) {
            final VirtualFile file = e.getData(CommonDataKeys.VIRTUAL_FILE);
            e.getPresentation().setEnabledAndVisible(e.getProject() != null && file != null && "tide".equals(file.getExtension()));
        }

        @Override
        public @NotNull ActionUpdateThread getActionUpdateThread() {
            return ActionUpdateThread.BGT;
        }

        @Override
        public void actionPerformed(@NotNull AnActionEvent e) {
            final Project project = e.getProject();
            final VirtualFile file = e.getData(CommonDataKeys.VIRTUAL_FILE);
            if (project == null || file == null) return;
            final int choice = Messages.showYesNoCancelDialog(project,
                "Convert " + file.getName() + " to a graph, " + file.getName() + "graph?\n\nReplace removes the .tide file. Keep Both leaves it, and the game then declares everything twice until one goes.",
                "Convert to Tide Graph", "Replace", "Keep Both", "Cancel", Messages.getQuestionIcon());
            if (choice == Messages.CANCEL) return;
            convert(project, file, choice == Messages.YES);
        }
    }

    static void convert(@NotNull Project project, @NotNull VirtualFile file, boolean replace) {
        final var document = FileDocumentManager.getInstance().getCachedDocument(file);
        if (document != null) FileDocumentManager.getInstance().saveDocument(document);
        new Task.Backgroundable(project, "Converting " + file.getName() + " to a graph", true) {
            @Override
            public void run(@NotNull ProgressIndicator indicator) {
                final String out = replace ? tide(project, file, "graph", file.getPath(), "--replace") : tide(project, file, "graph", file.getPath());
                if (out == null) return;
                final VirtualFile folder = file.getParent();
                folder.refresh(false, false);
                final VirtualFile graph = LocalFileSystem.getInstance().refreshAndFindFileByPath(file.getPath() + "graph");
                if (graph != null) ApplicationManager.getApplication().invokeLater(() -> FileEditorManager.getInstance(project).openFile(graph, true));
            }
        }.queue();
    }

    // Show as Tide, on a .tidegraph
    public static final class ShowAsTide extends DumbAwareAction {
        @Override
        public void update(@NotNull AnActionEvent e) {
            e.getPresentation().setEnabledAndVisible(e.getProject() != null && TideGraphFileType.is(e.getData(CommonDataKeys.VIRTUAL_FILE)));
        }

        @Override
        public @NotNull ActionUpdateThread getActionUpdateThread() {
            return ActionUpdateThread.BGT;
        }

        @Override
        public void actionPerformed(@NotNull AnActionEvent e) {
            final Project project = e.getProject();
            final VirtualFile file = e.getData(CommonDataKeys.VIRTUAL_FILE);
            if (project != null && file != null) showTide(project, file, null);
        }
    }
}
