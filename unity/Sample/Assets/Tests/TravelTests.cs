using System.Collections;
using System.Collections.Generic;
using NUnit.Framework;
using Tide;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.TestTools;

// Time travel in play mode (TideRunner's, which Window > Tide > Time Travel drives): the editor's match paused and
// stepped a tick at a time, kept ticks shown again with Unity's GameObjects following them, a recording replayed from
// a file bit for bit, going on from a tick shown, and a hot reload while paused.
public sealed unsafe class TravelTests
{
    const string Bounce = "Assets/Sample/Bounce.unity";

    [UnityTearDown]
    public IEnumerator EndTheMatch()
    {
        if (TideRunner.Current != null) Object.Destroy(TideRunner.Current.gameObject);
        yield return null;
    }

    static IEnumerator Until(System.Func<bool> done, float seconds)
    {
        var until = Time.realtimeSinceStartup + seconds;
        while (!done() && Time.realtimeSinceStartup < until) yield return null;
    }

    static TideEntityView MyBall()
    {
        foreach (var view in Object.FindObjectsByType<TideEntityView>())
        {
            if (view.Showing && view.Mine && view.GetComponent<Tide.Game.Ball>() != null) return view;
        }
        return null;
    }

    static TideEntityView Tossed()
    {
        foreach (var view in Object.FindObjectsByType<TideEntityView>())
        {
            if (view.Showing && view.TryGetComponent(out Tide.Game.Ball ball) && ball.seat == -1) return view;
        }
        return null;
    }

