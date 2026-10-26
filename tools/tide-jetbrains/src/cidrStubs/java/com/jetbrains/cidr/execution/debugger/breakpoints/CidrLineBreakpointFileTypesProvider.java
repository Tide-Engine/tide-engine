package com.jetbrains.cidr.execution.debugger.breakpoints;

import java.util.Set;

// Compiled against only, never packaged: CLion's native debugger's extension
// point for the file types its line breakpoints go in (nativeDebug plugin,
// cidr.debugger.lineBreakpointFileTypesProvider). The plugin's
// TideBreakpoints implements the real one, which CLion supplies when it loads
// tide-cidr.xml. Its method's descriptor, ()Ljava/util/Set;, is the real
// one's; verifyPlugin checks it against CLion.
public interface CidrLineBreakpointFileTypesProvider {
    Set<?> getFileTypes();
}
