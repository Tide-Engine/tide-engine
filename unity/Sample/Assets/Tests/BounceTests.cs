using System.Collections;
using NUnit.Framework;
using Tide;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.TestTools;

// The sample in play mode: Tide runs Bounce's match in the editor, and Unity shows this machine's player's ball,
// placed and colored by the match.
public sealed unsafe class BounceTests
{
    const string Bounce = "Assets/Sample/Bounce.unity";
    const string Bonus = "Assets/Sample/Bonus.unity";

    // The runner goes on across scene loads (the match decides which are loaded): each test ends its own
    [UnityTearDown]
    public IEnumerator EndTheMatch()
    {
        if (TideRunner.Current != null) Object.Destroy(TideRunner.Current.gameObject);
        yield return null;
    }

    // Waits by real time (a batch run's frames are short) for `done`, up to `seconds`.
    static IEnumerator Until(System.Func<bool> done, float seconds)
    {
        var until = Time.realtimeSinceStartup + seconds;
        while (!done() && Time.realtimeSinceStartup < until) yield return null;
    }

    static TideEntityView Totem()
    {
        var scene = SceneManager.GetSceneByPath(Bonus);
        if (!scene.IsValid() || !scene.isLoaded) return null;
        foreach (var root in scene.GetRootGameObjects())
        {
            var totem = root.transform.Find("Totem");
            var view = totem != null ? totem.GetComponent<TideEntityView>() : null;
            if (view != null && view.Showing) return view;
        }
        return null;
    }

