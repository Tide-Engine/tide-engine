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
    //
    // The runner reads a component from the match once one of it is enabled on a GameObject, or a script asked
    // (TideRunner.Read, a TideEntity's Has, TryGet and Get): what nothing in Unity reads stays out of the game's
    // frame. While it's read, every shown entity that has it has its component, as authored or added by the runner.
    // Disabling one doesn't take the component from its entity: it says this GameObject doesn't read it, and while
    // the only ones there are disabled ones, the component isn't read.
    [RequireComponent(typeof(TideEntity))]
    [Icon(TideIcon.Component)]
    public abstract class TideComponent : MonoBehaviour
    {
        // Its Tide component's name, as the schema has it: "Combat.Health"
        public abstract string TideName { get; }

        // The entity's value this frame: `values` its bytes in the frame's view packet, laid out as the schema says, and
        // `locals` its local fields' (null for a component with none)
        protected internal abstract unsafe void ReadLive(byte* values, byte* locals, TidePacket packet, TideRunner runner);

        // What its fields hold of Unity's own, handed to Tide (TideAssets): generated for a component with a local
        // Texture field, which is set in Unity's inspector, or by a script, and drawn by Tide's views. Each frame its
        // entity is shown, after ReadLive.
        protected internal virtual void WriteAssets(TideAssets assets)
        {
        }

        internal int LiveFrame = -1; // The runner's frame that last wrote it: one the entity lost isn't
        internal int AssetsRun = -1; // The runner's game its assets were handed to (TideRunner.Live)...
        internal TideEntityId AssetsOf; // ...and for which entity
        internal TideEntity Beside;  // The TideEntity that keeps it among its GameObject's Tide components, once it does

        // What GameObjects say of each generated component, counted as Unity wakes, enables, disables and destroys
        // them: how many are enabled on active GameObjects, how many are disabled themselves (their own checkbox, not
        // their GameObject's), and whether one was ever enabled. A component is read once one was enabled, and from
        // then on, GameObjects of it coming and going or not, unless the only ones there are disabled ones.
        sealed class Said
        {
            public int on, off;
            public bool had;

            public bool Read => on > 0 || (had && off == 0);
        }

        static readonly Dictionary<Type, Said> saidOf = new Dictionary<Type, Said>();
        Said said;
        bool switchedOff; // Counted among its type's disabled ones

        // Moves as whether a type is read does
        internal static int EnabledVersion { get; private set; }

        // Whether GameObjects read generated component `kind`
        internal static bool AnyEnabled(Type kind)
        {
            return saidOf.TryGetValue(kind, out var said) && said.Read;
        }

        Said Mine()
        {
            if (said != null) return said;
            var kind = GetType();
            if (!saidOf.TryGetValue(kind, out said)) saidOf[kind] = said = new Said();
            return said;
        }

        // One disabled from the start, as a prefab can have it, is woken but never enabled
        protected virtual void Awake()
        {
            if (enabled) return;
            var counts = Mine();
            var read = counts.Read;
            switchedOff = true;
            counts.off++;
            if (counts.Read != read) EnabledVersion++;
        }

        protected virtual void OnEnable()
        {
            var counts = Mine();
            var read = counts.Read;
            counts.on++;
            counts.had = true;
            if (switchedOff)
            {
                switchedOff = false;
                counts.off--;
            }
            if (counts.Read != read) EnabledVersion++;
        }

        protected virtual void OnDisable()
        {
            var counts = Mine();
            var read = counts.Read;
            if (counts.on > 0) counts.on--;
            if (!enabled && !switchedOff) // Its own checkbox: a GameObject going, or put away, leaves it set
            {
                switchedOff = true;
                counts.off++;
            }
            if (counts.Read != read) EnabledVersion++;
        }

        protected virtual void OnDestroy()
        {
            if (!switchedOff) return;
            var counts = Mine();
            var read = counts.Read;
            switchedOff = false;
            counts.off--;
            if (counts.Read != read) EnabledVersion++;
        }

        // A runner starting (a new match's) reads what GameObjects have enabled now: what the last one's had, and has
        // no more, isn't read for that
        internal static void StartOver()
        {
            foreach (var counts in saidOf.Values) counts.had = counts.on > 0;
            EnabledVersion++;
        }

        // Play entered without a domain reload starts counting again
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        static void ForgetEnabled()
        {
            saidOf.Clear();
            EnabledVersion++;
        }
    }

    // A component that makes its entity a part of another's body (its package's unity.json, "part"): tide writes it on
    // the generated component, so where a part's shapes are is found with no look through the GameObject's fields.
    public interface ITidePart
    {
        // The GameObject of the body it's a part of, or null
        GameObject TidePartOf { get; }
    }

    // Unity's assets on their way into the game's local state: what a generated component's or singleton's
    // WriteAssets hands over, a field at a time. Local fields alone take them, as the match holds no rendering
    // resource, so nothing here reaches the match, a hash or another machine.
    public sealed class TideAssets
    {
        readonly TideRunner runner;
        internal int Type;           // The schema's type of the local fields written
        internal TideEntityId Entity; // Whose they are: none for a singleton's
        internal bool All;           // The entity, or the game, is another than last time: every field is handed again

        internal TideAssets(TideRunner runner)
        {
            this.runner = runner;
        }

        // The Texture field at `offset` of the local fields. Tide is handed `texture` when it's another than `given`,
        // the one handed last, which the caller keeps. A field left with none stays as Tide has it (its declared
        // default, or what the game's own code gave it), and one whose texture is taken away is left with none.
        public void Texture(uint offset, UnityEngine.Texture texture, ref UnityEngine.Texture given)
        {
            if (All) given = null;
            if (texture == null) // None, or one Unity destroyed
            {
                if (!ReferenceEquals(given, null)) runner.Give(Type, offset, Entity, null);
                given = null;
                return;
            }
            if (ReferenceEquals(texture, given)) return;
            // One Tide can't take (not 2D) is said once and counts as handed: it isn't tried again each frame
            runner.Give(Type, offset, Entity, texture);
            given = texture;
        }

        // The field at `offset` that's one of Unity's own objects to Tide (a UnityEngine.Mesh, Material or AudioClip
        // field): handed over as a Texture is, when it's another than the one handed last. Tide reads nothing of it,
        // and hands it back in a draw or a play.
        public void Object(uint offset, UnityEngine.Object item, ref UnityEngine.Object given)
        {
            if (All) given = null;
            if (item == null)
            {
                if (!ReferenceEquals(given, null)) runner.GiveObject(Type, offset, Entity, null);
                given = null;
                return;
            }
            if (ReferenceEquals(item, given)) return;
            runner.GiveObject(Type, offset, Entity, item);
            given = item;
        }
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

        // As a component's: a local Texture field's texture, which a script sets (a singleton has no inspector)
        protected internal virtual void WriteAssets(TideAssets assets)
        {
        }

        internal int AssetsRun = -1;
    }

    // One of the match's events, as this machine's local handlers hear it (TideRunner.On, OnVerified, and an entity's
    // TideEntity.On): its fields, and what it was sent to.
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
    // {offset, length}, then their bytes. The runner keeps one, so a command sent every frame makes nothing new.
    public sealed unsafe class TideCommandWriter
    {
        readonly TideRunner runner;
        byte[] value = new byte[64];
        int valueSize;
        byte[] held = new byte[256]; // The text and lists' bytes, one after another
        int heldSize;
        int[] entries = new int[8];  // Each one's offset and length in `held`
        int entryCount;
        byte[] packed = new byte[256];

        internal TideCommandWriter(TideRunner runner)
        {
            this.runner = runner;
        }

        internal void Reset()
        {
            valueSize = heldSize = entryCount = 0;
        }

        // `size` more bytes of `held`, as an entry: where they start.
        int Entry(int size)
        {
            if (held.Length < heldSize + size) Array.Resize(ref held, Math.Max(heldSize + size, held.Length * 2));
            if (entries.Length < (entryCount + 1) * 2) Array.Resize(ref entries, entries.Length * 2);
            entries[entryCount * 2] = heldSize;
            entries[entryCount * 2 + 1] = size;
            entryCount++;
            heldSize += size;
            return heldSize - size;
        }

        public TideText Text(string text)
        {
            if (string.IsNullOrEmpty(text)) return default;
            var size = Encoding.UTF8.GetByteCount(text);
            var at = Entry(size);
            Encoding.UTF8.GetBytes(text, 0, text.Length, held, at);
            return new TideText { handle = (uint)entryCount };
        }

        public TideList List<T>(List<T> items) where T : unmanaged
        {
            if (items == null || items.Count == 0) return default;
            var at = Entry(items.Count * sizeof(T));
            fixed (byte* to = &held[at])
            {
                for (var i = 0; i < items.Count; i++) ((T*)to)[i] = items[i];
            }
            return new TideList { handle = (uint)entryCount };
        }

        // An entity field's handle: the entity a GameObject shows this frame, none for null or one that shows none
        public TideEntityId Entity(GameObject shown)
        {
            return runner != null ? runner.EntityOf(shown) : default;
        }

        public void Value<T>(T v) where T : unmanaged
        {
            if (value.Length < sizeof(T)) value = new byte[Math.Max(sizeof(T), value.Length * 2)];
            fixed (byte* to = value) *(T*)to = v;
            valueSize = sizeof(T);
        }

        // The command's bytes, the first `size` of what's given: valid until the next command is written.
        internal byte[] Bytes(out int size)
        {
            size = valueSize;
            if (entryCount == 0) return value;
            var table = 4 + entryCount * 8;
            size = valueSize + table + heldSize;
            if (packed.Length < size) packed = new byte[Math.Max(size, packed.Length * 2)];
            Buffer.BlockCopy(value, 0, packed, 0, valueSize);
            fixed (byte* p = packed)
            {
                var head = (uint*)(p + valueSize);
                head[0] = (uint)entryCount;
                for (var i = 0; i < entryCount; i++)
                {
                    head[1 + i * 2] = (uint)entries[i * 2];
                    head[2 + i * 2] = (uint)entries[i * 2 + 1];
                }
            }
            Buffer.BlockCopy(held, 0, packed, valueSize + table, heldSize);
            return packed;
        }
    }

    // What hears one kind of the match's events: the handlers of TideRunner.On or a TideEntity's, each called with the
    // event as its own type, with no reflection. A handler added or taken away while they're told makes a new list, so
    // the telling goes through the one it started with.
    internal abstract class TideHearing
    {
        public abstract bool Empty { get; }
        public abstract int Count { get; }
        public abstract void Tell(TideEvent e, UnityEngine.Object context);

        // What `table` heard goes with its owner (a runner or a TideEntity destroyed with handlers still on it): its
        // events are heard by that much less
        public static void Forget(Dictionary<Type, TideHearing> table)
        {
            foreach (var pair in table)
            {
                if (!heardBy.TryGetValue(pair.Key, out var count) || count <= 0) continue;
                var left = Math.Max(0, count - pair.Value.Count);
                heardBy[pair.Key] = left;
                if (left == 0) Version++;
            }
            table.Clear();
        }

        // How many handlers hear each kind of event, the runner's and every TideEntity's: an event nothing hears isn't
        // read from the match
        static readonly Dictionary<Type, int> heardBy = new Dictionary<Type, int>();

        // Moves as a kind gets its first handler or loses its last
        public static int Version { get; private set; }

        public static bool Heard(Type kind)
        {
            return heardBy.TryGetValue(kind, out var count) && count > 0;
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        static void ForgetHeard()
        {
            heardBy.Clear();
            Version++;
        }

        // Adds or takes away `heard` in `table`'s handlers of `T`
        public static void Listen<T>(Dictionary<Type, TideHearing> table, Action<T> heard, bool on) where T : TideEvent
        {
            if (heard == null) return;
            if (!table.TryGetValue(typeof(T), out var had))
            {
                if (!on) return;
                table[typeof(T)] = had = new TideHearing<T>();
            }
            var typed = (TideHearing<T>)had;
            heardBy.TryGetValue(typeof(T), out var count);
            if (on)
            {
                typed.Add(heard);
                heardBy[typeof(T)] = count + 1;
                if (count == 0) Version++;
            }
            else if (typed.Remove(heard) && count > 0)
            {
                heardBy[typeof(T)] = count - 1;
                if (count == 1) Version++;
            }
            if (typed.Empty) table.Remove(typeof(T));
        }

        // Whether anything in `table` hears events of `kind`
        public static bool Hears(Dictionary<Type, TideHearing> table, Type kind)
        {
            return table.Count > 0 && table.ContainsKey(kind);
        }

        public static void Tell(Dictionary<Type, TideHearing> table, Type kind, TideEvent e, UnityEngine.Object context)
        {
            if (table.Count > 0 && table.TryGetValue(kind, out var hearing)) hearing.Tell(e, context);
        }
    }

    internal sealed class TideHearing<T> : TideHearing where T : TideEvent
    {
        Action<T>[] handlers = Array.Empty<Action<T>>();

        public override bool Empty => handlers.Length == 0;
        public override int Count => handlers.Length;

        public void Add(Action<T> heard)
        {
            var next = new Action<T>[handlers.Length + 1];
            Array.Copy(handlers, next, handlers.Length);
            next[handlers.Length] = heard;
            handlers = next;
        }

        // False where it wasn't among them
        public bool Remove(Action<T> heard)
        {
            var at = Array.LastIndexOf(handlers, heard); // The last one added, as taking a delegate away does
            if (at < 0) return false;
            var next = new Action<T>[handlers.Length - 1];
            Array.Copy(handlers, next, at);
            Array.Copy(handlers, at + 1, next, at, next.Length - at);
            handlers = next;
            return true;
        }

        public override void Tell(TideEvent e, UnityEngine.Object context)
        {
            var told = handlers;
            var typed = (T)e;
            for (var i = 0; i < told.Length; i++)
            {
                try
                {
                    told[i](typed);
                }
                catch (Exception thrown)
                {
                    Debug.LogException(thrown, context); // One handler's mistake doesn't keep the rest from hearing
                }
            }
        }
    }

    // The generated components, singletons and events, by their Tide names, and the schema they were generated from:
    // the runner reads live values only into those of the build it runs.
    public static class TideTypes
    {
        static readonly Dictionary<string, Type> byName = new Dictionary<string, Type>();
        static readonly Dictionary<Type, string> names = new Dictionary<Type, string>();
        static readonly Dictionary<string, Func<object>> makers = new Dictionary<string, Func<object>>();

        public static ulong Hash { get; private set; }

        // Counts each registration: what was found by name before is looked for again once it moves
        public static int Version { get; private set; }

        // `make` makes one of a singleton or an event, which the runner makes itself: none for a component, which
        // Unity makes.
        public static void Register(string hash, string name, Type type, Func<object> make = null)
        {
            Hash = ulong.TryParse(hash, System.Globalization.NumberStyles.HexNumber, null, out var h) ? h : 0;
            byName[name] = type;
            names[type] = name;
            if (make != null) makers[name] = make;
            else makers.Remove(name);
            Version++;
        }

        public static Type Of(string name)
        {
            return name != null && byName.TryGetValue(name, out var type) ? type : null;
        }

        // The Tide name of a generated class, or null for a type that isn't one
        public static string NameOf(Type type)
        {
            return type != null && names.TryGetValue(type, out var name) ? name : null;
        }

        // What makes one of the singleton or event named `name`, or null for one registered without
        internal static Func<object> Maker(string name)
        {
            return name != null && makers.TryGetValue(name, out var make) ? make : null;
        }
    }
}
