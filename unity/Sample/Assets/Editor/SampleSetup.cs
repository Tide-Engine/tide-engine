using System.Collections.Generic;
using System.IO;
using Tide;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// Makes the sample's scene and ball prefab: Tools > Tide Sample > Make Scene, or
// Unity.exe -batchmode -projectPath . -executeMethod SampleSetup.Make -quit
// The ball (Roller.prefab) and the pillars in the scene have TideEntity: tide
// reads them as Tide's prefabs Roller and Bounce, which Bounce.tide spawns.
public static class SampleSetup
{
    const string Folder = "Assets/Sample";

    [MenuItem("Tools/Tide Sample/Make Scene")]
    public static void Make()
    {
        Directory.CreateDirectory(Folder);
        foreach (var old in new[] { "Ball.prefab", "Roller.prefab", "Bounce.unity", "Ball.mat", "Floor.mat", "Pillar.mat" })
        {
            AssetDatabase.DeleteAsset(Folder + "/" + old);
        }
        var material = new Material(Shader.Find("Standard"));
        AssetDatabase.CreateAsset(material, Folder + "/Ball.mat");
        var floorMaterial = new Material(Shader.Find("Standard")) { color = new Color(0.18f, 0.19f, 0.22f) };
        AssetDatabase.CreateAsset(floorMaterial, Folder + "/Floor.mat");
        var pillarMaterial = new Material(Shader.Find("Standard")) { color = new Color(0.85f, 0.55f, 0.25f) };
        AssetDatabase.CreateAsset(pillarMaterial, Folder + "/Pillar.mat");

        var sphere = GameObject.CreatePrimitive(PrimitiveType.Sphere);
        sphere.name = "Roller";
        Object.DestroyImmediate(sphere.GetComponent<Collider>());
        sphere.GetComponent<MeshRenderer>().sharedMaterial = material;
        Entity(sphere, ("Ball", ""));
        sphere.AddComponent<BallTint>();
        PrefabUtility.SaveAsPrefabAsset(sphere, Folder + "/Roller.prefab");
        Object.DestroyImmediate(sphere);

        var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var camera = new GameObject("Camera").AddComponent<Camera>();
        camera.tag = "MainCamera";
        camera.transform.position = new Vector3(0, 9, -10);
        camera.transform.rotation = Quaternion.Euler(42, 0, 0);
        camera.clearFlags = CameraClearFlags.SolidColor;
        camera.backgroundColor = new Color(0.086f, 0.086f, 0.094f);
        var light = new GameObject("Light").AddComponent<Light>();
        light.type = LightType.Directional;
        light.transform.rotation = Quaternion.Euler(50, -30, 0);
        var floor = GameObject.CreatePrimitive(PrimitiveType.Cube);
        floor.name = "Floor";
        floor.transform.localScale = new Vector3(11, 0.2f, 11);
        floor.transform.position = new Vector3(0, -0.1f, 0);
        floor.GetComponent<MeshRenderer>().sharedMaterial = floorMaterial;
        // Two spinning pillars, under a GameObject that's no entity: Tide places them as Unity has them
        var pillars = new GameObject("Pillars").transform;
        pillars.position = new Vector3(0, 0, 3.5f);
        Pillar(pillars, "Pillar", new Vector3(-3.5f, 0.75f, 0), pillarMaterial, 1.5f);
        Pillar(pillars, "Pillar", new Vector3(3.5f, 0.75f, 0), pillarMaterial, -0.75f);
        new GameObject("Tide").AddComponent<TideRunner>();
        EditorSceneManager.SaveScene(scene, Folder + "/Bounce.unity");
        EditorBuildSettings.scenes = new[] { new EditorBuildSettingsScene(Folder + "/Bounce.unity", true) };
        AssetDatabase.SaveAssets();
        Debug.Log("SampleSetup: made " + Folder + "/Bounce.unity");
    }

    // A WebGL player of the scene, into the folder after -tideOut, for web-smoke.mjs:
    // Unity.exe -batchmode -buildTarget WebGL -projectPath . -executeMethod SampleSetup.BuildWeb -tideOut <folder> -quit
    public static void BuildWeb()
    {
        var args = System.Environment.GetCommandLineArgs();
        var at = System.Array.IndexOf(args, "-tideOut");
        var output = at >= 0 && at + 1 < args.Length ? args[at + 1] : "Builds/Web";
        var report = BuildPipeline.BuildPlayer(new BuildPlayerOptions
        {
            scenes = new[] { Folder + "/Bounce.unity" },
            locationPathName = output,
            target = BuildTarget.WebGL,
        });
        Debug.Log($"SampleSetup: WebGL build {report.summary.result} in {output}");
        if (report.summary.result != UnityEditor.Build.Reporting.BuildResult.Succeeded) EditorApplication.Exit(1);
    }

    static void Pillar(Transform parent, string name, Vector3 at, Material material, float speed)
    {
        var pillar = GameObject.CreatePrimitive(PrimitiveType.Cube);
        pillar.name = name;
        Object.DestroyImmediate(pillar.GetComponent<Collider>());
        pillar.transform.SetParent(parent, false);
        pillar.transform.localPosition = at;
        pillar.transform.localScale = new Vector3(0.4f, 1.5f, 1.6f);
        pillar.GetComponent<MeshRenderer>().sharedMaterial = material;
        Entity(pillar, ("Spinner", "speed = " + speed.ToString(System.Globalization.CultureInfo.InvariantCulture)));
    }

    static void Entity(GameObject on, params (string type, string values)[] components)
    {
        var entity = on.AddComponent<TideEntity>();
        entity.components = new List<TideAuthoredComponent>();
        foreach (var (type, values) in components) entity.components.Add(new TideAuthoredComponent { type = type, values = values });
    }
}
