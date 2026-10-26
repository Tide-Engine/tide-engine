package io.github.blenminer.tide;

import com.intellij.application.options.CodeStyle;
import com.intellij.codeInsight.editorActions.enter.EnterHandlerDelegate;
import com.intellij.openapi.actionSystem.DataContext;
import com.intellij.openapi.editor.Document;
import com.intellij.openapi.editor.Editor;
import com.intellij.openapi.editor.ScrollType;
import com.intellij.openapi.editor.actionSystem.EditorActionHandler;
import com.intellij.openapi.util.Ref;
import com.intellij.openapi.vfs.VirtualFile;
import com.intellij.psi.PsiFile;
import org.jetbrains.annotations.NotNull;

// Enter between a block's braces, `{|}`, opens the block: the caret on an indented line of its own and the closing
// brace on the next, under the line that opened it, as VS Code does for the grammar's brackets. The TextMate
// support the plugin colors Tide with leaves the brace beside the caret.
public final class TideEnterBetweenBraces implements EnterHandlerDelegate {
    @Override
    public Result preprocessEnter(@NotNull PsiFile file, @NotNull Editor editor, @NotNull Ref<Integer> caretOffset,
                                  @NotNull Ref<Integer> caretAdvance, @NotNull DataContext dataContext,
                                  EditorActionHandler originalHandler) {
        final VirtualFile virtual = file.getVirtualFile();
        final String extension = virtual != null ? virtual.getExtension() : null;
        if (!"tide".equals(extension) && !"tprefab".equals(extension)) return Result.Continue;
        if (editor.getCaretModel().getCaretCount() != 1 || editor.getSelectionModel().hasSelection()) return Result.Continue;
        final Document document = editor.getDocument();
        final CharSequence text = document.getCharsSequence();
        final int offset = editor.getCaretModel().getOffset();
        int open = offset;
        while (open > 0 && blank(text.charAt(open - 1))) open--;
        int close = offset;
        while (close < text.length() && blank(text.charAt(close))) close++;
        if (open == 0 || text.charAt(open - 1) != '{' || close >= text.length() || text.charAt(close) != '}') return Result.Continue;
        // The opening line's own indentation, and one more level for the block's
        final int lineStart = document.getLineStartOffset(document.getLineNumber(open - 1));
        int indentEnd = lineStart;
        while (indentEnd < open - 1 && blank(text.charAt(indentEnd))) indentEnd++;
        final String indent = text.subSequence(lineStart, indentEnd).toString();
        final var options = CodeStyle.getIndentOptions(file);
        final String unit = options.USE_TAB_CHARACTER ? "\t" : " ".repeat(Math.max(1, options.INDENT_SIZE));
        final String inside = "\n" + indent + unit;
        document.replaceString(open, close, inside + "\n" + indent);
        editor.getCaretModel().moveToOffset(open + inside.length());
        editor.getScrollingModel().scrollToCaret(ScrollType.RELATIVE);
        return Result.Stop;
    }

    @Override
    public Result postProcessEnter(@NotNull PsiFile file, @NotNull Editor editor, @NotNull DataContext dataContext) {
        return Result.Continue;
    }

    private static boolean blank(final char c) {
        return c == ' ' || c == '\t';
    }
}