    // The match loads Bonus's entities when its code says, and Unity loads the scene with them beside Bounce; and
    // unloads it as the match does.
    [UnityTest]
    public IEnumerator TheMatchLoadsAndUnloadsScenes()
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        yield return Until(() => runner.State == TideSessionState.Connected && runner.Packet.InMatch, 10f);
        Assert.AreEqual(TideSessionState.Connected, runner.State);
        Assert.IsFalse(SceneManager.GetSceneByPath(Bonus).isLoaded, "Bonus isn't the match's yet");
        Assert.IsTrue(runner.Send(new Tide.Game.ShowBonus { on = true }), "the command is the game's");
        yield return Until(() => Totem() != null, 10f);
        var totem = Totem();
        Assert.IsNotNull(totem, "Unity loaded Bonus as the match did, and the Totem shows its entity");
        Assert.IsTrue(totem.TryGet(out Tide.Game.Spinner spinner) && spinner.speed == 3f, "with the scene's Spinner");
        Assert.IsTrue(SceneManager.GetSceneByPath(Bounce).isLoaded, "beside Bounce");
        // Spinning is "without scene Bonus": the totem stays as Bonus has it
        var turn = totem.transform.rotation;
        yield return Until(() => false, 1f);
        Assert.Less(Quaternion.Angle(turn, totem.transform.rotation), 0.01f, "Bonus's totem doesn't spin");
        runner.Send(new Tide.Game.ShowBonus { on = false });
        yield return Until(() => !SceneManager.GetSceneByPath(Bonus).isLoaded, 10f);
        Assert.IsFalse(SceneManager.GetSceneByPath(Bonus).isLoaded, "and unloaded it as the match did");
    }

    // With Bonus alone open, the match has it rather than Build Settings' first, Bounce, which it loaded as Main
    // started and then let go, and Unity with it.
    [UnityTest]
    public IEnumerator TheEditorsOpenScenesAloneAreTheMatchs()
    {
        SceneManager.LoadScene(Bonus);
        yield return null;
        new GameObject("Tide").AddComponent<TideRunner>(); // As one is made for a level opened alone
        yield return Until(() => Totem() != null && !SceneManager.GetSceneByPath(Bounce).isLoaded, 10f);
        Assert.IsNotNull(Totem(), "the match has Bonus's entities");
        // Bounce came as Main started, and went with the editor's command
        yield return Until(() => !SceneManager.GetSceneByPath(Bounce).isLoaded, 5f);
        Assert.IsFalse(SceneManager.GetSceneByPath(Bounce).isLoaded, "and not Bounce's");
    }

    // In the editor, the match has the scenes the editor has open: Bonus too, without the game's code asking.
    [UnityTest]
    public IEnumerator TheEditorsOpenScenesAreTheMatchs()
    {
        SceneManager.LoadScene(Bounce);
        SceneManager.LoadScene(Bonus, LoadSceneMode.Additive);
        yield return null;
        yield return Until(() => Totem() != null, 10f);
        Assert.IsNotNull(Totem(), "the match loaded Bonus's entities, which the open scene shows");
        Assert.IsNotNull(GameObject.Find("Pillars")?.transform.GetChild(0).GetComponent<TideEntityView>(), "and Bounce's");
    }

    // Whether the frame's draw packet (tide/embed.h's tide_embed_draw) has a text command saying `words`.
    static bool DrewText(TideRunner runner, string words)
    {
        var bytes = runner.DrawPacket(out var length);
        if (bytes == null || length < 64) return false;
        var header = (uint*)bytes;
        ulong at = 64;
        ulong Part(uint count, uint each)
        {
            var from = (at + 7) & ~7ul;
            at = from + (ulong)count * each;
            return from;
        }
        var commands = (TideDrawCommand*)(bytes + Part(header[2], header[9]));
        Part(header[3], header[10]);
        Part(header[4], header[11]);
        Part(header[5], 4);
        var text = bytes + Part(header[6], 1);
        for (var i = 0; i < header[2]; i++)
        {
            if (commands[i].kind != 7 || commands[i].text >= header[6]) continue;
            var start = text + commands[i].text;
            var n = 0;
            while (start[n] != 0) n++;
            if (System.Text.Encoding.UTF8.GetString(start, n) == words) return true;
        }
        return false;
    }

    [UnityTest]
    public IEnumerator TheMatchShowsItsBall()
    {
        SceneManager.LoadScene("Assets/Sample/Bounce.unity");
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        Assert.IsNotNull(runner, "the scene has its TideRunner");
        TideEntityView ball = null;
        for (var frame = 0; frame < 300 && ball == null; frame++)
        {
            yield return null;
            foreach (var view in Object.FindObjectsByType<TideEntityView>())
            {
                if (view.Has<Tide.Game.Ball>()) ball = view;
            }
        }
        Assert.IsTrue(runner.Running, "the game started");
        Assert.AreEqual(TideSessionState.Connected, runner.State);
        Assert.IsNotNull(ball, "the player's ball is shown");
        Assert.IsTrue(ball.Mine, "and it's this machine's");
        Assert.IsTrue(ball.TryGet(out Tide.Game.Ball value), "it has Tide's Ball, read through the generated type");
        Assert.Greater(value.color.r + value.color.g + value.color.b, 0.1f, "with the color the match gave it");
        // Dropped from 2 units up, the ball falls onto the floor and stays on it: Tide's clock is the frames' real
        // lengths, which a run in batch mode makes short, so this waits by time
        var pillars = GameObject.Find("Pillars").transform;
        var first = pillars.GetChild(0).rotation;
        var second = pillars.GetChild(1).rotation;
        var until = Time.realtimeSinceStartup + 3f;
        while (Time.realtimeSinceStartup < until) yield return null;
        var y = ball.transform.position.y;
        Assert.That(y, Is.InRange(0.45f, 0.6f), "it fell to the floor: " + y);
        Assert.Greater(Tide.Game.TideGameSchema.Hash.Length, 0, "the bindings were made from the game's schema");
        // What the Hud view drew, for Unity to draw over its frame: its text among the draw packet's commands
        Assert.IsTrue(DrewText(runner, "Balls: 1"), "the draw packet has the Hud's text");
        // The scene's pillars are the match's Spinner entities (both named Pillar), turned by Tide where Unity has them
        for (var i = 0; i < 2; i++)
        {
            var pillar = pillars.GetChild(i);
            var view = pillar.GetComponent<TideEntityView>();
            Assert.IsNotNull(view, $"pillar {i} shows an entity");
            Assert.IsTrue(view.TryGet(out Tide.Game.Spinner spinner), $"pillar {i} has its Spinner");
            Assert.AreEqual(i == 0 ? 1.5f : -0.75f, spinner.speed, 1e-6f, "with the speed the scene gave it");
            Assert.AreEqual(i == 0 ? -3.5f : 3.5f, pillar.position.x, 1e-3f, "placed as Unity has it, under Pillars");
            Assert.AreEqual(3.5f, pillar.position.z, 1e-3f);
            Assert.Greater(Quaternion.Angle(i == 0 ? first : second, pillar.rotation), 10f, $"pillar {i} turned");
        }
    }
}
