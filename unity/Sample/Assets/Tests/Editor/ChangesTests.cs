using NUnit.Framework;
using Tide.Editor;
using UnityEditor;

// What Unity imports that tide is told of (tide unity --notified): the game's files, and C only in a package's
// folder, as tide builds no other C of Assets.
public class ChangesTests
{
    const string Folder = "Assets/TideChangesTestTemp";

    [SetUp]
    public void Make()
    {
        if (!AssetDatabase.IsValidFolder(Folder)) AssetDatabase.CreateFolder("Assets", "TideChangesTestTemp");
        AssetDatabase.CreateFolder(Folder, "Kit");
        System.IO.File.WriteAllText(Folder + "/Kit/tide.packages", "package ChangesTest\n");
        AssetDatabase.Refresh();
    }

    [TearDown]
    public void Clean()
    {
        AssetDatabase.DeleteAsset(Folder);
    }

    [Test]
    public void TideHearsOfTheGamesFilesAlone()
    {
        Assert.IsTrue(TideChanges.Matters(new[] { "Assets/Tide/Bounce.tide" }));
        Assert.IsTrue(TideChanges.Matters(new[] { "Assets/tide.packages" }));
        Assert.IsTrue(TideChanges.Matters(new[] { "Assets/Sample/Roller.prefab" }));
        Assert.IsTrue(TideChanges.Matters(new[] { "Assets/Sample/Bounce.unity" }));
        Assert.IsTrue(TideChanges.Matters(new[] { Folder + "/Kit/physics.c" }), "a package's C");
        Assert.IsTrue(TideChanges.Matters(new[] { Folder + "/Kit/Deep/physics.h" }), "in a folder of a package's");
        Assert.IsFalse(TideChanges.Matters(new[] { "Assets/Plugins/native.c" }), "a plugin's C is Unity's");
        Assert.IsFalse(TideChanges.Matters(new[] { "Assets/Plugins/Managed.dll" }));
        Assert.IsFalse(TideChanges.Matters(new[] { "Assets/Art/brick.png", "Assets/Scripts/Ball.cs" }));
        Assert.IsFalse(TideChanges.Matters(new[] { "Packages/com.tide.game/schema.json" }));
        Assert.IsTrue(TideChanges.Matters(new[] { "Assets/Art/brick.png", "Assets/Tide/Bounce.tide" }));
    }
}
