using System.IO;
using Tide;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// Makes the sample's scene and ball prefab: Tools > Tide Sample > Make Scene, or
// Unity.exe -batchmode -projectPath . -executeMethod SampleSetup.Make -quit
public static class SampleSetup
{
    const string Folder = "Assets/Sample";

    [MenuItem("Tools/Tide Sample/Make Scene")]
    public static void Make()
    {
        Directory.CreateDirectory(Folder);
        var material = new Material(Shader.Find("Standard"));
        AssetDatabase.CreateAsset(material, Folder + "/Ball.mat");
        var floorMaterial = new Material(Shader.Find("Standard")) { color = new Color(0.18f, 0.19f, 0.22f) };
        AssetDatabase.CreateAsset(floorMaterial, Folder + "/Floor.mat");

        var sphere = GameObject.CreatePrimitive(PrimitiveType.Sphere);
        sphere.name = "Ball";
        Object.DestroyImmediate(sphere.GetComponent<Collider>());
        sphere.GetComponent<MeshRenderer>().sharedMaterial = material;
        sphere.AddComponent<TideEntityView>();
        sphere.AddComponent<BallTint>();
        var prefab = PrefabUtility.SaveAsPrefabAsset(sphere, Folder + "/Ball.prefab");
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
        var runner = new GameObject("Tide").AddComponent<TideRunner>();
        runner.views.Add(new TideViewRule { match = "Ball", prefab = prefab });
        EditorSceneManager.SaveScene(scene, Folder + "/Bounce.unity");
        EditorBuildSettings.scenes = new[] { new EditorBuildSettingsScene(Folder + "/Bounce.unity", true) };
        AssetDatabase.SaveAssets();
        Debug.Log("SampleSetup: made " + Folder + "/Bounce.unity");
    }
}
