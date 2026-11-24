package io.github.blenminer.tide;

import com.intellij.openapi.fileEditor.FileEditor;
import com.intellij.openapi.project.DumbAware;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.ui.EditorNotificationPanel;
import com.intellij.ui.EditorNotificationProvider;
import com.intellij.ui.jcef.JBCefApp;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.JComponent;
import java.util.function.Function;

// Above a .tidegraph shown as text because the IDE has no JCEF, which the graph editor runs in: why, and what to do.
public final class TideGraphBanner implements EditorNotificationProvider, DumbAware {
    static final String NO_JCEF = "This IDE runs without JCEF, the browser Tide's graph editor is a page of, so the graph shows as its JSON."
        + " Choose a boot runtime with JCEF (Help | Find Action | Choose Boot Java Runtime for the IDE) to edit it as a graph.";

    @Override
    public @Nullable Function<? super FileEditor, ? extends JComponent> collectNotificationData(@NotNull Project project,
                                                                                              @NotNull VirtualFile file) {
        if (!TideGraphFileType.is(file) || JBCefApp.isSupported()) return null;
        return editor -> {
            final EditorNotificationPanel panel = new EditorNotificationPanel(editor, EditorNotificationPanel.Status.Warning);
            panel.setText(NO_JCEF);
            panel.createActionLabel("Show as Tide", () -> TideGraphActions.showTide(project, file, null));
            return panel;
        };
    }
}
