using System.Linq;
using NUnit.Framework;
using Tide;
using Tide.Editor;

// Window > Tide > Time Travel: what it says of where time travel is (its buttons are TideRunner's calls, which the
// PlayMode TravelTests drive), and that it opens outside Play saying what it's for.
public class TravelWindowTests
{
    static string Said(TideTravel t) => string.Join("\n", TideTimeTravelWindow.Describe(t));

    [Test]
    public void TheWindowSaysWhereTimeTravelIs()
    {
        StringAssert.Contains("off", Said(new TideTravel { state = TideTravelState.Off }));
        StringAssert.Contains("Recording: tick 90, from tick 0", Said(new TideTravel { state = TideTravelState.Live, last = 90 }));
        var paused = new TideTravel { state = TideTravelState.Paused, first = 0, last = 90, shown = 90, hash = 0xabc };
        StringAssert.Contains("Paused at tick 90", Said(paused));
        StringAssert.Contains("0000000000000abc", Said(paused));
        paused.shown = 40;
        paused.background = 1;
        StringAssert.Contains("Showing tick 40 of 0 to 90", Said(paused));
        StringAssert.Contains("goes on behind what's shown", Said(paused));
        // Another machine's match, followed
        StringAssert.Contains("Following another machine's match", Said(new TideTravel { state = TideTravelState.Live, first = 61, last = 90, followed = 1 }));
        StringAssert.Contains("61 to 90", Said(new TideTravel { state = TideTravelState.Live, first = 61, last = 90, followed = 1 }));
        var held = new TideTravel { state = TideTravelState.Paused, first = 61, last = 90, shown = 70, background = 1, followed = 1 };
        StringAssert.Contains("Showing tick 70 of 61 to 90", Said(held));
        StringAssert.Contains("another machine's", Said(held));
        // Another machine's match, recorded here from what it sends
        StringAssert.Contains("Recording another machine's match: tick 90, from tick 12", Said(new TideTravel { state = TideTravelState.Live, first = 12, last = 90, followed = 3 }));
        StringAssert.Contains("goes on behind what's shown", Said(new TideTravel { state = TideTravelState.Paused, first = 12, last = 90, shown = 40, background = 1, followed = 3 }));
        // A sharded match, followed: one this machine runs alone pauses as any
        StringAssert.Contains("Following a sharded match", Said(new TideTravel { state = TideTravelState.Live, first = 61, last = 90, followed = 2 }));
        var sharded = new TideTravel { state = TideTravelState.Paused, first = 61, last = 90, shown = 90, followed = 2 };
        StringAssert.Contains("Paused at tick 90", Said(sharded));
        StringAssert.Contains("home shard", Said(sharded));
        sharded.background = 1;
        StringAssert.Contains("Other machines are in this sharded match", Said(sharded));
        var replay = new TideTravel { state = TideTravelState.Replay, first = 0, last = 300, shown = 300, checkedTo = 300 };
        StringAssert.Contains("Every tick to 300", Said(replay));
        replay.differs = 213;
        replay.expected = 1;
        replay.got = 2;
        replay.otherBuild = 1;
        var said = Said(replay);
        StringAssert.Contains("Goes otherwise from tick 213", said);
        StringAssert.Contains("0000000000000001", said);
        StringAssert.Contains("another", said);
        replay.unreadable = 5;
        StringAssert.Contains("can't replay tick 5", Said(replay));
        // Another layout's recording, carried over: nothing is checked, and nothing says it was
        var carried = new TideTravel { state = TideTravelState.Replay, first = 0, last = 300, shown = 120, checkedTo = 0, otherBuild = 2 };
        StringAssert.Contains("laid out otherwise", Said(carried));
        StringAssert.Contains("carried over by name", Said(carried));
        StringAssert.DoesNotContain("made the recording's world", Said(carried));
        StringAssert.DoesNotContain("code was another", Said(carried));
        StringAssert.DoesNotContain("Not carried over", Said(carried));
        carried.fieldsReset = 2;
        carried.entitiesDropped = 5;
        StringAssert.Contains("Not carried over: 2 field(s)", Said(carried));
        StringAssert.Contains("5 entities", Said(carried));
    }

    [Test]
    public void TheWindowIsInTheMenu()
    {
        var method = typeof(TideTimeTravelWindow).GetMethod("Open");
        var item = method.GetCustomAttributes(typeof(UnityEditor.MenuItem), false).Cast<UnityEditor.MenuItem>().First();
        Assert.AreEqual("Window/Tide/Time Travel", item.menuItem);
    }
}
