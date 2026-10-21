using System.IO;
using NUnit.Framework;
using Tide.Editor;
using UnityEngine;

// What the .tide and tide.packages inspectors read: outline.json, as tide unity writes it, with its files and
// folders as the project names them, and the last build's messages found in their files.
public class OutlineTests
{
    [Test]
    public void TheOutlineNamesFilesAsTheProjectDoes()
    {
        var assets = Application.dataPath.Replace('\\', '/');
        var json = "{\"packages\": [\n" +
                   "  {\"name\": \"Physics\", \"folder\": \"" + assets + "/Kit/Physics/\", \"kind\": \"found\", \"line\": 0, \"source\": \"./Kit/Physics\"},\n" +
                   "  {\"name\": \"Rooms\", \"folder\": \"C:/tide/src/packages/rooms/\", \"kind\": \"builtin\", \"line\": 2, \"source\": \"rooms\"}\n" +
                   "],\n\"builtin\": [\"box2d\", \"rooms\"],\n\"declarations\": [\n" +
                   "  {\"kind\": \"component\", \"name\": \"Ball\", \"file\": \"" + assets + "/Tide/Bounce.tide\", \"line\": 82, \"column\": 11, " +
                   "\"fields\": [{\"name\": \"velocity\", \"type\": \"float3\"}, {\"name\": \"shown\", \"type\": \"float\", \"local\": true}]},\n" +
                   "  {\"kind\": \"system\", \"name\": \"Slower\", \"file\": \"" + assets + "/Tide/Bounce.tide\", \"line\": 57, \"column\": 8, " +
                   "\"perEntity\": true, \"access\": [{\"name\": \"Spin\", \"writes\": true}, {\"name\": \"Health\", \"every\": true}], " +
                   "\"without\": [\"scene UnityEngine.Scenes.Bonus\"], \"stage\": 2, " +
                   "\"after\": [{\"name\": \"Faster\", \"why\": \"both write Spin\"}], \"alongside\": [\"Tally\"]},\n" +
                   "  {\"kind\": \"scene\", \"name\": \"UnityEngine.Scenes.Bonus\", \"file\": \"Library/Tide/Unity.tide\", \"line\": 9, \"column\": 7}\n" +
                   "]}\n";
        var outline = TideOutline.Parse(json);
        Assert.IsNotNull(outline);
        Assert.AreEqual("Assets/Kit/Physics", outline.packages[0].asset);
        Assert.AreEqual("found", outline.packages[0].kind);
        Assert.AreEqual(new[] { "box2d", "rooms" }, outline.builtin);

        var bounce = outline.In("Assets/Tide/Bounce.tide");
        Assert.AreEqual(2, bounce.Count);
        Assert.AreEqual("Ball", bounce[0].name);
        Assert.IsTrue(bounce[0].fields[1].local);
        var slower = bounce[1];
        Assert.AreEqual("system", slower.kind);
        Assert.AreEqual(2, slower.stage);
        Assert.IsTrue(slower.access[0].writes);
        Assert.IsTrue(slower.access[1].every);
        Assert.AreEqual("both write Spin", slower.after[0].why);
        Assert.AreEqual(new[] { "scene UnityEngine.Scenes.Bonus" }, slower.without);
        Assert.AreEqual(1, outline.In("Library/Tide/Unity.tide").Count, "tide's own declarations keep their name");
    }

    [Test]
    public void AMessageIsFoundInItsFile()
    {
        Assert.IsTrue(TideBuildService.Locate("Tide/Bounce.tide:3:5: warning: 'spin' is never used", out var file, out var line, out var column));
        Assert.AreEqual("Assets/Tide/Bounce.tide", file);
        Assert.AreEqual(3, line);
        Assert.AreEqual(5, column);
        Assert.IsFalse(TideBuildService.Locate("tide: built the game for Unity", out _, out _, out _));
    }

    [Test]
    public void ItsPathIsBesideTheSchema()
    {
        Assert.AreEqual(Path.GetDirectoryName(Path.GetFullPath(TideSchemaWatcher.SchemaPath)), Path.GetDirectoryName(Path.GetFullPath(TideOutline.Path)));
    }
}
