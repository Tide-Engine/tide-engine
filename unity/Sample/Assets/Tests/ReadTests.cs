using System.Collections;
using NUnit.Framework;
using Tide;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.TestTools;

// The game's frame carries what Unity reads of the match, and nothing else: a component once one of it is enabled on
// a GameObject or a script asked, a singleton once a script asked, an event while something hears it. What a script
// asks for the first time is there in the same frame.
public sealed class ReadTests
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

    static TideEntity MyBall()
    {
        foreach (var view in Object.FindObjectsByType<TideEntity>())
        {
            if (view.Showing && view.Mine && view.GetComponent<Tide.Game.Ball>() != null) return view;
        }
        return null;
    }

    static IEnumerator Start(System.Action<TideRunner> started)
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        yield return Until(() => runner.State == TideSessionState.Connected && runner.Packet.InMatch && MyBall() != null, 10f);
        Assert.IsNotNull(MyBall(), "this machine's ball shows");
        started(runner);
    }

    // What's on GameObjects is read, a component the match gave an entity that no GameObject has isn't, until a
    // script asks for the component: then every entity that has it has it, in that frame.
    [UnityTest]
    public IEnumerator AComponentIsReadWhileAGameObjectHasItOrAScriptAsked()
    {
        TideRunner runner = null;
        yield return Start(r => runner = r);
        var ball = MyBall();
        Assert.IsTrue(runner.Reads("Ball"), "the prefab's Ball is read");
        Assert.IsTrue(runner.Reads("Spinner"), "and the scene's pillars' Spinner");
        Assert.IsNotNull(ball.GetComponent<Tide.Game.Ball>());
        Assert.IsTrue(runner.Reads("Transform"), "the runner places GameObjects by it");
        // Owner: the match gave it to the ball as it spawned; nothing in Unity reads it
        var owner = runner.Schema.TypeIndex("Owner");
        Assert.GreaterOrEqual(owner, 0);
        Assert.IsFalse(runner.Reads("Owner"), "what nothing reads isn't in the packet");
        Assert.AreEqual(0, runner.Packet.Count((uint)owner, out _));
        Assert.IsNull(ball.GetComponent<Tide.Game.Owner>(), "and no GameObject is given it");
        var small = runner.Packet.Size;

        runner.Read<Tide.Game.Owner>();
        Assert.IsTrue(runner.Reads("Owner"), "asked for: in the frame's packet, written again");
        Assert.Greater(runner.Packet.Size, small);
        var given = ball.GetComponent<Tide.Game.Owner>();
        Assert.IsNotNull(given, "and on the ball's GameObject at once");
        Assert.AreNotEqual(0u, given.player, "with the entity's value");
        Assert.IsNotNull(ball.GetComponent<Tide.Game.Ball>(), "the ball's other component stays");
        Assert.AreSame(ball, MyBall(), "and its GameObject is the one it was");
        yield return null;
        yield return null;
        Assert.IsTrue(runner.Reads("Owner"), "from then on");
        Assert.AreSame(given, ball.GetComponent<Tide.Game.Owner>());

        // Everything, as a setting: what the packet carried before any of this
        runner.readEverything = true;
        yield return null;
        yield return null;
        Assert.IsTrue(runner.Reads("Room"));
        Assert.IsTrue(runner.Reads("Bounced"));
    }

    // A script asking for an entity's component by its value reads it too, there in the same frame.
    [UnityTest]
    public IEnumerator AskingAnEntityForAValueReadsIt()
    {
        TideRunner runner = null;
        yield return Start(r => runner = r);
        var ball = MyBall();
        Assert.IsFalse(runner.Reads("Owner"));
        Assert.IsTrue(ball.Has<Tide.Game.Owner.Value>(), "the entity has it, asked for the first time");
        Assert.IsTrue(ball.TryGet<Tide.Game.Owner.Value>(out var owner));
        Assert.AreNotEqual(0u, owner.player);
        Assert.IsTrue(runner.Reads("Owner"));
        Assert.IsTrue(ball.TryGet<Tide.Game.Ball.Value>(out var value), "what was read before is still there");
        Assert.AreEqual(ball.GetComponent<Tide.Game.Ball>().seat, value.seat);
        // A pillar has none
        foreach (var view in Object.FindObjectsByType<TideEntity>())
        {
            if (view.Showing && view.GetComponent<Tide.Game.Spinner>() != null) Assert.IsFalse(view.Has<Tide.Game.Owner.Value>());
        }
    }

    // A singleton is read once a script asks for it, and is there at once; an event while something hears it.
    [UnityTest]
    public IEnumerator SingletonsAndEventsAreReadOnceAsked()
    {
        TideRunner runner = null;
        yield return Start(r => runner = r);
        Assert.IsFalse(runner.Reads("Room"), "a singleton nothing asked for isn't read");
        Assert.IsFalse(runner.Reads("Extras"));
        var room = runner.Get<Tide.Game.Room>();
        Assert.IsNotNull(room, "asked for: there in the same frame");
        Assert.AreEqual(1, room.arrivals);
        Assert.AreEqual(1, room.balls.Count);
        Assert.IsTrue(runner.Reads("Room"));
        Assert.IsFalse(runner.Reads("Extras"), "the others stay out");
        yield return null;
        Assert.AreSame(room, runner.Get<Tide.Game.Room>(), "the same object from then on");
        Assert.IsTrue(runner.TryGetSingleton<Tide.Game.Extras.Value>(out _), "by its value too");
        Assert.IsTrue(runner.Reads("Extras"));

        // The ball this test's match gives is heard bouncing only by what listens
        var heard = 0;
        System.Action<Tide.Game.Bounced> listen = e => heard++;
        runner.On(listen);
        yield return null;
        Assert.IsTrue(runner.Reads("Bounced"), "an event something hears is read");
        runner.Off(listen);
    }

    // Disabling a component says its GameObject doesn't read it: while the only ones there are disabled the packet
    // leaves the type out, the component stays as it was, and enabling one reads it again.
    [UnityTest]
    public IEnumerator ADisabledComponentIsNotRead()
    {
        TideRunner runner = null;
        yield return Start(r => runner = r);
        var ball = MyBall();
        var live = ball.GetComponent<Tide.Game.Ball>();
        Assert.IsTrue(runner.Reads("Ball"));
        var with = runner.Packet.Size;
        live.enabled = false;
        yield return null;
        yield return null;
        Assert.IsFalse(runner.Reads("Ball"), "no GameObject reads it");
        Assert.Less(runner.Packet.Size, with);
        Assert.IsTrue(live != null, "the component stays on its GameObject");
        Assert.IsTrue(ball.Showing, "which still shows its entity, placed by the match");
        var seat = live.seat;
        live.enabled = true;
        yield return null;
        yield return null;
        Assert.IsTrue(runner.Reads("Ball"), "enabled: read again");
        Assert.AreEqual(seat, live.seat);
        Assert.AreSame(live, ball.GetComponent<Tide.Game.Ball>());
    }
}
