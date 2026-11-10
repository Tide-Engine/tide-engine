using System.Reflection;
using NUnit.Framework;
using Tide;
using UnityEditor;
using UnityEngine;

public class EntityBindingTests
{
    const BindingFlags Internal = BindingFlags.Instance | BindingFlags.NonPublic;

    [Test]
    public void AuthoredEntityHasTheRuntimeApiBeforeBinding()
    {
        var go = new GameObject("Authored");
        try
        {
            var entity = go.AddComponent<TideEntity>();
            entity.components.Add(new TideAuthoredComponent { type = "Ball", values = "seat = 2" });
            Assert.IsFalse(entity.Showing);
            Assert.IsFalse(entity.Has<int>());
            Assert.IsFalse(entity.TryGet<int>(out var value));
            Assert.AreEqual(0, value);
            Assert.AreEqual(0, entity.Get<int>());
            Assert.AreEqual("", entity.Text(0));
            using var serialized = new SerializedObject(entity);
            Assert.AreEqual(1, serialized.FindProperty("components").arraySize);
            Assert.IsNull(serialized.FindProperty("Row"), "the live binding is not authored state");
            Assert.IsNull(serialized.FindProperty("Runner"));
        }
        finally { Object.DestroyImmediate(go); }
    }

    [Test]
    public void ReleasingAnAuthoredBindingKeepsItsComponentAndSubscriptions()
    {
        var go = new GameObject("Authored");
        var host = new GameObject("Runner");
        host.SetActive(false);
        try
        {
            var runner = host.AddComponent<TideRunner>();
            var entity = go.AddComponent<TideEntity>();
            var heard = 0;
            System.Action<Tide.Game.Bounced> listener = _ => heard++;
            entity.On(listener); // Scripts may subscribe before the entity is bound.
            typeof(TideEntity).GetProperty("Runner").SetValue(entity, runner);
            typeof(TideEntity).GetField("Row", Internal).SetValue(entity, 0);
            typeof(TideEntity).GetField("Authored", Internal).SetValue(entity, true);
            Assert.IsTrue(entity.Showing);
            typeof(TideRunner).GetMethod("Release", Internal).Invoke(runner, new object[] { entity });
            Assert.IsFalse(entity.Showing);
            Assert.IsFalse(go.activeSelf);
            Assert.AreSame(entity, go.GetComponent<TideEntity>());
            Assert.AreEqual(1, go.GetComponents<TideEntity>().Length);
            var hear = typeof(TideEntity).GetMethod("Hear", Internal);
            hear.Invoke(entity, new object[] { typeof(Tide.Game.Bounced), new Tide.Game.Bounced() });
            Assert.AreEqual(1, heard);
            entity.Off(listener);
            hear.Invoke(entity, new object[] { typeof(Tide.Game.Bounced), new Tide.Game.Bounced() });
            Assert.AreEqual(1, heard);
        }
        finally
        {
            Object.DestroyImmediate(go);
            Object.DestroyImmediate(host);
        }
    }
}
