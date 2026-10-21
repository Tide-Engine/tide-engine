using NUnit.Framework;
using Tide.Editor;
using UnityEditor;
using UnityEngine;

// Tide's files in the Project window: a .tide file and a tide.packages import as their text with Tide's icons, and
// a folder whose tide.packages says `package Name` is a package's, which gets the badge.
public class IconTests
{
    const string Folder = "Assets/TideIconTestTemp";

    [SetUp]
    public void Make()
    {
        if (!AssetDatabase.IsValidFolder(Folder)) AssetDatabase.CreateFolder("Assets", "TideIconTestTemp");
        AssetDatabase.CreateFolder(Folder, "Kit");
        System.IO.File.WriteAllText(Folder + "/Kit/tide.packages", "# A package of the test's\npackage IconTest\n");
        System.IO.File.WriteAllText(Folder + "/Kit/kit.tide", "namespace IconTest;\n");
        System.IO.File.WriteAllText(Folder + "/notes.packages", "not tide's\n");
        AssetDatabase.Refresh();
    }

    [TearDown]
    public void Clean()
    {
        AssetDatabase.DeleteAsset(Folder);
    }

    [Test]
    public void TideFilesHaveTideIcons()
    {
        var source = AssetDatabase.LoadMainAssetAtPath(Folder + "/Kit/kit.tide") as TextAsset;
        Assert.IsNotNull(source, "a .tide file is its text");
        Assert.AreEqual("namespace IconTest;\n", source.text);
        Assert.AreEqual(typeof(TideSourceImporter), AssetDatabase.GetImporterType(Folder + "/Kit/kit.tide"));
        Assert.IsNotNull(AssetDatabase.GetCachedIcon(Folder + "/Kit/kit.tide"));

        Assert.IsInstanceOf<TextAsset>(AssetDatabase.LoadMainAssetAtPath(Folder + "/Kit/tide.packages"));
        Assert.IsNotNull(AssetDatabase.GetCachedIcon(Folder + "/Kit/tide.packages"));
        Assert.IsInstanceOf<TextAsset>(AssetDatabase.LoadMainAssetAtPath(Folder + "/notes.packages"));
        // Not the icon of plain text, which another .packages file has
        Assert.AreNotEqual(AssetDatabase.GetCachedIcon(Folder + "/notes.packages"), AssetDatabase.GetCachedIcon(Folder + "/Kit/kit.tide"));
        Assert.AreNotEqual(AssetDatabase.GetCachedIcon(Folder + "/notes.packages"), AssetDatabase.GetCachedIcon(Folder + "/Kit/tide.packages"));

        Assert.IsNotNull(AssetDatabase.LoadAssetAtPath<Texture2D>(TideIcons.BadgePath));
        Assert.IsTrue(TidePackageFolders.IsPackage(Folder + "/Kit"));
        Assert.IsFalse(TidePackageFolders.IsPackage(Folder));
    }
}
