using System;
using System.Collections;
using System.Diagnostics;
using System.Reflection;
using NUnit.Framework;
using Tide;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.TestTools;
using Debug = UnityEngine.Debug;
using Object = UnityEngine.Object;

// What a busy match costs Unity each frame: a crowd of balls (Bounce.tide's Crowd), every one a GameObject with its
// live components, beside singletons holding text, a list, a set, a map and a grid. Once the match is as it'll stay,
// a frame of the runner's makes nothing new for the garbage collector, and what a script reads there is the same
// list and the same string as the frame before, with this frame's values.
public sealed class ScaleTests
{
    const string Bounce = "Assets/Sample/Bounce.unity";

    [UnityTearDown]
    public IEnumerator EndTheMatch()
    {
        if (TideRunner.Current != null) Object.Destroy(TideRunner.Current.gameObject);
        yield return null;
    }

    static IEnumerator Until(Func<bool> done, float seconds)
    {
        var until = Time.realtimeSinceStartup + seconds;
        while (!done() && Time.realtimeSinceStartup < until) yield return null;
    }

    static int Shown()
    {
        var shown = 0;
        foreach (var view in Object.FindObjectsByType<TideEntity>())
        {
            if (view.Showing) shown++;
        }
        return shown;
    }

    // How many balls: TIDE_UNITY_CROWD for another number, to measure with
    static int Crowd => int.TryParse(Environment.GetEnvironmentVariable("TIDE_UNITY_CROWD"), out var n) && n > 0 ? n : 300;

    [UnityTest]
    public IEnumerator ABusyMatchsFramesMakeNothingNew()
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        yield return Until(() => runner.State == TideSessionState.Connected && runner.Packet.InMatch, 10f);
        Assert.AreEqual(TideSessionState.Connected, runner.State);
        var crowd = Crowd;
        Assert.IsTrue(runner.Send(new Tide.Game.Say { text = "héllo, wörld" }));
        Assert.IsTrue(runner.Send(new Tide.Game.Crowd { count = crowd }));
        yield return Until(() => Shown() > crowd, 60f);
        Assert.Greater(Shown(), crowd, "the crowd's balls and this machine's show");
        // The balls land and lie still, and their bounces' events end
        var rested = Time.realtimeSinceStartup + 6f;
        while (Time.realtimeSinceStartup < rested) yield return null;

        var extras = runner.Get<Tide.Game.Extras>();
        var room = runner.Get<Tide.Game.Room>();
        Assert.IsNotNull(extras);
        Assert.AreEqual("héllo, wörld", extras.said);
        Assert.AreEqual(crowd, extras.crowd.Count, "the singleton's list of the crowd, GameObjects each");
        Assert.IsNotNull(extras.crowd[crowd - 1], "the last of them shown");
        Assert.AreEqual(1, extras.lines.Count);
        Assert.AreEqual("héllo, wörld", extras.lines[0]);

