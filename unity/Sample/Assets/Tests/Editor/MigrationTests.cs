using System.Collections.Generic;
using System.Text.RegularExpressions;
using NUnit.Framework;
using Tide;
using Tide.Editor;
using UnityEditor;
using UnityEngine;
using UnityEngine.TestTools;

// Tools > Tide > Convert TideEntity Lists to Components, on a prefab and a prefab holding it: Base lists a Spinner and a
// Ball, its Arm an Owner; Holder's Base sets the Spinner's speed over Base's and adds an Owner. What can't be read stays
// listed: a Transform's values, and a field written as an expression.
public class MigrationTests
{
    const string Folder = "Assets/TideMigrationTestTemp";

    [SetUp]
    public void Make()
    {
        if (!AssetDatabase.IsValidFolder(Folder)) AssetDatabase.CreateFolder("Assets", "TideMigrationTestTemp");
        var made = new GameObject("Base");
        List(made, ("Spinner", "speed = 2, badge = 'x'"),
             ("Ball", "seat = 2, color = Color(1, 0, 0), velocity = float3(1, 2, 3), trail = Trail { length = 4, tint = Color(0, 1, 0) }"),
             ("Transform", "localPosition = float3(0, 1, 0)"));
        var arm = new GameObject("Arm");
        arm.transform.SetParent(made.transform);
        List(arm, ("Owner", "player = 7"));
        var odd = new GameObject("Odd");
        odd.transform.SetParent(made.transform);
        List(odd, ("Spinner", "speed = 2 * 3"));
        var source = PrefabUtility.SaveAsPrefabAsset(made, Folder + "/Base.prefab");
        Object.DestroyImmediate(made);

        var holder = new GameObject("Holder");
        List(holder, ("Ball", "seat = 9"));
        var nested = (GameObject)PrefabUtility.InstantiatePrefab(source);
        nested.transform.SetParent(holder.transform);
        using (var so = new SerializedObject(nested.GetComponent<TideEntity>()))
        {
            var list = so.FindProperty("components");
            list.GetArrayElementAtIndex(0).FindPropertyRelative("values").stringValue = "speed = 5";
            list.arraySize++;
            list.GetArrayElementAtIndex(list.arraySize - 1).FindPropertyRelative("type").stringValue = "Owner";
            list.GetArrayElementAtIndex(list.arraySize - 1).FindPropertyRelative("values").stringValue = "player = 1";
            so.ApplyModifiedPropertiesWithoutUndo();
        }
        PrefabUtility.SaveAsPrefabAsset(holder, Folder + "/Holder.prefab");
        Object.DestroyImmediate(holder);
    }

    [TearDown]
    public void Clean()
    {
        AssetDatabase.DeleteAsset(Folder);
    }

    static void List(GameObject on, params (string type, string values)[] components)
    {
        var entity = on.AddComponent<TideEntity>();
        foreach (var (type, values) in components) entity.components.Add(new TideAuthoredComponent { type = type, values = values });
    }

    static SerializedProperty Field(GameObject on, string component, string field)
    {
        var type = TideMigration.ComponentType(component);
        Assert.IsNotNull(type, component + " is the game's own component");
        var c = on.GetComponent(type);
        Assert.IsNotNull(c, $"{on.name} has {component}");
        return new SerializedObject(c).FindProperty(field);
    }

    static List<string> Listed(GameObject on)
    {
        var types = new List<string>();
        foreach (var c in on.GetComponent<TideEntity>().components) types.Add(c.type);
        return types;
    }

    [Test]
    public void ListedComponentsBecomeTheGamesOwnWithTheirFieldsAndOverrides()
    {
        Assume.That(TideMigration.ComponentType("Spinner"), Is.Not.Null, "the Sample's package has Spinner");
        LogAssert.Expect(LogType.Warning, new Regex("Spinner of Odd in .*Base.prefab: speed = 2 \\* 3"));
        var converted = TideMigration.ConvertFolder(Folder + "/", out var kept, out var error);
        Assert.IsNull(error);
        Assert.AreEqual(6, converted, "Base's Spinner, Ball and Owner; Holder's Ball, its Base's speed and Owner");
        Assert.AreEqual(1, kept.Count, string.Join("\n", kept));

        var source = AssetDatabase.LoadAssetAtPath<GameObject>(Folder + "/Base.prefab");
        Assert.AreEqual(2f, Field(source, "Spinner", "speed").floatValue);
        Assert.AreEqual(2, Field(source, "Ball", "seat").intValue);
        Assert.AreEqual(new Color(1, 0, 0, 1), Field(source, "Ball", "color").colorValue, "Color(r, g, b) is opaque");
        Assert.AreEqual(new Vector3(1, 2, 3), Field(source, "Ball", "velocity").vector3Value);
        Assert.AreEqual(4f, Field(source, "Ball", "trail.length").floatValue, "a struct's field");
        Assert.AreEqual(new Color(0, 1, 0, 1), Field(source, "Ball", "trail.tint").colorValue);
        Assert.AreEqual('x', Field(source, "Spinner", "badge").intValue, "a char, as its code point");
        Assert.AreEqual(new[] { "Transform" }, Listed(source), "Transform's values stay listed, over the GameObject's placement");
        Assert.AreEqual(7, Field(source.transform.Find("Arm").gameObject, "Owner", "player").intValue);
        Assert.AreEqual(new[] { "Spinner" }, Listed(source.transform.Find("Odd").gameObject), "an expression stays listed");
        Assert.IsNull(source.transform.Find("Odd").GetComponent(TideMigration.ComponentType("Spinner")));

        var holder = AssetDatabase.LoadAssetAtPath<GameObject>(Folder + "/Holder.prefab");
        Assert.AreEqual(9, Field(holder, "Ball", "seat").intValue);
        var nested = holder.transform.Find("Base").gameObject;
        var speed = Field(nested, "Spinner", "speed");
        Assert.AreEqual(5f, speed.floatValue, "the instance's speed over Base's");
        Assert.IsTrue(speed.prefabOverride, "as an override of Base's Spinner");
        Assert.AreEqual(2, Field(nested, "Ball", "seat").intValue, "Base's Ball, as it is");
        var owner = nested.GetComponent(TideMigration.ComponentType("Owner"));
        Assert.AreEqual(1, new SerializedObject(owner).FindProperty("player").intValue);
        Assert.IsTrue(PrefabUtility.IsAddedComponentOverride(owner), "the Owner it added, added to it");
        using var entity = new SerializedObject(nested.GetComponent<TideEntity>());
        Assert.IsFalse(entity.FindProperty("components").prefabOverride, "its list follows Base's again");
        Assert.AreEqual(new[] { "Transform" }, Listed(nested));

        // Again, it has nothing more to do
        Assert.AreEqual(0, TideMigration.ConvertFolder(Folder + "/", out _, out _), "nothing converts twice");
    }
}
