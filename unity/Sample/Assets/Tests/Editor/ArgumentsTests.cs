using System.Collections.Generic;
using NUnit.Framework;
using Tide;

// The runner's Arguments: what the embedded game takes, and the first word it doesn't, which the runner warns of
// rather than leaving out unsaid.
public class ArgumentsTests
{
    static string Unknown(params string[] words)
    {
        var all = new List<string> { "unity" };
        all.AddRange(words);
        return TideRunner.UnknownArguments(all);
    }

    [Test]
    public void TheRunnerSaysWhichArgumentTheGameDoesntTake()
    {
        Assert.IsNull(Unknown());
        Assert.IsNull(Unknown("--stats"));
        Assert.IsNull(Unknown("--host", "--stats"));
        Assert.IsNull(Unknown("--host", "7777", "--seed", "3"));
        Assert.IsNull(Unknown("--join", "ABCD", "--data-dir", "saves"));
        Assert.AreEqual("--players", Unknown("--stats", "--players", "2"));
        Assert.AreEqual("--connect with nothing after it", Unknown("--connect"));
        Assert.AreEqual("stats", Unknown("stats"));
    }
}
