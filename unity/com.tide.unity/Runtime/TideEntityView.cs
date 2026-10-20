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

        public bool Mine => Entity.Mine;

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
    }
}
