package io.github.blenminer.tide;

import org.junit.Test;

import java.util.List;

import static org.junit.Assert.assertEquals;

// Which processes the debug actions attach to: no IDE, debugger or game.
public final class TideAttachTest {
    private static final boolean WINDOWS = System.getProperty("os.name", "").toLowerCase().startsWith("windows");

    @Test
    public void unityEditorsAreTheProjectsFirst() {
        final List<TideAttach.Running> all = List.of(
            new TideAttach.Running(1, "C:\\Program Files\\Unity\\Editor\\Unity.exe", "\"Unity.exe\" -projectpath C:\\Other -useHub"),
            new TideAttach.Running(2, "C:\\Program Files\\Unity Hub\\Unity Hub.exe", "Unity Hub.exe"),
            new TideAttach.Running(3, "C:\\Program Files\\Unity\\Editor\\Unity.exe", "\"Unity.exe\" -projectpath C:\\Projects\\Game"),
            new TideAttach.Running(4, "/Applications/Unity/Hub/Editor/6000.6.0f1/Unity.app/Contents/MacOS/Unity", "Unity -projectPath /p"));
        assertEquals(List.of(all.get(2)), TideAttach.unityEditors(all, "C:/Projects/Game"));
        // With no project of its own running, every editor is a candidate
        assertEquals(List.of(all.get(0), all.get(2), all.get(3)), TideAttach.unityEditors(all, "C:/Projects/Nothing"));
        assertEquals(List.of(all.get(0), all.get(2), all.get(3)), TideAttach.unityEditors(all, null));
    }

    @Test
    public void gamesAreTheHostsTideRunStartedForTheFolder() {
        final String game = WINDOWS ? "C:\\games\\Bounce" : "/games/Bounce";
        final String sep = WINDOWS ? "\\" : "/";
        final List<TideAttach.Running> all = List.of(
            new TideAttach.Running(10, game + sep + ".tide" + sep + "debug" + sep + "run" + sep + "4242" + sep + "Bounce.exe", ""),
            new TideAttach.Running(11, game + sep + ".tide" + sep + "debug" + sep + "Bounce.exe", ""),
            new TideAttach.Running(12, game + "2" + sep + ".tide" + sep + "debug" + sep + "run" + sep + "1" + sep + "Bounce2.exe", ""),
            new TideAttach.Running(13, game + sep + "build" + sep + "Bounce.exe", ""));
        assertEquals(List.of(all.get(0)), TideAttach.games(all, game));
        assertEquals(List.of(all.get(0)), TideAttach.games(all, game + sep));
    }
}
