using System.Collections;
using System.IO;
using NUnit.Framework;
using Tide.Editor;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.TestTools;

// Play has a scene as the editor has it, saved or not: a change made and not saved is in what tide builds, once
// it's synced, and gone again once the scene's opened from its file.
public class UnsavedTests
{
    const string Scene = "Assets/Sample/Bounce.unity";
    const string Text = "Library/Tide/prefabs/" + Scene + ".tprefab"; // The Tide text tide read of it

    [UnityTest]
    public IEnumerator AnUnsavedChangeIsWhatTideBuilds()
    {
        yield return Running();
        var scene = EditorSceneManager.OpenScene(Scene, OpenSceneMode.Single);
        SerializedObject Spinner()
        {
            foreach (var root in scene.GetRootGameObjects())
            {
                if (root.name != "Pillars") continue;
                foreach (var part in root.transform.GetChild(0).GetComponents<MonoBehaviour>())
                {
                    if (part.GetType().Name == "Spinner") return new SerializedObject(part);
                }
            }
            return null;
        }
        var spinner = Spinner();
        Assert.IsNotNull(spinner, "Bounce's first Pillar has its Spinner");
        Assert.AreEqual(1.5f, spinner.FindProperty("speed").floatValue);
        yield return Synced(); // Whatever tide was building first

        try
        {
            spinner.FindProperty("speed").floatValue = 2.25f; // As the inspector sets it
            spinner.ApplyModifiedProperties();
            EditorSceneManager.MarkSceneDirty(scene);
            Assert.IsTrue(scene.isDirty, "the scene has a change it hasn't saved");
            Assert.IsTrue(TideBuildService.Sync(), "tide has the unsaved scene to build");
            Assert.IsTrue(File.Exists("Library/Tide/unsaved/" + Scene), "the scene's copy");
            StringAssert.Contains("speed: 2.25", File.ReadAllText("Library/Tide/unsaved/" + Scene), "with the change");
            yield return Synced();
            StringAssert.Contains("Spinner { speed = 2.25", File.ReadAllText(Text));
            Assert.IsTrue(scene.isDirty, "the scene isn't saved");
        }
        finally
        {
            scene = EditorSceneManager.OpenScene(Scene, OpenSceneMode.Single); // From its file, which takes the copy away
        }
        Assert.IsFalse(File.Exists("Library/Tide/unsaved/" + Scene));
        Assert.IsTrue(TideBuildService.Sync(), "tide has the scene as saved to build");
        yield return Synced();
        StringAssert.Contains("Spinner { speed = 1.5", File.ReadAllText(Text));
    }

    // A scene never saved is the game's too: its copy is the Tide scene Untitled, which goes with the scene.
    [UnityTest]
    public IEnumerator AnUntitledSceneIsTheGames()
    {
        const string untitled = "Library/Tide/unsaved/Untitled.unity";
        const string text = "Library/Tide/prefabs/Untitled.unity.tprefab";
        yield return Running();
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        try
        {
            var totem = new GameObject("Totem");
            totem.AddComponent<Tide.TideEntity>();
            var spinner = new SerializedObject(totem.AddComponent(System.Type.GetType("Tide.Game.Spinner, Tide.Game")));
            spinner.FindProperty("speed").floatValue = 4.5f;
            spinner.ApplyModifiedProperties();
            yield return Synced();
            Assert.IsTrue(File.Exists(untitled), "the untitled scene's copy");
            Assert.IsTrue(File.Exists(text), "which tide read");
            StringAssert.Contains("prefab Untitled", File.ReadAllText(text));
            StringAssert.Contains("Spinner { speed = 4.5", File.ReadAllText(text));
            StringAssert.Contains("\"path\": \"\", \"scene\": true", File.ReadAllText("Library/Tide/unity.json"), "Unity's path for it");
        }
        finally
        {
            EditorSceneManager.OpenScene(Scene, OpenSceneMode.Single);
        }
        Assert.IsFalse(File.Exists(untitled), "gone with the scene");
        yield return Synced();
        Assert.IsFalse(File.Exists(text));
    }

    static IEnumerator Running()
    {
        var until = EditorApplication.timeSinceStartup + 30;
        while (!TideBuildService.Running)
        {
            Assert.Less(EditorApplication.timeSinceStartup, until, "tide's building for the editor");
            yield return null;
        }
    }

    static IEnumerator Synced()
    {
        if (!TideBuildService.Sync() && TideBuildService.Synced) yield break;
        var until = EditorApplication.timeSinceStartup + 180;
        while (!TideBuildService.Synced)
        {
            Assert.Less(EditorApplication.timeSinceStartup, until, "tide synced in 3 minutes");
            yield return null;
        }
    }
}
