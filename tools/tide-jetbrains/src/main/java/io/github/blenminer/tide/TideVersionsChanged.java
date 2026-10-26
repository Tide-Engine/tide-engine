package io.github.blenminer.tide;

import com.intellij.openapi.application.ApplicationManager;
import com.intellij.openapi.project.ProjectManager;
import com.intellij.openapi.vfs.newvfs.BulkFileListener;
import com.intellij.openapi.vfs.newvfs.events.VFileEvent;
import org.jetbrains.annotations.NotNull;
import java.util.List;

// Package updates and branch switches can change which tidels a project needs while its IDE stays open.
public final class TideVersionsChanged implements BulkFileListener {
    @Override public void after(@NotNull List<? extends VFileEvent> events) {
        for (final var project : ProjectManager.getInstance().getOpenProjects()) {
            final String base = project.getBasePath();
            if (base == null || project.isDisposed()) continue;
            final String prefix = base.replace('\\', '/') + "/";
            if (events.stream().noneMatch(event -> {
                final String path = event.getPath().replace('\\', '/');
                return path.startsWith(prefix) && (path.endsWith("/tide.packages") || path.endsWith("/Packages/packages-lock.json")
                    || path.endsWith("/Library/Tide/version.json") || path.endsWith("/Library/Tide/toolchain.json"));
            })) continue;
            ApplicationManager.getApplication().invokeLater(() -> {
                if (!project.isDisposed()) TidelsFactory.restart(project);
            });
        }
    }
}
