package io.github.blenminer.tide;

import com.intellij.openapi.fileTypes.FileType;
import com.jetbrains.cidr.execution.debugger.breakpoints.CidrLineBreakpointFileTypesProvider;
import org.jetbrains.plugins.textmate.TextMateFileType;

import java.util.Set;

// Line breakpoints in .tide files for CLion's native debugger (registered in
// tide-cidr.xml, which only an IDE with that debugger loads). A game's C maps
// each line back to its .tide line, so the debugger finds them by file and
// line as it finds C's. .tide files are TextMate's file type, as the grammar
// colors them, so the gutter takes breakpoints in other TextMate files too.
public final class TideBreakpoints implements CidrLineBreakpointFileTypesProvider {
    @Override
    public Set<FileType> getFileTypes() {
        return Set.of(TextMateFileType.INSTANCE);
    }
}
