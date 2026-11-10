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

    // Authors and shows a Tide entity. The runtime binding is absent until a match shows it here.
    // Makes the GameObject a Tide entity of its prefab or scene: `tide unity`
    // reads the prefabs and scenes holding these straight from their files
    // (Force Text serialization) and makes each a Tide prefab named after its
    // file, the GameObject's Transform Tide's, and its components these. Entity
    // fields name other entities as `@n`, `references[n]` being the GameObject
    // whose entity is meant.
    [DisallowMultipleComponent]
    [AddComponentMenu("Tide/Tide Entity")]
    [Icon(TideIcon.Entity)]
    public sealed class TideEntity : MonoBehaviour
    {
        public List<TideAuthoredComponent> components = new List<TideAuthoredComponent>();
        public List<GameObject> references = new List<GameObject>();

        public TideRunner Runner { get; internal set; }
        public TideEntityRow Entity { get; internal set; }
        internal int Row = -1; // Among the packet's entities, this frame; -1 once it's gone
        internal bool Seen;
        internal int PlacedAt = -1; // The frame it was last placed in
        // A GameObject of a scene, or inside a prefab's instance, that an entity of its UnityEngine.Source shows through:
        // hidden while its entity's gone, rather than destroyed, and placed where it is in Unity's hierarchy
        internal bool Authored;
        // What it was made for, which a rollback giving the handle to another entity changes: the entity's spawn
        // (0: not known), its UnityEngine.Source (-1: none), and the prefab it's an instance of (null: a scene's)
        internal uint Born;
        internal int SourceAsset = -1, SourceNode = -1;
        internal GameObject MadeFrom;

        public bool Mine => Entity.Mine;

        // Whether it shows an entity of this frame's packet (false once the entity's gone)
        public bool Showing => Runner != null && Row >= 0;

        // Whether the entity has `T`'s component this frame.
        public bool Has<T>() where T : unmanaged
        {
            return Runner != null && Row >= 0 && Runner.Find<T>(Row, out _);
        }

        // `T`'s component of the entity this frame, or false (and `value` zero) without one.
        public bool TryGet<T>(out T value) where T : unmanaged
        {
            value = default;
            return Runner != null && Row >= 0 && Runner.Find(Row, out value);
        }

        // ...or zero without one.
        public T Get<T>() where T : unmanaged
        {
            TryGet<T>(out var value);
            return value;
        }

        public string Text(uint handle)
        {
            return Runner != null ? Runner.Packet.Text(handle) : string.Empty;
        }

        readonly Dictionary<Type, TideHearing> hearing = new Dictionary<Type, TideHearing>();
        readonly Dictionary<Type, TideHearing> hearingVerified = new Dictionary<Type, TideHearing>();

        // What hears the match's event `T` sent to this entity, as TideRunner.On and OnVerified hear every one.
        public void On<T>(Action<T> heard) where T : TideEvent
        {
            TideHearing.Listen(hearing, heard, true);
        }

        public void OnVerified<T>(Action<T> heard) where T : TideEvent
        {
            TideHearing.Listen(hearingVerified, heard, true);
        }

        public void Off<T>(Action<T> heard) where T : TideEvent
        {
            TideHearing.Listen(hearing, heard, false);
        }

        public void OffVerified<T>(Action<T> heard) where T : TideEvent
        {
            TideHearing.Listen(hearingVerified, heard, false);
        }

        // Whether anything here hears events of `kind`, as predicted or once verified
        internal bool Hears(Type kind, bool verified)
        {
            return TideHearing.Hears(verified ? hearingVerified : hearing, kind);
        }

        internal void Hear(Type kind, TideEvent e)
        {
            TideHearing.Tell(e.Verified ? hearingVerified : hearing, kind, e, this);
        }

        // The GameObject's Tide components, which the runner writes its entity's values into each frame: found once as
        // it starts showing an entity, then kept, so no frame looks through the GameObject for them.
        TideComponent[] live = Array.Empty<TideComponent>();
        Type[] liveKinds = Array.Empty<Type>();
        internal int LiveCount;
        internal int LiveSeen;     // How many of them the entity had in frame LiveSeenAt
        internal int LiveSeenAt = -1;
        static readonly List<TideComponent> found = new List<TideComponent>();

        // Finds the Tide components the GameObject has now: those authored on it, or left by the entity it showed last.
        internal void CollectLive()
        {
            LiveCount = 0;
            GetComponents(found);
            foreach (var component in found) AddLive(component, component.GetType());
            found.Clear();
        }

        void AddLive(TideComponent component, Type kind)
        {
            if (LiveCount == live.Length)
            {
                Array.Resize(ref live, Math.Max(4, LiveCount * 2));
                Array.Resize(ref liveKinds, live.Length);
            }
            live[LiveCount] = component;
            liveKinds[LiveCount] = kind;
            LiveCount++;
        }

        // Its component of `kind`, added where it has none.
        internal TideComponent Live(Type kind)
        {
            for (var i = 0; i < LiveCount; i++)
            {
                if (liveKinds[i] != kind) continue;
                if (live[i] != null) return live[i];
                // Something else destroyed it: forgotten, and added again
                live[i] = live[LiveCount - 1];
                liveKinds[i] = liveKinds[LiveCount - 1];
                live[--LiveCount] = null;
                break;
            }
            // One a script added since is taken as it is: a GameObject has one of each
            var added = (TideComponent)(TryGetComponent(kind, out var had) ? had : gameObject.AddComponent(kind));
            if (added != null) AddLive(added, kind);
            return added;
        }

        // Destroys the components the entity didn't have in frame `frame`: it lost them, or never had what was authored.
        internal void DropStale(int frame)
        {
            for (var i = LiveCount - 1; i >= 0; i--)
            {
                if (live[i] != null && live[i].LiveFrame == frame) continue;
                if (live[i] != null) Destroy(live[i]);
                live[i] = live[LiveCount - 1];
                liveKinds[i] = liveKinds[LiveCount - 1];
                live[--LiveCount] = null;
            }
        }
    }
}
