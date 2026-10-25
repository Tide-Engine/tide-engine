using System;
using System.Collections;
using System.Reflection;
using NUnit.Framework;
using Tide.Editor;
using UnityEngine.TestTools;

// tide's errors in the Console: one naming a .tide file's line is an entry with that file and line, which a double
// click opens there, as a C# error's, and the next build takes it away.
public class ConsoleTests
{
    const BindingFlags Any = BindingFlags.Static | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
    static readonly Type entries = Type.GetType("UnityEditor.LogEntries, UnityEditor");
    static readonly Type entryType = Type.GetType("UnityEditor.LogEntry, UnityEditor");

    static int Count() => (int)entries.GetMethod("GetCount", Any).Invoke(null, null);

    // The Console's last entry: its message, file and line
    static (string message, string file, int line) Last()
    {
        var entry = Activator.CreateInstance(entryType);
        entries.GetMethod("StartGettingEntries", Any).Invoke(null, null);
        try
        {
            entries.GetMethod("GetEntryInternal", Any).Invoke(null, new[] { Count() - 1, entry });
        }
        finally
        {
            entries.GetMethod("EndGettingEntries", Any).Invoke(null, null);
        }
        string Text(string name) => (string)entryType.GetField(name, Any)?.GetValue(entry);
        return (Text("message"), Text("file"), (int)entryType.GetField("line", Any).GetValue(entry));
    }

    [Test]
    public void AnErrorAtALineOpensItsFileThere()
    {
        Assume.That(entries != null && entryType != null, "this Unity has the Console's entries");
        LogAssert.ignoreFailingMessages = true;
        TideBuildService.RemoveLocated();
        var before = Count();
        TideBuildService.Log("Tide/Bounce.tide:3:5: error: made up by a test\n    3 | component Spinner", true);
        Assert.AreEqual(before + 1, Count());
        var (message, file, line) = Last();
        StringAssert.StartsWith("tide: Tide/Bounce.tide:3:5: error: made up by a test", message);
        Assert.AreEqual("Assets/Tide/Bounce.tide", file);
        Assert.AreEqual(3, line);

        TideBuildService.RemoveLocated(); // As a build with nothing to say does
        Assert.AreEqual(before, Count());
    }

    // One about a whole prefab or scene, with no line, opens the asset; one about no file has no C# stack under it
    [Test]
    public void AnErrorAboutAnAssetOpensItAndNoneHasACSharpStack()
    {
        Assume.That(entries != null && entryType != null, "this Unity has the Console's entries");
        LogAssert.ignoreFailingMessages = true;
        TideBuildService.RemoveLocated();
        var before = Count();
        TideBuildService.Log("Assets/Sample/Roller.prefab: error: made up by a test", true);
        Assert.AreEqual(before + 1, Count());
        var (message, file, line) = Last();
        StringAssert.StartsWith("tide: Assets/Sample/Roller.prefab: error: made up by a test", message);
        Assert.AreEqual("Assets/Sample/Roller.prefab", file);
        Assert.AreEqual(0, line);
        TideBuildService.RemoveLocated();
        Assert.AreEqual(before, Count());

        TideBuildService.Log("tide: can't do what a test made up", true);
        Assert.AreEqual(before + 1, Count());
        (message, file, _) = Last();
        StringAssert.StartsWith("tide: can't do what a test made up", message);
        StringAssert.DoesNotContain("TideBuildService", message);
        Assert.IsEmpty(file);
        TideBuildService.RemoveLocated(); // An error naming no file goes with the next build too
        Assert.AreEqual(before, Count());
    }

    // A build is a task in the editor's status bar, from tide's "building" line to how it went, and nothing in the
    // Console. Unity starts and takes away tasks at its next update.
    [UnityTest]
    public IEnumerator ABuildIsATaskInTheStatusBarAndNoneOfTheConsoles()
    {
        Assume.That(entries != null, "this Unity has the Console's entries");
        var before = Count();
        bool Shown()
        {
            foreach (var item in UnityEditor.Progress.EnumerateItems())
            {
                if (item.name == TideBuildService.TaskName && item.running) return true;
            }
            return false;
        }

        // The editor's own tide, whose lines would come in the frames between, stops for the while
        var running = TideBuildService.Running;
        TideBuildService.Stop();
        try
        {
            TideBuildService.Hear("tide: building game-41");
            Assert.IsTrue(TideBuildService.Busy);
            yield return null;
            Assert.IsTrue(Shown());
            TideBuildService.Hear("tide: built game-41");
            Assert.IsFalse(TideBuildService.Busy);
            yield return null;
            Assert.IsFalse(Shown());

            TideBuildService.Hear("tide: building game-42");
            yield return null;
            Assert.IsTrue(Shown());
            TideBuildService.Hear("tide: the game keeps running its last build");
            yield return null;
            Assert.IsFalse(Shown());
            Assert.AreEqual(before, Count());
        }
        finally
        {
            if (running) TideBuildService.Start();
        }
    }
}
