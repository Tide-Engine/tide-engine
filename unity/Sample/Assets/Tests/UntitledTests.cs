using System.Collections;
using NUnit.Framework;
using Tide;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.TestTools;

// The editor's untitled scene in Play. Its own fixture, which runs after BounceTests: the game's C# changes with the
// scene, mid-Play, after which Unity calls the OnAudioFilterRead of no component made in that Play, so a later test's
// runner would be silent.
public sealed class UntitledTests
{
    const string Bonus = "Assets/Sample/Bonus.unity";

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

    static int Open(string path)
    {
        var open = 0;
        for (var i = 0; i < SceneManager.sceneCount; i++) open += SceneManager.GetSceneAt(i).path == path ? 1 : 0;
        return open;
    }

#if UNITY_EDITOR
    // The editor's untitled scene, never saved, is the match's: tide builds it from the copy the editor writes at Play
    // (written here from Bonus's file, as the editor would of a scene holding what Bonus does), and the runner shows
    // its entities through the open scene with no path.
    [UnityTest]
    public IEnumerator TheEditorsUntitledSceneIsTheMatchs()
    {
        const string copy = "Library/Tide/unsaved/Untitled.unity";
        const string text = "Library/Tide/prefabs/Untitled.unity.tprefab";
        var service = System.Type.GetType("Tide.Editor.TideBuildService, Tide.Editor");
        var changed = service?.GetMethod("Changed");
        Assert.IsNotNull(changed, "the editor's build service");
        System.IO.File.Delete(text);
        System.IO.Directory.CreateDirectory("Library/Tide/unsaved");
        System.IO.File.Copy(Bonus, copy, true);
        try
        {
            changed.Invoke(null, null);
            yield return Until(() => System.IO.File.Exists(text) && System.IO.File.ReadAllText("Library/Tide/unity.json").Contains("\"path\": \"\""), 60f);
            Assert.IsTrue(System.IO.File.Exists(text), "tide built the untitled scene");
            yield return Until(() => false, 1f); // Its library written
            // A scene with no path, holding Bonus's GameObjects, as the editor's untitled scene does
            SceneManager.LoadScene(Bonus);
            yield return null;
            var untitled = SceneManager.CreateScene("Untitled");
            Assert.AreEqual("", untitled.path);
            var bonus = SceneManager.GetSceneByPath(Bonus);
            foreach (var root in bonus.GetRootGameObjects()) SceneManager.MoveGameObjectToScene(root, untitled);
            yield return SceneManager.UnloadSceneAsync(bonus);
            new GameObject("Tide").AddComponent<TideRunner>();
            TideEntityView totem = null;
            yield return Until(() =>
            {
                totem = null;
                foreach (var root in untitled.GetRootGameObjects())
                {
                    var at = root.transform.Find("Totem");
                    if (at != null) totem = at.GetComponent<TideEntityView>();
                }
                return totem != null && totem.Showing;
            }, 10f);
            Assert.IsTrue(totem != null && totem.Showing, "the untitled scene's Totem shows its entity");
            Assert.AreEqual(3f, totem.GetComponent<Tide.Game.Spinner>()?.speed, "with the scene's Spinner");
            Assert.AreEqual(0, Open(Bonus), "Bonus itself isn't loaded for it");
        }
        finally
        {
            System.IO.File.Delete(copy);
            changed.Invoke(null, null);
        }
        // The build without it, before the next test's match, which would have it swapped in
        yield return Until(() => !System.IO.File.Exists(text), 60f);
        yield return Until(() => false, 1f);
    }
#endif
}
