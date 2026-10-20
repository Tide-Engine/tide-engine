using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;

namespace Tide
{
    // Which GameObject shows an entity: the first rule whose `match` is the
    // entity's prefab (its Tide name) or a component it has.
    [Serializable]
    public sealed class TideViewRule
    {
        [Tooltip("A Tide prefab's name, or a component's (Combat.Health)")]
        public string match;
        public GameObject prefab;
    }

    // Runs the game: one Tide frame in each of Unity's, with Unity's input,
    // then shows the entities the frame's view packet has, each by a
    // GameObject of the first view rule that matches it.
    [DefaultExecutionOrder(-10000)]
    [DisallowMultipleComponent]
    public sealed unsafe class TideRunner : MonoBehaviour
    {
        [Tooltip("As the game's command line: --host, --join <code>, --connect <address>, --seed <n>")]
        public string arguments = "";

        [Tooltip("Another relay's address; empty for Tide's")]
        public string relay = "";

        public List<TideViewRule> views = new List<TideViewRule>();

        public static TideRunner Current { get; private set; }
        public TidePacket Packet { get; } = new TidePacket();
        public TideSchema Schema { get; private set; }
        public TideEmbedStatus Status => status;
        public bool Running => embed != IntPtr.Zero;

        public TideSessionState State => (TideSessionState)status.state;
        public string Room
        {
            get
            {
                fixed (byte* room = status.room) return Utf8(room, 64);
            }
        }

        // A kick's message, once this machine was kicked
        public string Message
        {
            get
            {
                fixed (byte* message = status.message) return Utf8(message, 256);
            }
        }

        TideLibrary library;
        IntPtr embed;
        TideEmbedInput input;
        TideEmbedStatus status;
        bool cursorLocked;
        bool schemaWarned;

        readonly Dictionary<ulong, TideEntityView> shown = new Dictionary<ulong, TideEntityView>();
        readonly List<ulong> gone = new List<ulong>();
        TideEntityView[] byRow = Array.Empty<TideEntityView>();

        // Where each type's rows are: rows[type][entity] is the entity's row in that type's section, or -1, as of
        // frame rowsAt[type]
        int[][] rows = Array.Empty<int[]>();
        int[] rowsAt = Array.Empty<int>();
        int frames;

        void OnEnable()
        {
            if (Current != null && Current != this)
            {
                Debug.LogError("Tide: one TideRunner at a time: the game's state is the process's.", this);
                enabled = false;
                return;
            }
            Current = this;
            library = TideLibrary.Open(TideLibrary.BuildFolder);
            if (library == null)
            {
                enabled = false;
                return;
            }
            embed = Create();
            if (embed == IntPtr.Zero)
            {
                Debug.LogError("Tide: the game didn't start: see the Console for what tide said.", this);
                enabled = false;
                return;
            }
            LoadSchema();
        }

        IntPtr Create()
        {
            var words = new List<string> { "unity" };
            words.AddRange(arguments.Split((char[])null, StringSplitOptions.RemoveEmptyEntries));
            var strings = new List<IntPtr>();
            IntPtr Str(string s)
            {
                var bytes = Encoding.UTF8.GetBytes(s + "\0");
                var p = Marshal.AllocHGlobal(bytes.Length);
                Marshal.Copy(bytes, 0, p, bytes.Length);
                strings.Add(p);
                return p;
            }
            var argv = Marshal.AllocHGlobal(IntPtr.Size * words.Count);
            try
            {
                for (var i = 0; i < words.Count; i++) Marshal.WriteIntPtr(argv, i * IntPtr.Size, Str(words[i]));
                var desc = new TideEmbedDesc
                {
                    argc = words.Count,
                    argv = argv,
                    gameName = Str(Application.productName),
                    relay = string.IsNullOrEmpty(relay) ? IntPtr.Zero : Str(relay),
                    libraryDir = Str(TideLibrary.BuildFolder),
                };
                return library.create(&desc);
            }
            finally
            {
                foreach (var p in strings) Marshal.FreeHGlobal(p);
                Marshal.FreeHGlobal(argv);
            }
        }

        void LoadSchema()
        {
            uint size = 0;
            var bytes = library.schema(embed, &size);
            Schema = bytes == null || size == 0 ? null : TideSchema.Parse(Encoding.UTF8.GetString(bytes, (int)size));
            schemaWarned = false;
        }

        void OnDisable()
        {
            foreach (var view in shown.Values)
            {
                if (view != null) Destroy(view.gameObject);
            }
            shown.Clear();
            if (embed != IntPtr.Zero) library.destroy(embed);
            embed = IntPtr.Zero;
            library = null;
            if (Current == this) Current = null;
        }

