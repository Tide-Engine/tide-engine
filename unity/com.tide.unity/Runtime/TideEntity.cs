using System;
using System.Collections.Generic;
using UnityEngine;

namespace Tide
{
    // A Tide component on a TideEntity: its type's Tide name (Combat.Health) and the
    // fields it sets, as a Tide literal's ("health = 10, color = Color(1, 0, 0)").
    // Fields it doesn't name keep their defaults. In a prefab instance, the fields
    // the instance sets go over its prefab's, field by field.
    [Serializable]
    public struct TideAuthoredComponent
    {
        public string type;
        public string values;
    }

    // Makes the GameObject a Tide entity of its prefab or scene: `tide unity`
    // reads the prefabs and scenes holding these straight from their files
    // (Force Text serialization) and makes each a Tide prefab named after its
    // file, the GameObject's Transform Tide's, and its components these. Entity
    // fields name other entities as `@n`, `references[n]` being the GameObject
    // whose entity is meant.
    [DisallowMultipleComponent]
    [AddComponentMenu("Tide/Tide Entity")]
    public sealed class TideEntity : MonoBehaviour
    {
        public List<TideAuthoredComponent> components = new List<TideAuthoredComponent>();
        public List<GameObject> references = new List<GameObject>();

        // The entity showing through this GameObject while a match runs, if any
        public TideEntityView View => GetComponent<TideEntityView>();
    }
}
