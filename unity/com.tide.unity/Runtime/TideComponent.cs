using System;
using System.Collections.Generic;
using System.Text;
using UnityEngine;

namespace Tide
{
    // A Tide component on a GameObject. The package generates one for each of the game's components, into
    // Packages/com.tide.game, its fields the component's. Outside Play they're what's authored, which tide bakes into
    // the game's prefabs and scenes. In Play they're the entity's own values this frame, as Tide's views see them,
    // written before scripts' Update: a script reads them as fields, its local fields too. Writing one changes nothing
    // in the match, which is the server's: send a command.
    [RequireComponent(typeof(TideEntity))]
    [Icon(TideIcon.Component)]
    public abstract class TideComponent : MonoBehaviour
    {
        // Its Tide component's name, as the schema has it: "Combat.Health"
        public abstract string TideName { get; }

        // The entity's value this frame: `values` its bytes in the frame's view packet, laid out as the schema says, and
        // `locals` its local fields' (null for a component with none)
        protected internal abstract unsafe void ReadLive(byte* values, byte* locals, TidePacket packet, TideRunner runner);

        internal int LiveFrame = -1; // The runner's frame that last wrote it: one the entity lost isn't
    }

    // The Inspector's icons of Tide's scripts (com.tide.unity's Editor/Icons): the generated components name Component
    public static class TideIcon
    {
        const string Folder = "Packages/com.tide.unity/Editor/Icons/";
        public const string Component = Folder + "TideComponent.png";
        public const string Entity = Folder + "TideEntity.png";
        public const string Tide = Folder + "TideFile.png";
    }

    // One of the match's singletons, as the package generates it: its fields this frame, its local fields too, as Tide's
    // views see them. TideRunner.Get<T>() gives the one the runner keeps.
    public abstract class TideSingleton
    {
        public abstract string TideName { get; }

        protected internal abstract unsafe void ReadLive(byte* values, byte* locals, TidePacket packet, TideRunner runner);
    }

    // One of the match's events, as this machine's local handlers hear it (TideRunner.On, OnVerified, and an entity's
    // TideEntityView.On): its fields, and what it was sent to.
    public abstract class TideEvent
    {
        public abstract string TideName { get; }

        // The tick that sent it
        public uint Tick { get; internal set; }

        // Heard once the server confirmed its tick, as `local event(verified Hit hit)` hears it; else as predicted,
        // which a correction can turn out not to have happened
        public bool Verified { get; internal set; }

        // The entity it was sent to: its GameObject this frame (null for the world, or an entity that isn't shown),
        // and its handle
        public GameObject Target { get; internal set; }
        public TideEntityId TargetEntity { get; internal set; }

        protected internal abstract unsafe void ReadLive(byte* values, TidePacket packet, TideRunner runner);
    }

    // One of the game's commands, as the package generates it: TideRunner.Send sends it, as local code's Send does.
    public abstract class TideCommand
    {
        public abstract string TideName { get; }

        // Its value, with its text and lists, into `writer`
        protected internal abstract void Pack(TideCommandWriter writer);
    }

    // A command's bytes as tide_embed_command takes them: its value, then its text and lists, a count, each one's
    // {offset, length}, then their bytes.
    public sealed unsafe class TideCommandWriter
    {
        readonly List<byte[]> entries = new List<byte[]>();
        readonly TideRunner runner;
        byte[] value = Array.Empty<byte>();

        internal TideCommandWriter(TideRunner runner)
        {
            this.runner = runner;
        }

        public TideText Text(string text)
        {
            if (string.IsNullOrEmpty(text)) return default;
            entries.Add(Encoding.UTF8.GetBytes(text));
            return new TideText { handle = (uint)entries.Count };
        }

        public TideList List<T>(List<T> items) where T : unmanaged
        {
            if (items == null || items.Count == 0) return default;
            var bytes = new byte[items.Count * sizeof(T)];
            fixed (byte* to = bytes)
            {
                for (var i = 0; i < items.Count; i++) ((T*)to)[i] = items[i];
            }
            entries.Add(bytes);
            return new TideList { handle = (uint)entries.Count };
        }

        // An entity field's handle: the entity a GameObject shows this frame, none for null or one that shows none
        public TideEntityId Entity(GameObject shown)
        {
            return runner != null ? runner.EntityOf(shown) : default;
        }

        public void Value<T>(T v) where T : unmanaged
        {
            value = new byte[sizeof(T)];
            fixed (byte* to = value) *(T*)to = v;
        }

        internal byte[] Bytes()
        {
            if (entries.Count == 0) return value;
            var tail = 4 + entries.Count * 8;
            var total = tail;
            foreach (var e in entries) total += e.Length;
            var bytes = new byte[value.Length + total];
            Buffer.BlockCopy(value, 0, bytes, 0, value.Length);
            fixed (byte* p = bytes)
            {
                var head = (uint*)(p + value.Length);
                head[0] = (uint)entries.Count;
                var offset = 0;
                for (var i = 0; i < entries.Count; i++)
                {
                    head[1 + i * 2] = (uint)offset;
                    head[2 + i * 2] = (uint)entries[i].Length;
                    Buffer.BlockCopy(entries[i], 0, bytes, value.Length + tail + offset, entries[i].Length);
                    offset += entries[i].Length;
                }
            }
            return bytes;
        }
    }

    // The generated components, singletons and events, by their Tide names, and the schema they were generated from:
    // the runner reads live values only into those of the build it runs.
    public static class TideTypes
    {
        static readonly Dictionary<string, Type> byName = new Dictionary<string, Type>();

        public static ulong Hash { get; private set; }

        public static void Register(string hash, string name, Type type)
        {
            Hash = ulong.TryParse(hash, System.Globalization.NumberStyles.HexNumber, null, out var h) ? h : 0;
            byName[name] = type;
        }

        public static Type Of(string name)
        {
            return name != null && byName.TryGetValue(name, out var type) ? type : null;
        }
    }
}