        void OnApplicationPause(bool paused)
        {
            if (embed != IntPtr.Zero) library.background(embed, (byte)(paused ? 1 : 0));
        }

        void Update()
        {
            if (embed == IntPtr.Zero) return;
            TideInputReader.Read(ref input, 1f);
            fixed (TideEmbedInput* p = &input)
            {
                library.frame(embed, Time.unscaledDeltaTime, p, Screen.width, Screen.height, 1f);
            }
            fixed (TideEmbedStatus* p = &status) library.status(embed, p);
            FollowCursor();
            uint size = 0;
            var bytes = library.view(embed, &size);
            frames++;
            if (!Packet.Read(bytes, size))
            {
                Show(false);
                return;
            }
            if (Schema == null || Packet.SchemaHash != Schema.Hash) LoadSchema(); // A new build of the game came in
            if (Schema == null || Packet.SchemaHash != Schema.Hash)
            {
                if (!schemaWarned) Debug.LogWarning("Tide: the view packet isn't the schema's: the game's build and its schema disagree.", this);
                schemaWarned = true;
                Show(false);
                return;
            }
            Show(true);
        }

        void FollowCursor()
        {
            var locked = status.cursorLocked != 0;
            if (locked == cursorLocked) return;
            cursorLocked = locked;
            Cursor.lockState = locked ? CursorLockMode.Locked : CursorLockMode.None;
            Cursor.visible = !locked;
        }

        // Shows the packet's entities, making and letting go of GameObjects as they come and go.
        void Show(bool read)
        {
            foreach (var view in shown.Values)
            {
                view.Seen = false;
                view.Row = -1;
            }
            var count = read ? Packet.EntityCount : 0;
            if (byRow.Length < count) byRow = new TideEntityView[Math.Max(count, byRow.Length * 2)];
            for (var i = 0; i < count; i++)
            {
                var entity = Packet.Entity(i);
                byRow[i] = null;
                if (!shown.TryGetValue(entity.Id, out var view))
                {
                    var prefab = RuleFor(entity, i);
                    if (prefab == null) continue;
                    var made = Instantiate(prefab);
                    made.name = $"{prefab.name} ({entity.Index})";
                    view = made.GetComponent<TideEntityView>() ?? made.AddComponent<TideEntityView>();
                    view.Runner = this;
                    shown[entity.Id] = view;
                }
                view.Entity = entity;
                view.Row = i;
                view.Seen = true;
                byRow[i] = view;
            }
            gone.Clear();
            foreach (var pair in shown)
            {
                if (!pair.Value.Seen) gone.Add(pair.Key);
            }
            foreach (var id in gone)
            {
                if (shown[id] != null) Destroy(shown[id].gameObject);
                shown.Remove(id);
            }
            for (var i = 0; i < count; i++)
            {
                if (byRow[i] != null) Place(byRow[i], i);
            }
        }

        GameObject RuleFor(TideEntityRow entity, int row)
        {
            var prefabName = entity.HasPrefab && entity.Prefab < Schema.prefabs.Length ? Schema.prefabs[entity.Prefab] : null;
            foreach (var rule in views)
            {
                if (rule.prefab == null || string.IsNullOrEmpty(rule.match)) continue;
                if (rule.match == prefabName) return rule.prefab;
                var type = Schema.TypeIndex(rule.match);
                if (type >= 0 && RowOf(type, row) >= 0) return rule.prefab;
            }
            return null;
        }

        // Tide's Transform, where the entity has one: its local placement under its parent's GameObject.
        void Place(TideEntityView view, int row)
        {
            var entity = view.Entity;
            var parent = entity.HasParent && entity.Parent < (uint)Packet.EntityCount ? byRow[entity.Parent] : null;
            var under = parent != null ? parent.transform : null;
            if (view.transform.parent != under) view.transform.SetParent(under, false);
            var type = Schema.TypeIndex("Transform");
            if (type < 0) return;
            var r = RowOf(type, row);
            if (r < 0) return;
            var value = Packet.Row((uint)type, r, out _);
            var t = Schema.Type(type);
            var position = t?.Field("localPosition");
            var rotation = t?.Field("localRotation");
            var scale = t?.Field("localScale");
            if (value == null || position == null || rotation == null || scale == null) return;
            var p = (float*)(value + position.offset);
            var q = (float*)(value + rotation.offset);
            var s = (float*)(value + scale.offset);
            // Tide is left-handed, y up, z forward, as Unity: the same numbers
            view.transform.localPosition = new Vector3(p[0], p[1], p[2]);
            view.transform.localRotation = new Quaternion(q[0], q[1], q[2], q[3]);
            view.transform.localScale = new Vector3(s[0], s[1], s[2]);
        }

