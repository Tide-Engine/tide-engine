package io.github.blenminer.tide;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

public final class TidePageAddressTest {
    @Test
    public void everyOutputSplitKeepsTheCompleteAddress() {
        final String line = "tide: the game is at http://localhost:12345/index.html\r\n";
        for (int split = 0; split < line.length() - 1; split++) {
            final TidePageAddress reader = new TidePageAddress();
            assertNull("split " + split, reader.accept(line.substring(0, split)));
            assertEquals("http://localhost:12345/index.html", reader.accept(line.substring(split)));
            assertNull(reader.accept(line));
        }
    }

    @Test
    public void individualCharactersAndEarlierOutputAreAccepted() {
        final TidePageAddress reader = new TidePageAddress();
        assertNull(reader.accept("Compiling the game...\n"));
        final String line = "tide: the game is at http://localhost:54321/";
        for (int i = 0; i < line.length(); i++) assertNull(reader.accept(line.substring(i, i + 1)));
        assertEquals("http://localhost:54321/", reader.accept("\nWatching...\n"));
    }
}