        // The runner's own frame, called here to be measured alone (Unity's Update calls it too), and the part of it
        // that's Unity's side: the packet's entities shown, placed, their components and the singletons read, the
        // events told (Show, here a second time over the frame's packet)
        const BindingFlags own = BindingFlags.Instance | BindingFlags.NonPublic;
        var frame = (Action)Delegate.CreateDelegate(typeof(Action), runner, typeof(TideRunner).GetMethod("Update", own));
        var show = (Action<bool>)Delegate.CreateDelegate(typeof(Action<bool>), runner, typeof(TideRunner).GetMethod("Show", own));
        for (var i = 0; i < 40; i++)
        {
            frame();
            show(true);
            yield return null;
        }
        var said = extras.said;
        var list = extras.crowd;
        var speakers = extras.speakers;
        var board = extras.board;
        var balls = room.balls;
        const int frames = 120;
        var made = new int[frames];
        var took = new double[frames];
        var showing = new double[frames];
        var allocations = UnityEngine.Profiling.Recorder.Get("GC.Alloc");
        allocations.FilterToCurrentThread();
        allocations.enabled = false;
        long bytes = 0;
        int framed = 0, quiet = 0; // Those of the whole frame, and the frames that made none
        for (var i = 0; i < frames; i++)
        {
            var before = GC.GetTotalMemory(false);
            allocations.enabled = true;
            var start = Stopwatch.GetTimestamp();
            frame();
            var middle = Stopwatch.GetTimestamp();
            allocations.enabled = false;
            framed += allocations.sampleBlockCount;
            if (allocations.sampleBlockCount == 0) quiet++;
            allocations.enabled = true;
            show(true);
            var end = Stopwatch.GetTimestamp();
            allocations.enabled = false;
            made[i] = allocations.sampleBlockCount; // Showing's
            bytes += Math.Max(0, GC.GetTotalMemory(false) - before); // Less after a collection: counted as none
            took[i] = (middle - start) * 1000.0 / Stopwatch.Frequency;
            showing[i] = (end - middle) * 1000.0 / Stopwatch.Frequency;
            yield return null;
        }
        allocations.CollectFromAllThreads();
        var none = 0;
        int most = 0, sum = 0;
        foreach (var count in made)
        {
            if (count == 0) none++;
            most = Math.Max(most, count);
            sum += count;
        }
        Array.Sort(took);
        Array.Sort(showing);
        Debug.Log($"Tide scale: {Shown()} entities shown; a frame of the runner's takes {took[frames / 2]:0.000} ms (median; {took[frames / 10]:0.000} to {took[frames * 9 / 10]:0.000}), " +
                  $"of which showing them in Unity {showing[frames / 2]:0.000} ms ({showing[frames / 10]:0.000} to {showing[frames * 9 / 10]:0.000}); " +
                  $"showing makes {(double)sum / frames:0.0} objects a frame on average, {most} at most, none in {none} of {frames} frames; " +
                  $"the whole frame {(double)framed / frames:0.0}, none in {quiet} ({bytes / frames} bytes for both)");

        // The same objects as before, with the frame's values
        Assert.AreSame(extras, runner.Get<Tide.Game.Extras>());
        Assert.AreSame(said, extras.said, "text that didn't change is the string it was");
        Assert.AreSame(list, extras.crowd, "a list is filled where it is");
        Assert.AreSame(speakers, extras.speakers);
        Assert.AreSame(board, extras.board);
        Assert.AreSame(balls, room.balls);
        Assert.AreEqual(crowd, extras.crowd.Count);
        Assert.AreEqual(1, extras.speakers.Count);
        Assert.AreEqual(1, extras.board[3, 4]);
        Assert.AreEqual(1, room.balls.Count);
        // The editor looks at the built files now and then, which a player doesn't: nearly every frame makes nothing.
        // The whole frame is the game's own frame too, its drawing replayed and Unity's input read
        Assert.GreaterOrEqual(quiet, frames * 9 / 10, $"whole frames of the runner's that made nothing new ({(double)framed / frames:0.0} objects a frame on average)");
        Assert.GreaterOrEqual(none, frames * 9 / 10, $"frames that made nothing new for the garbage collector ({most} objects at most, {(double)sum / frames:0.0} on average)");

        // And what changes is read as it changes
        Assert.IsTrue(runner.Send(new Tide.Game.Say { text = "again" }));
        yield return Until(() => extras.said == "again", 10f);
        Assert.AreEqual("again", extras.said);
        Assert.AreEqual(2, extras.lines.Count);
        Assert.AreEqual("héllo, wörld", extras.lines[0]);
        Assert.AreEqual("again", extras.lines[1]);
        Assert.AreEqual(2, extras.board[3, 4]);
        Assert.IsTrue(runner.Send(new Tide.Game.Crowd { count = 3 }));
        yield return Until(() => extras.crowd.Count == 3, 10f);
        Assert.AreEqual(3, extras.crowd.Count);
        yield return Until(() => Shown() < crowd, 10f);
        Assert.Less(Shown(), crowd, "the balls that went show no more");
    }
}