    // The match running, this machine's ball showing
    static IEnumerator Started(System.Action<TideRunner, TideEntityView> found)
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        TideEntityView ball = null;
        yield return Until(() => runner.State == TideSessionState.Connected && (ball = MyBall()) != null, 10f);
        Assert.IsNotNull(ball, "the player's ball shows");
        found(runner, ball);
    }

    // A step runs at the next frame
    static IEnumerator Step(TideRunner runner)
    {
        Assert.IsTrue(runner.Step(), "a step: " + runner.Travel.Why);
        yield return null;
        yield return null;
    }

    // Paused, the match runs no tick and its ball stays where it is; a step runs one tick; going on, it runs again
    [UnityTest]
    public IEnumerator PausingHoldsTheMatchAndStepsATick()
    {
        TideRunner runner = null;
        TideEntityView ball = null;
        yield return Started((r, b) => (runner, ball) = (r, b));
        Assert.AreEqual(TideTravelState.Live, runner.Travel.state, "the editor's match is recorded: " + runner.Travel.Why);
        Assert.IsTrue(runner.Pause());
        yield return null;
        var at = runner.Travel.last;
        Assert.AreEqual(TideTravelState.Paused, runner.Travel.state);
        Assert.AreEqual(at, runner.Travel.shown);
        var where = ball.transform.position;
        yield return Until(() => false, 0.5f);
        Assert.AreEqual(at, runner.Travel.last, "no tick while paused");
        Assert.AreEqual(at, runner.Status.verifiedTick);
        Assert.AreEqual(where, ball.transform.position, "the ball stays as the tick has it");
        yield return Step(runner);
        Assert.AreEqual(at + 1, runner.Travel.last, "one tick");
        Assert.AreEqual(at + 1, runner.Status.verifiedTick);
        Assert.AreEqual(at + 1, runner.Travel.shown);
        yield return Until(() => false, 0.3f);
        Assert.AreEqual(at + 1, runner.Travel.last, "and only one");
        Assert.IsTrue(runner.Resume());
        yield return Until(() => runner.Travel.last > at + 10, 3f);
        Assert.Greater(runner.Travel.last, at + 10, "it goes on");
        Assert.AreEqual(TideTravelState.Live, runner.Travel.state);
    }

    // A kept tick shows again: Unity's GameObjects as the tick had them (a ball tossed after it gone, the player's
    // ball where it was, its live Ball component's values), and the world's hash the same
    [UnityTest]
    public IEnumerator ScrubbingShowsAKeptTickWithItsGameObjects()
    {
        TideRunner runner = null;
        TideEntityView ball = null;
        yield return Started((r, b) => (runner, ball) = (r, b));
        Assert.IsTrue(runner.Pause());
        yield return null;
        var positions = new Dictionary<uint, Vector3>();
        var velocities = new Dictionary<uint, Vector3>();
        var hashes = new Dictionary<uint, ulong>();
        void Note()
        {
            var t = runner.Travel.last;
            positions[t] = ball.transform.position;
            velocities[t] = ball.GetComponent<Tide.Game.Ball>().velocity;
            hashes[t] = runner.Travel.hash;
        }
        Note();
        for (var i = 0; i < 8; i++)
        {
            yield return Step(runner);
            Note();
        }
        var before = runner.Travel.last;
        Assert.IsTrue(runner.Send(new Tide.Game.Toss { on = true }), "a command while paused waits for the next tick");
        yield return Step(runner);
        var tossed = runner.Travel.last;
        Assert.IsNotNull(Tossed(), "the tossed ball shows, at the tick the step ran");
        for (var i = 0; i < 4; i++) yield return Step(runner);
        Note();

        Assert.IsTrue(runner.ShowTick(before));
        yield return null;
        Assert.AreEqual(before, runner.Travel.shown);
        Assert.IsNull(Tossed(), "the tick before the toss has no tossed ball");
        Assert.AreEqual(hashes[before], runner.Travel.hash, "its world, as it was");
        Assert.Less(Vector3.Distance(positions[before], ball.transform.position), 1e-4f, "the ball where it was");
        Assert.AreEqual(velocities[before], ball.GetComponent<Tide.Game.Ball>().velocity, "its component's live values the tick's");
        var earlier = before - 6;
        Assert.IsTrue(runner.ShowTick(earlier));
        yield return null;
        Assert.AreEqual(hashes[earlier], runner.Travel.hash);
        Assert.Less(Vector3.Distance(positions[earlier], ball.transform.position), 1e-4f);
        Assert.IsTrue(runner.ShowTick(tossed + 1));
        yield return null;
        Assert.IsNotNull(Tossed(), "after the toss, the tossed ball again");
        Assert.IsFalse(runner.ShowTick(runner.Travel.last + 1), "no tick past the latest");
        Assert.IsTrue(runner.Step());
        yield return null;
        Assert.AreEqual(tossed + 2, runner.Travel.shown, "a step from a tick shown shows the next");
        Assert.IsTrue(runner.Resume());
        yield return Until(() => false, 0.3f);
        Assert.Greater(runner.Travel.last, tossed + 5, "going on, from the latest");
        Assert.IsNotNull(Tossed());
    }

    // The match's recording, saved to a file and replayed: every tick's world has the hash the match's had, its
    // GameObjects as the match had them, and each tick checked as it plays
    [UnityTest]
    public IEnumerator ARecordingReplaysFromAFileBitForBit()
    {
        TideRunner runner = null;
        TideEntityView ball = null;
        yield return Started((r, b) => (runner, ball) = (r, b));
        var held = true;
        TideRunner.InputEdit right = (ref TideEmbedInput input) => { if (held) input.Hold("d"); };
        runner.EditInput += right;
        Assert.IsTrue(runner.Send(new Tide.Game.Say { text = "recorded" }));
        yield return Until(() => false, 1f);
        held = false;
        runner.EditInput -= right;
        yield return Until(() => false, 0.3f);
        Assert.IsTrue(runner.Pause());
        yield return null;
        var live = runner.Travel;
        var hashes = new Dictionary<uint, ulong>();
        for (var tick = live.first; tick <= live.last; tick++)
        {
            Assert.IsTrue(runner.ShowTick(tick));
            hashes[tick] = runner.Travel.hash;
        }
        var middle = (live.first + live.last) / 2;
        Assert.IsTrue(runner.ShowTick(middle));
        yield return null;
        var there = ball.transform.position;
        var path = System.IO.Path.Combine(Application.temporaryCachePath, "travel-test.tiderec");
        Assert.IsTrue(runner.SaveRecording(path));
        Assert.IsTrue(runner.ReplayFile(path), "replayed: " + runner.Travel.Refused);
        var t = runner.Travel;
        Assert.AreEqual(TideTravelState.Replay, t.state);
        Assert.AreEqual(live.first, t.first);
        Assert.AreEqual(live.last, t.last);
        Assert.AreEqual(0, t.otherBuild);
        Assert.IsTrue(runner.Resume(), "it plays");
        yield return Until(() => runner.Travel.playing == 0, 20f);
        t = runner.Travel;
        Assert.AreEqual(t.last, t.shown, "played to its end");
        Assert.AreEqual(t.last, t.checkedTo, "every tick checked");
        Assert.AreEqual(0u, t.differs, $"none went otherwise (from {t.differs}: {t.expected:x16} against {t.got:x16})");
        Assert.AreEqual(0u, t.unreadable);
        for (var tick = t.first; tick <= t.last; tick++)
        {
            Assert.IsTrue(runner.ShowTick(tick));
            Assert.AreEqual(hashes[tick], runner.Travel.hash, $"tick {tick}'s world");
        }
        Assert.IsTrue(runner.ShowTick(middle));
        yield return null;
        var replayed = MyBall();
        Assert.IsNotNull(replayed, "the recording's player's ball shows as this machine's");
        Assert.Less(Vector3.Distance(there, replayed.transform.position), 1e-4f, "where the match had it");
        Assert.AreEqual("recorded", runner.Get<Tide.Game.Extras>()?.said, "with what the recording's command said");
        runner.EndReplay();
        yield return null;
        Assert.AreEqual(TideTravelState.Paused, runner.Travel.state, "back to the match, paused");
        Assert.AreEqual(live.last, runner.Travel.last);
        var bytes = System.IO.File.ReadAllBytes(path);
        bytes[13] ^= 1; // Its layout's hash
        Assert.IsFalse(runner.Replay(bytes));
        StringAssert.Contains("laid out otherwise", runner.Travel.Refused);
        System.IO.File.Delete(path);
    }

    // Going on from a tick shown: the match goes back there, a ball tossed after it gone for good
    [UnityTest]
    public IEnumerator ContinuingFromATickTakesTheMatchBack()
    {
        TideRunner runner = null;
        yield return Started((r, b) => runner = r);
        Assert.IsTrue(runner.Pause());
        yield return null;
        yield return Step(runner);
        var back = runner.Travel.last;
        Assert.IsTrue(runner.Send(new Tide.Game.Toss { on = true }));
        for (var i = 0; i < 5; i++) yield return Step(runner);
        Assert.IsNotNull(Tossed());
        Assert.IsTrue(runner.ShowTick(back));
        Assert.IsTrue(runner.ContinueFromHere());
        yield return null;
        Assert.AreEqual(TideTravelState.Live, runner.Travel.state);
        yield return Until(() => false, 1f);
        Assert.IsNull(Tossed(), "the toss went with the ticks after it");
        Assert.Greater(runner.Travel.last, back + 10, "and the match goes on from there");
        Assert.IsTrue(runner.Send(new Tide.Game.Toss { on = true }));
        yield return Until(() => Tossed() != null, 5f);
        Assert.IsNotNull(Tossed(), "a command after it goes in");
    }

    // A hot reload while paused: the new build comes in, the match stays paused at its tick, the ticks kept show again
    // as they were, and the next step runs the new build
    [UnityTest]
    public IEnumerator AHotReloadWhilePausedKeepsThePause()
    {
        TideRunner runner = null;
        yield return Started((r, b) => runner = r);
        Assert.IsTrue(runner.Pause());
        yield return null;
        for (var i = 0; i < 3; i++) yield return Step(runner);
        var last = runner.Travel.last;
        Assert.IsTrue(runner.ShowTick(last - 2));
        var hash = runner.Travel.hash;
        var folder = TideLibrary.BuildFolder;
        int Newest(string prefix)
        {
            var newest = 0;
            foreach (var file in System.IO.Directory.GetFiles(folder, prefix + "*" + TideLibrary.LibrarySuffix))
            {
                var name = System.IO.Path.GetFileNameWithoutExtension(file);
                if (int.TryParse(name.Substring(prefix.Length), out var n) && n > newest) newest = n;
            }
            return newest;
        }
        var running = Newest("loaded-");
        var built = Newest("game-");
        System.IO.File.Copy(System.IO.Path.Combine(folder, $"game-{built}{TideLibrary.LibrarySuffix}"),
            System.IO.Path.Combine(folder, $"game-{built + 1}{TideLibrary.LibrarySuffix}"), true);
        yield return Until(() => Newest("loaded-") > running, 5f);
        Assert.Greater(Newest("loaded-"), running, "a newer build took over while paused");
        yield return null;
        var t = runner.Travel;
        Assert.AreEqual(TideTravelState.Paused, t.state, "still paused");
        Assert.AreEqual(last, t.last, "at its tick");
        Assert.AreEqual(last - 2, t.shown, "showing what it showed");
        Assert.AreEqual(hash, t.hash, "a kept tick as it was");
        Assert.IsTrue(runner.ShowTick(last));
        yield return Step(runner);
        Assert.AreEqual(last + 1, runner.Travel.last, "the new build steps");
    }
}
