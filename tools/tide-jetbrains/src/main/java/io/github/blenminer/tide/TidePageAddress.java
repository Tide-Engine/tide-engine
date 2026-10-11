package io.github.blenminer.tide;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

// Process output can end anywhere, including in the port or page path. Wait
// for the trailing whitespace before handing the address to the browser.
final class TidePageAddress {
    private static final Pattern PAGE = Pattern.compile("tide: the game is at (http://\\S+)\\s");
    private final StringBuilder seen = new StringBuilder();
    private boolean shown;

    String accept(String text) {
        if (shown) return null;
        seen.append(text);
        final Matcher page = PAGE.matcher(seen);
        if (!page.find()) return null;
        final String address = page.group(1);
        shown = true;
        seen.setLength(0);
        return address;
    }
}
