package io.github.blenminer.tide;

import com.intellij.openapi.project.Project;
import com.redhat.devtools.lsp4ij.client.LanguageClientImpl;
import org.eclipse.lsp4j.PublishDiagnosticsParams;
import org.jetbrains.annotations.NotNull;

// LSP4IJ's client of tidels, which also hands a graph's diagnostics, with the node each is on (`data.node`), to its
// graph editor (TideGraphDiagnostics).
public final class TideLanguageClient extends LanguageClientImpl {
    public TideLanguageClient(@NotNull Project project) {
        super(project);
    }

    @Override
    public void publishDiagnostics(PublishDiagnosticsParams params) {
        super.publishDiagnostics(params);
        final Project project = getProject();
        if (!project.isDisposed()) TideGraphDiagnostics.of(project).publish(params);
    }
}
