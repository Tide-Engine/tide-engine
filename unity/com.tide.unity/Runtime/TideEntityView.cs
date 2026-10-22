using System;
using UnityEngine;

namespace Tide
{
    // Names the Tide type a C# struct mirrors (generated bindings carry it):
    // its fields laid out as the game's C lays the type out.
    [AttributeUsage(AttributeTargets.Struct)]
    public sealed class TideTypeAttribute : Attribute
    {
        public readonly string Name;
        public readonly bool Local; // The type of a match component's local fields

        public TideTypeAttribute(string name, bool local = false)
        {
            Name = name;
            Local = local;
        }
    }

    // On each GameObject that shows a Tide entity: which one, and its
    // components as this frame's view packet has them, blended between ticks.
    [DisallowMultipleComponent]
    public sealed class TideEntityView : MonoBehaviour
    {
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

        readonly System.Collections.Generic.Dictionary<Type, Delegate> hearing = new System.Collections.Generic.Dictionary<Type, Delegate>();
        readonly System.Collections.Generic.Dictionary<Type, Delegate> hearingVerified = new System.Collections.Generic.Dictionary<Type, Delegate>();

        // What hears the match's event `T` sent to this entity, as TideRunner.On and OnVerified hear every one.
        public void On<T>(Action<T> heard) where T : TideEvent
        {
            TideRunner.Listen(hearing, heard, true);
        }

        public void OnVerified<T>(Action<T> heard) where T : TideEvent
        {
            TideRunner.Listen(hearingVerified, heard, true);
        }

        public void Off<T>(Action<T> heard) where T : TideEvent
        {
            TideRunner.Listen(hearing, heard, false);
        }

        public void OffVerified<T>(Action<T> heard) where T : TideEvent
        {
            TideRunner.Listen(hearingVerified, heard, false);
        }

        internal void Hear(TideEvent e)
        {
            TideRunner.Tell(e.Verified ? hearingVerified : hearing, e, this);
        }
    }
}