        // The row of the packet's entity `entity` in type `type`'s section this frame, or -1.
        public int RowOf(int type, int entity)
        {
            if (Schema == null || (uint)type >= (uint)Schema.types.Length) return -1;
            var count = Packet.EntityCount;
            if ((uint)entity >= (uint)count) return -1;
            if (rows.Length < Schema.types.Length)
            {
                Array.Resize(ref rows, Schema.types.Length);
                Array.Resize(ref rowsAt, Schema.types.Length);
            }
            var table = rows[type];
            if (table == null || table.Length < count) table = rows[type] = new int[Math.Max(count, 16)];
            if (rowsAt[type] != frames)
            {
                rowsAt[type] = frames;
                var local = Schema.types[type].kind == "local";
                for (var i = 0; i < count; i++) table[i] = -1;
                var n = Packet.Count((uint)type, out _, local);
                for (var k = 0; k < n; k++)
                {
                    Packet.Row((uint)type, k, out var e, local);
                    if (e >= 0 && e < count) table[e] = k;
                }
            }
            return table[entity];
        }

        // `T`'s component of the packet's entity `entity` (TideTypeAttribute names its type).
        internal bool Find<T>(int entity, out T value) where T : unmanaged
        {
            value = default;
            var type = TypeOf<T>.Index(Schema);
            if (type < 0) return false;
            var row = RowOf(type, entity);
            if (row < 0) return false;
            var bytes = Packet.Row((uint)type, row, out _, TypeOf<T>.Local);
            var stride = Schema.types[type].size;
            if (bytes == null || stride != (uint)sizeof(T)) return false;
            value = *(T*)bytes;
            return true;
        }

        // A singleton's value this frame, or false outside a match.
        public bool TryGetSingleton<T>(out T value) where T : unmanaged
        {
            value = default;
            var type = TypeOf<T>.Index(Schema);
            if (type < 0) return false;
            var bytes = Packet.Singleton((uint)type, out var stride, TypeOf<T>.Local);
            if (bytes == null || stride != (uint)sizeof(T)) return false;
            value = *(T*)bytes;
            return true;
        }

        // Session calls, as local code's: false when one can't be asked now.
        public bool Request(TideRequest kind, string text = null, uint port = 0)
        {
            if (embed == IntPtr.Zero) return false;
            if (text == null) return library.request(embed, (uint)kind, null, port) != 0;
            var bytes = Encoding.UTF8.GetBytes(text + "\0");
            fixed (byte* p = bytes) return library.request(embed, (uint)kind, p, port) != 0;
        }

        // A command, as local code's Send: `T` is the command's type (TideTypeAttribute names it). False for a type
        // that's no command of the game's, or one holding text or lists.
        public bool Send<T>(T value) where T : unmanaged
        {
            return Send(value, 0, 0);
        }

        // ...sent to an entity, as local code's entity.Send: its handlers run for that entity.
        public bool Send<T>(T value, TideEntityRow target) where T : unmanaged
        {
            return Send(value, target.Index, target.Generation);
        }

        bool Send<T>(T value, uint index, uint generation) where T : unmanaged
        {
            if (embed == IntPtr.Zero || Schema == null) return false;
            var type = TypeOf<T>.Index(Schema);
            if (type < 0) return false;
            return library.command(embed, (uint)type, index, generation, &value, (uint)sizeof(T)) != 0;
        }

        static string Utf8(byte* bytes, int capacity)
        {
            var n = 0;
            while (n < capacity && bytes[n] != 0) n++;
            return Encoding.UTF8.GetString(bytes, n);
        }

        static class TypeOf<T> where T : unmanaged
        {
            static TideSchema seen;
            static int index = -1;
            public static readonly bool Local;
            static readonly string Name;

            static TypeOf()
            {
                var attribute = (TideTypeAttribute)Attribute.GetCustomAttribute(typeof(T), typeof(TideTypeAttribute));
                Name = attribute?.Name ?? typeof(T).Name;
                Local = attribute?.Local ?? false;
            }

            public static int Index(TideSchema schema)
            {
                if (schema == null) return -1;
                if (schema != seen)
                {
                    seen = schema;
                    var i = schema.TypeIndex(Name);
                    index = Local && i >= 0 ? schema.types[i].local : i;
                }
                return index;
            }
        }
    }
}
