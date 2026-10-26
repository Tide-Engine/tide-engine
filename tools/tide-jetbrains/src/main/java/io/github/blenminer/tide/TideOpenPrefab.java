package io.github.blenminer.tide;

import com.intellij.execution.ExecutionException;
import com.intellij.execution.configurations.GeneralCommandLine;
import com.intellij.execution.process.OSProcessHandler;
import com.intellij.execution.process.ProcessEvent;
import com.intellij.execution.process.ProcessListener;
import com.intellij.openapi.actionSystem.AnAction;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.actionSystem.ActionUpdateThread;
import com.intellij.openapi.actionSystem.CommonDataKeys;
import com.intellij.openapi.application.ApplicationManager;
import com.intellij.openapi.fileEditor.FileDocumentManager;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.ui.Messages;
import com.intellij.openapi.util.Key;
import com.intellij.openapi.vfs.VirtualFile;
import org.jetbrains.annotations.NotNull;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;

// Launch the native authoring app; the IDE remains the source-code editor.
public final class TideOpenPrefab extends AnAction {
    @Override public @NotNull ActionUpdateThread getActionUpdateThread() { return ActionUpdateThread.BGT; }

    @Override public void update(@NotNull AnActionEvent event) {
        final VirtualFile file = event.getData(CommonDataKeys.VIRTUAL_FILE);
        event.getPresentation().setEnabledAndVisible(event.getProject() != null && file != null
            && file.isInLocalFileSystem() && "tprefab".equalsIgnoreCase(file.getExtension()));
    }

    @Override public void actionPerformed(@NotNull AnActionEvent event) {
        final Project project = event.getProject();
        final VirtualFile file = event.getData(CommonDataKeys.VIRTUAL_FILE);
        if (project == null || file == null || !file.isInLocalFileSystem() || !"tprefab".equalsIgnoreCase(file.getExtension())) return;
        if (!TidelsFactory.isTrusted(project)) {
            Messages.showErrorDialog(project, "Prefab preview can run project code. Trust this project before opening the editor.", "Tide"); return;
        }
        final String base = project.getBasePath();
        final TideGames.Game game = TideGames.of(file.toNioPath(), base != null ? Path.of(base) : null);
        if (game.error() != null) { Messages.showErrorDialog(project, game.error(), "Tide"); return; }
        final var tools = TideProjectTools.find(game.folder(), "tide");
        if (tools.error() != null) { Messages.showErrorDialog(project, tools.error(), "Tide"); return; }
        final Path tide = tools.file();
        if (tide == null) { Messages.showErrorDialog(project, "Couldn't find tide. Install it: " + Tide.INSTALL, "Tide"); return; }
        final var document = FileDocumentManager.getInstance().getCachedDocument(file);
        if (document != null) FileDocumentManager.getInstance().saveDocument(document);
        try {
            final OSProcessHandler handler = new OSProcessHandler(new GeneralCommandLine(tide.toString(), "open", file.getPath(),
                "--project", game.folder().toString()).withWorkDirectory(game.folder().toFile()).withCharset(StandardCharsets.UTF_8));
            handler.addProcessListener(new ProcessListener() {
                private final StringBuilder said = new StringBuilder();
                @Override public synchronized void onTextAvailable(@NotNull ProcessEvent e, @NotNull Key type) {
                    if (said.length() < 4096) said.append(e.getText(), 0, Math.min(e.getText().length(), 4096 - said.length()));
                }
                @Override public synchronized void processTerminated(@NotNull ProcessEvent e) {
                    if (e.getExitCode() == 0) return;
                    final String message = "Prefab editor exited with code " + e.getExitCode() + ".\n" + said;
                    ApplicationManager.getApplication().invokeLater(() -> { if (!project.isDisposed()) Messages.showErrorDialog(project, message, "Tide"); });
                }
            });
            handler.startNotify();
        } catch (ExecutionException e) { Messages.showErrorDialog(project, e.getMessage(), "Tide"); }
    }
}
