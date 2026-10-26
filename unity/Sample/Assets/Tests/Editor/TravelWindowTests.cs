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
    }

    [Test]
    public void TheWindowIsInTheMenu()
    {
        var method = typeof(TideTimeTravelWindow).GetMethod("Open");
        var item = method.GetCustomAttributes(typeof(UnityEditor.MenuItem), false).Cast<UnityEditor.MenuItem>().First();
        Assert.AreEqual("Window/Tide/Time Travel", item.menuItem);
    }
}
