using NUnit.Framework;
using Tide;
using Tide.Editor;
using UnityEditor;
using UnityEngine;

// The TideEntity inspector's Reset and Revert to Prefab, as its menus run them, through a prefab inside a prefab:
// Inner sets a Ball's seat and bounces, Outer's Inner sets seat again, and the scene's Outer sets seat and color.
public class EntityEditorTests
{
    const string Folder = "Assets/TideEditorTestTemp";
    GameObject placed;

    [SetUp]
    public void Make()
    {
        if (!AssetDatabase.IsValidFolder(Folder)) AssetDatabase.CreateFolder("Assets", "TideEditorTestTemp");
        var made = new GameObject("Inner");
        made.AddComponent<TideEntity>().components.Add(new TideAuthoredComponent { type = "Ball", values = "seat = 2, bounces = 1" });
        var inner = PrefabUtility.SaveAsPrefabAsset(made, Folder + "/Inner.prefab");
        Object.DestroyImmediate(made);

        var root = new GameObject("Outer");
        var nested = (GameObject)PrefabUtility.InstantiatePrefab(inner);
        nested.transform.SetParent(root.transform);
        SetValues(nested.GetComponent<TideEntity>(), "seat = 3");
        var outer = PrefabUtility.SaveAsPrefabAsset(root, Folder + "/Outer.prefab");
        Object.DestroyImmediate(root);

        placed = (GameObject)PrefabUtility.InstantiatePrefab(outer);
        SetValues(placed.GetComponentInChildren<TideEntity>(), "seat = 4, color = Color(1, 0, 0, 1)");
    }

    [TearDown]
    public void Clean()
    {
        if (placed) Object.DestroyImmediate(placed);
        AssetDatabase.DeleteAsset(Folder);
    }

    static void SetValues(TideEntity entity, string values)
    {
        using var so = new SerializedObject(entity);
        so.FindProperty("components").GetArrayElementAtIndex(0).FindPropertyRelative("values").stringValue = values;
        so.ApplyModifiedPropertiesWithoutUndo();
    }

    static string Field(TideEntityEditor editor, string name) => TideLiterals.Get(editor.Values(0), name);

    static bool Overrides(TideEntity entity)
    {
        using var so = new SerializedObject(entity);
        return so.FindProperty("components").GetArrayElementAtIndex(0).FindPropertyRelative("values").prefabOverride;
    }

    [Test]
    public void AFieldRevertsToTheNestedPrefabAndResetsToItsDefault()
    {
        Assume.That(TideEditorSchema.Current?.Type(TideEditorSchema.Current.TypeIndex("Ball")), Is.Not.Null, "the Sample's schema has Ball");
        var entity = placed.GetComponentInChildren<TideEntity>();
        var editor = (TideEntityEditor)UnityEditor.Editor.CreateEditor(entity);
        try
        {
            Assert.AreEqual("4", Field(editor, "seat"));
            Assert.AreEqual("1", Field(editor, "bounces"));

            editor.RevertFields(0, "seat"); // Back to what Outer's Inner says
            Assert.AreEqual("3", Field(editor, "seat"));
            Assert.AreEqual("Color(1, 0, 0, 1)", Field(editor, "color"));

            editor.ResetFields(0, "bounces"); // Inner sets it, so the instance says its default
            Assert.AreEqual("0", Field(editor, "bounces"));
            Assert.IsNull(editor.Resetting(0, "velocity"), "nothing sets velocity: it's its default already");

            editor.RevertFields(0); // The whole component follows its prefab again, with no override left
            Assert.IsFalse(Overrides(entity));
            Assert.AreEqual("3", Field(editor, "seat"));
            Assert.AreEqual("1", Field(editor, "bounces"));
            Assert.IsNull(Field(editor, "color"));
            Assert.IsNull(editor.Reverting(0));

            editor.ResetFields(0); // Each field the prefabs set, at its default
            Assert.AreEqual("0", Field(editor, "seat"));
            Assert.AreEqual("0", Field(editor, "bounces"));
            Assert.IsNull(Field(editor, "color"));
            Assert.IsNull(editor.Resetting(0));
        }
        finally
        {
            Object.DestroyImmediate(editor);
        }
    }

    [Test]
    public void ResetOnAPrefabItselfClearsItsValues()
    {
        Assume.That(TideEditorSchema.Current?.Type(TideEditorSchema.Current.TypeIndex("Ball")), Is.Not.Null, "the Sample's schema has Ball");
        var inner = AssetDatabase.LoadAssetAtPath<GameObject>(Folder + "/Inner.prefab").GetComponent<TideEntity>();
        var editor = (TideEntityEditor)UnityEditor.Editor.CreateEditor(inner);
        try
        {
            editor.ResetFields(0, "seat");
            Assert.AreEqual("bounces = 1", inner.components[0].values);
            editor.ResetFields(0);
            Assert.AreEqual("", inner.components[0].values);
            Assert.IsNull(editor.Reverting(0), "a prefab that's no instance has nothing to revert to");
        }
        finally
        {
            Object.DestroyImmediate(editor);
        }
    }
}
