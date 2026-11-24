package io.github.blenminer.tide;

import com.intellij.openapi.fileTypes.FileType;
import com.intellij.openapi.util.IconLoader;
import com.intellij.openapi.vfs.VirtualFile;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import javax.swing.Icon;

// A .tidegraph: Tide source as a graph, JSON as text (notes/visual-scripting.md). The graph editor opens it
// (TideGraphEditor), with its text a tab beside it.
public final class TideGraphFileType implements FileType {
    public static final TideGraphFileType INSTANCE = new TideGraphFileType();
    private static final Icon ICON = IconLoader.getIcon("/icons/tide.svg", TideGraphFileType.class);

    private TideGraphFileType() {}

    static boolean is(@Nullable VirtualFile file) {
        return file != null && "tidegraph".equals(file.getExtension());
    }

    @Override
    public @NotNull String getName() {
        return "Tide Graph";
    }

    @Override
    public @NotNull String getDescription() {
        return "Tide graph";
    }

    @Override
    public @NotNull String getDefaultExtension() {
        return "tidegraph";
    }

    @Override
    public Icon getIcon() {
        return ICON;
    }

    @Override
    public boolean isBinary() {
        return false;
    }
}
