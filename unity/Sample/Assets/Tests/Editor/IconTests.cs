using System.Collections;
using System.IO;
using NUnit.Framework;
using Tide.Editor;
using UnityEditor;
using UnityEngine;
using UnityEngine.TestTools;

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
        File.WriteAllText(Folder + "/Kit/tide.packages", "# A package of the test's\npackage IconTest\n");
        File.WriteAllText(Folder + "/Kit/kit.tide", "namespace IconTest;\n");
        File.WriteAllText(Folder + "/notes.packages", "not tide's\n");
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

        // The importers' scripts load, as Unity finds each by its file's name: else the inspector says it failed to
        foreach (var path in new[] { Folder + "/Kit/kit.tide", Folder + "/Kit/tide.packages" })
        {
            var importer = AssetImporter.GetAtPath(path);
            Assert.IsNotNull(importer, path);
            var script = new SerializedObject(importer).FindProperty("m_Script").objectReferenceValue as MonoScript;
            Assert.IsNotNull(script, path);
            Assert.AreEqual(importer.GetType(), script.GetClass(), path);
        }

        Assert.IsNotNull(AssetDatabase.LoadAssetAtPath<Texture2D>(TideIcons.BadgePath));
        Assert.IsTrue(TidePackageFolders.IsPackage(Folder + "/Kit"));
        Assert.IsFalse(TidePackageFolders.IsPackage(Folder));
    }

    // A .tide file imported before Tide's importers were has Unity's DefaultImporter in its .meta, which Unity warned
    // of at every import: the .meta is written again as the importer's, keeping its GUID
    [UnityTest]
    public IEnumerator AnOldMetaIsWrittenAsTheImporters()
    {
        const string guid = "5c1d2e7a9b3f4a6c8d0e1f2a3b4c5d6e";
        var meta = Folder + "/old.tide.meta";
        File.WriteAllText(Folder + "/old.tide", "namespace Old;\n");
        File.WriteAllText(meta, "fileFormatVersion: 2\nguid: " + guid + "\nDefaultImporter:\n  externalObjects: {}\n" +
                                "  userData: \n  assetBundleName: \n  assetBundleVariant: \n");
        AssetDatabase.Refresh();
        for (var i = 0; i < 20 && !File.ReadAllText(meta).Contains("\nScriptedImporter:"); i++) yield return null;
        var text = File.ReadAllText(meta);
        StringAssert.Contains("\nScriptedImporter:", text);
        StringAssert.Contains("guid: " + guid, text);
        Assert.AreEqual(Folder + "/old.tide", AssetDatabase.GUIDToAssetPath(guid));
        Assert.AreEqual(typeof(TideSourceImporter), AssetImporter.GetAtPath(Folder + "/old.tide").GetType());
    }
}
