using System;
using System.Collections;
using System.IO;
using NUnit.Framework;
using Tide.Editor;
using UnityEditor;
using UnityEngine.TestTools;

// The build service starts tide again when its executable is replaced under the editor (tide upgraded or installed
// again, a development tide built again): the running one is the old program, building with the new installation's
// files. What counts as replaced, with a file standing in for tide: another size or time, once it has stayed so.
public class ReplacedTests
{
    string file;

    [SetUp]
    public void Make()
    {
        file = Path.Combine(Path.GetTempPath(), "tide-replaced-" + Guid.NewGuid().ToString("N"));
        File.WriteAllText(file, "the tide that started");
        File.SetLastWriteTimeUtc(file, new DateTime(2021, 1, 1, 0, 0, 0, DateTimeKind.Utc));
    }

    [TearDown]
    public void Clean()
    {
        File.Delete(file);
    }

    [Test]
    public void ATideReplacedUnderTheEditorIsStartedAgainOnceItsFileSettles()
    {
        var info = new FileInfo(file);
        var size = info.Length;
        var written = info.LastWriteTimeUtc;
        Assert.IsFalse(TideBuildService.Replaced(file, size, written, 100), "the file it started from, as it was");
        Assert.IsFalse(TideBuildService.Replaced(file, size, written, 200), "however long it stays so");

        // A linker or an installer writes it: not taken while it still changes
        File.WriteAllText(file, "another tide, half");
        Assert.IsFalse(TideBuildService.Replaced(file, size, written, 300), "just changed");
        Assert.IsFalse(TideBuildService.Replaced(file, size, written, 301), "a second on");
        File.WriteAllText(file, "another tide, whole written");
        Assert.IsFalse(TideBuildService.Replaced(file, size, written, 302.5), "changed again: it waits from here");
        Assert.IsFalse(TideBuildService.Replaced(file, size, written, 304), "not two seconds yet");
        Assert.IsTrue(TideBuildService.Replaced(file, size, written, 304.6), "as it is for two seconds: another tide");

        // The same bytes written again later are another file too (its time), and one that's gone meanwhile is none yet
        File.WriteAllText(file, "the tide that started");
        File.SetLastWriteTimeUtc(file, new DateTime(2022, 1, 1, 0, 0, 0, DateTimeKind.Utc));
        Assert.IsFalse(TideBuildService.Replaced(file, size, written, 400));
        Assert.IsTrue(TideBuildService.Replaced(file, size, written, 402.1), "the same size at another time");
        File.Delete(file);
        Assert.IsFalse(TideBuildService.Replaced(file, size, written, 500), "a file that isn't there is being replaced");

        // Put back as it was, nothing happened
        File.WriteAllText(file, "the tide that started");
        File.SetLastWriteTimeUtc(file, written);
        Assert.IsFalse(TideBuildService.Replaced(file, size, written, 600));
        Assert.IsFalse(TideBuildService.Replaced(file, size, written, 700));
    }

    // ...and the service itself, with the tide it runs: its executable replaced as an installer replaces a running one
    // (the old file moved aside, a new one in its place), and tide is another process a few seconds on. Needs the
    // build service running a development tide (-tide-command): an installed tide's file isn't a test's to touch.
    [UnityTest]
    public IEnumerator TheRunningTideIsStartedAgainWhenItsFileIsReplaced()
    {
        var tide = TideBuildService.RunningTide;
        var was = TideBuildService.RunningId;
        if (was == 0 || tide == null || Array.IndexOf(Environment.GetCommandLineArgs(), "-tide-command") < 0)
        {
            Assert.Ignore("no development tide building for this editor (automatic builds off, or no -tide-command)");
            yield break;
        }
        var aside = tide + ".replaced-" + Guid.NewGuid().ToString("N");
        File.Move(tide, aside); // A running program's file can be moved, not written
        File.Copy(aside, tide);
        File.SetLastWriteTimeUtc(tide, DateTime.UtcNow);
        var until = EditorApplication.timeSinceStartup + 30;
        while (EditorApplication.timeSinceStartup < until && TideBuildService.RunningId == was) yield return null;
        Assert.AreNotEqual(0, TideBuildService.RunningId, "tide is running");
        Assert.AreNotEqual(was, TideBuildService.RunningId, "and it's another process: started again from its new file");
        Assert.AreEqual(tide, TideBuildService.RunningTide);
        was = TideBuildService.RunningId;
        var stays = EditorApplication.timeSinceStartup + 5;
        while (EditorApplication.timeSinceStartup < stays) yield return null;
        Assert.AreEqual(was, TideBuildService.RunningId, "and not again while its file stays");
        // Its first build done, before another test meets a half-built folder; the old program has ended by then
        var built = EditorApplication.timeSinceStartup + 180;
        while (EditorApplication.timeSinceStartup < built && TideBuildService.Describe() == "building the game") yield return null;
        File.Delete(aside);
    }
}
