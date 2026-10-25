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

    // A Unity prefab the game spawns, by its GUID: what a player build instantiates for an entity of it, filled in
    // by the editor as the scene builds.
    [Serializable]
    public sealed class TidePrefabLink
    {
        public string guid;
        public GameObject prefab;
    }

    // Makes and lets go of the GameObjects that show entities, in place of the runner's Instantiate and Destroy (or
    // its pool): a game's own pool, say. `prefab` is the Unity prefab an entity shows through; Release gets back what
    // Make gave for it.
    public interface ITideSpawner
    {
        GameObject Make(GameObject prefab);
        void Release(GameObject prefab, GameObject made);
    }

    // Runs the game: one Tide frame in each of Unity's, with Unity's input,
    // then shows the entities the frame's view packet has. An entity of a
    // Unity prefab or scene (made with TideEntity) shows through its own
    // GameObject: a scene's, or one of an instance of its prefab, made for its
    // prefab's root entity. Any other shows through a GameObject of the first
    // view rule that matches it.
    [DefaultExecutionOrder(-10000)]
    [DisallowMultipleComponent]
    [Icon(TideIcon.Tide)]
    public sealed unsafe class TideRunner : MonoBehaviour
    {
        [Tooltip("As the game's command line: --host [port], --join <code>, --connect <address>, --seed <n>, --data-dir <folder>, --stats")]
        public string arguments = "";

        [Tooltip("Another relay's address; empty for Tide's")]
        public string relay = "";

        [Tooltip("Draw what the game's Tide views draw over Unity's frame, with Tide's renderer: 2D, text, 3D with the camera's depth, render targets and the game's own shaders")]
        public bool drawTide = true;

        [Tooltip("Keep the GameObject of an entity that goes, inactive, for the next entity of its prefab: reset what Unity alone keeps on it (an Animator's state, particles) in OnDisable or OnEnable")]
        public bool pool;

        [Tooltip("The camera the game's views start with, which Draw.WorldToScreen and Tide's 3D draws go through: the main camera when none is set")]
        public Camera viewCamera;

        [Tooltip("The mixer group the game's Tide sound plays through: none for Unity's master")]
        public UnityEngine.Audio.AudioMixerGroup audioOutput;

        // Makes and lets go of the GameObjects of the prefabs entities show through, in place of Instantiate and
        // Destroy, or the pool: null for those
        public ITideSpawner Spawner { get; set; }

        public List<TideViewRule> views = new List<TideViewRule>();

        [HideInInspector] public List<TidePrefabLink> prefabs = new List<TidePrefabLink>();

        // A player's map of the Unity prefabs and scenes (unity.json), which the build put here: a web player can't
        // read files of its own
        [HideInInspector] public string map = "";

        public static TideRunner Current { get; private set; }

        // Each frame, once Unity's keyboard, mouse, gamepad and touches are read into what Tide's devices say: the
        // game's own input on top (an on-screen stick, a bot, a replay, a test), as a window's devices would say it.
        public delegate void InputEdit(ref TideEmbedInput input);
        public event InputEdit EditInput;
        TideRender render;
        TideAudio audio; // The game's sound, through Unity's (none on the web, where Tide's program plays it itself)
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

        // Which GameObject shows an entity of the project's prefabs and scenes
        public TideUnityMap Map { get; private set; } = new TideUnityMap();
        int sourceType = -1, sourceAsset = -1, sourceNode = -1; // UnityEngine.Source in the schema, and its fields' offsets
        readonly Dictionary<string, GameObject> prefabByGuid = new Dictionary<string, GameObject>();
        // The pool: GameObjects of entities that went, inactive under poolRoot, by the prefab they're of
        readonly Dictionary<GameObject, Stack<GameObject>> pooled = new Dictionary<GameObject, Stack<GameObject>>();
        Transform poolRoot;
        int makingRow = -1;

        // Play in the editor from a scene of Tide entities with no runner of its own (a level, opened alone): one is
        // made, which runs the match with the scenes open.
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        static void RunnerForOpenScenes()
        {
            if (!Application.isEditor || FindAnyObjectByType<TideRunner>() != null || FindAnyObjectByType<TideEntity>() == null) return;
            new GameObject("Tide").AddComponent<TideRunner>();
        }

        void OnEnable()
        {
            if (Current != null && Current != this)
            {
                // One at a time, as the game's state is the process's: a scene's own, for playing from it, gives way
                // when the running match loads that scene
                if (Application.isPlaying) Destroy(this);
                else enabled = false;
                return;
            }
            Current = this;
            // The match decides which of the project's scenes are loaded: this goes on whichever it unloads
            if (Application.isPlaying && transform.parent == null) DontDestroyOnLoad(gameObject);
            scenesOpen.Clear();
            scenesStarted.Clear();
            for (var i = 0; i < UnityEngine.SceneManagement.SceneManager.sceneCount; i++)
            {
                var scene = UnityEngine.SceneManagement.SceneManager.GetSceneAt(i);
                scenesOpen.Add(scene.path);
                scenesStarted.Add(scene);
            }
            openSent = false;
            sceneOf.Clear();
            sceneLoads.Clear();
            UnityEngine.SceneManagement.SceneManager.sceneLoaded += SceneLoaded;
            UnityEngine.SceneManagement.SceneManager.sceneUnloaded += SceneUnloaded;
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
#if !UNITY_WEBGL || UNITY_EDITOR
            if (audio == null)
            {
                var speaker = new GameObject("Tide Audio");
                speaker.transform.SetParent(transform, false);
                audio = speaker.AddComponent<TideAudio>();
            }
            audio.Attach(library, embed, audioOutput);
#endif
            if (drawTide && TideRender.Supported)
            {
                render = Ensure<TideRender>(gameObject);
                render.Runner = this;
                render.enabled = true;
            }
            else if (drawTide)
            {
                // A GPU Tide's renderer can't draw with: the 2D drawing's packet, drawn with Unity's GL
                var draw = Ensure<TideDraw>(gameObject);
                draw.Runner = this;
            }
        }

        // What the game's views drew this frame (tide_embed_draw's packet), valid until the next frame, or null.
        public byte* DrawPacket(out uint length)
        {
            length = 0;
            if (embed == IntPtr.Zero) return null;
            uint size = 0;
            var bytes = library.Draw(embed, &size);
            length = size;
            return bytes;
        }

        // The frame's 3D again, through another camera (an editor's scene view): tide_embed_render_view's commands,
        // valid until the next render, or null.
        public byte* RenderView(Matrix4x4* transform, Matrix4x4* projection, int width, int height, out uint size)
        {
            size = 0;
            if (embed == IntPtr.Zero) return null;
            uint got = 0;
            var commands = library.RenderView(embed, transform, projection, width, height, &got);
            size = got;
            return commands;
        }

        // What the embedded game takes (tide/embed.h), each with the values it's followed by: the first word it
        // doesn't take, or null. tide run's others (--players, --capture, --size...) are for its own window.
        public static string UnknownArguments(List<string> words)
        {
            for (var i = 1; i < words.Count; i++)
            {
                switch (words[i])
                {
                case "--stats":
                    break;
                case "--host":
                    if (i + 1 < words.Count && words[i + 1].Length > 0 && char.IsDigit(words[i + 1][0])) i++;
                    break;
                case "--join":
                case "--connect":
                case "--seed":
                case "--data-dir":
                    if (i + 1 >= words.Count) return words[i] + " with nothing after it";
                    i++;
                    break;
                default:
                    return words[i];
                }
            }
            return null;
        }

        IntPtr Create()
        {
            var words = new List<string> { "unity" };
            words.AddRange(arguments.Split((char[])null, StringSplitOptions.RemoveEmptyEntries));
            var unknown = UnknownArguments(words);
            if (unknown != null)
            {
                Debug.LogWarning($"Tide: the runner's Arguments have {unknown}, which the game doesn't take in Unity: it takes " +
                                 "--host [port], --join <code>, --connect <address>, --seed <n>, --data-dir <folder> and --stats", this);
            }
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
                    // A player's game is linked in by hand; the editor's embed loads each build tide makes
                    game = library.Game,
                    pack = library.Pack,
                    packSize = (UIntPtr)library.PackSize,
                    libraryDir = library.Game != IntPtr.Zero ? IntPtr.Zero : Str(TideLibrary.BuildFolder),
                };
                return library.Create(&desc);
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
            var bytes = library.Schema(embed, &size);
            Schema = bytes == null || size == 0 ? null : TideSchema.Parse(Encoding.UTF8.GetString(bytes, (int)size));
            schemaWarned = false;
            LoadMap();
        }

        void LoadMap()
        {
            Map = !Application.isEditor && !string.IsNullOrEmpty(map) ? TideUnityMap.Parse(map) : TideUnityMap.Load(TideLibrary.BuildFolder);
            sourceType = Schema?.TypeIndex("UnityEngine.Source") ?? -1;
            var t = sourceType >= 0 ? Schema.Type(sourceType) : null;
            sourceAsset = (int?)t?.Field("asset")?.offset ?? -1;
            sourceNode = (int?)t?.Field("node")?.offset ?? -1;
            if (sourceAsset < 0 || sourceNode < 0) sourceType = -1;
        }

        // The Unity prefab of `guid`: the scene's link to it, or in the editor, the asset itself.
        GameObject PrefabOf(string guid)
        {
            if (prefabByGuid.TryGetValue(guid, out var found) && found != null) return found;
            found = null;
            foreach (var link in prefabs)
            {
                if (link != null && link.guid == guid) found = link.prefab;
            }
#if UNITY_EDITOR
            if (found == null)
            {
                var path = UnityEditor.AssetDatabase.GUIDToAssetPath(guid);
                if (!string.IsNullOrEmpty(path)) found = UnityEditor.AssetDatabase.LoadAssetAtPath<GameObject>(path);
            }
#endif
            prefabByGuid[guid] = found;
            return found;
        }

        // The packet entity's UnityEngine.Source, if it has one.
        bool SourceOf(int row, out int asset, out int node)
        {
            asset = node = -1;
            if (sourceType < 0) return false;
            var r = RowOf(sourceType, row);
            if (r < 0) return false;
            var value = Packet.Row((uint)sourceType, r, out _);
            if (value == null) return false;
            asset = *(int*)(value + sourceAsset);
            node = *(int*)(value + sourceNode);
            return true;
        }

        void OnDisable()
        {
            UnityEngine.SceneManagement.SceneManager.sceneLoaded -= SceneLoaded;
            UnityEngine.SceneManagement.SceneManager.sceneUnloaded -= SceneUnloaded;
            foreach (var view in shown.Values)
            {
                if (view == null) continue;
                if (Spawner != null && view.MadeFrom != null) Spawner.Release(view.MadeFrom, view.gameObject);
                else if (!view.Authored) Destroy(view.gameObject);
            }
            shown.Clear();
            foreach (var free in pooled.Values)
            {
                foreach (var made in free)
                {
                    if (made != null) Destroy(made);
                }
            }
            pooled.Clear();
            if (poolRoot != null) Destroy(poolRoot.gameObject);
            if (render != null) render.enabled = false; // What it made was the embed's renderer's, which starts over
            if (audio != null) audio.Detach(); // Unity's audio thread reads the embed no more
            if (embed != IntPtr.Zero) library.Destroy(embed);
            embed = IntPtr.Zero;
            library = null;
            if (Current == this) Current = null;
        }

        // The camera Unity draws the scene with is the one the game's views start with, so Draw.WorldToScreen,
        // Tide's 3D draws and their depth agree with Unity's picture: `viewCamera`, else the main camera, perspective
        // or orthographic, its near and far planes too. With none, views start with Tide's own.
        Camera ViewCamera()
        {
            var seen = viewCamera != null ? viewCamera : Camera.main;
            if (seen == null)
            {
                library.Camera(embed, null, null);
                return null;
            }
            var t = seen.transform;
            var transform = Matrix4x4.TRS(t.position, t.rotation, Vector3.one);
            var projection = seen.projectionMatrix;
            library.Camera(embed, &transform, &projection);
            return seen;
        }

        void OnApplicationPause(bool paused)
        {
            if (embed != IntPtr.Zero) library.Background(embed, (byte)(paused ? 1 : 0));
        }

        void Update()
        {
            if (embed == IntPtr.Zero) return;
            TideInputReader.Read(ref input, 1f);
            EditInput?.Invoke(ref input);
            var seen = ViewCamera();
            // The screen the views draw for: the camera's pixels where Tide's renderer draws into it
            float width = Screen.width, height = Screen.height;
            if (render != null && seen != null)
            {
                width = seen.pixelWidth;
                height = seen.pixelHeight;
            }
            fixed (TideEmbedInput* p = &input)
            {
                library.Frame(embed, Time.unscaledDeltaTime, p, width, height, 1f);
            }
            if (render != null && render.enabled)
            {
                if (render.Fresh) library.RenderReset(embed); // Everything it draws with, made again
                uint drawn = 0;
                var commands = library.Render(embed, &drawn);
                render.Replay(commands, drawn, seen);
            }
            fixed (TideEmbedStatus* p = &status) library.Status(embed, p);
            if (audio != null) audio.Listen(status.microphone != 0);
            FollowCursor();
            uint size = 0;
            var bytes = library.View(embed, &size);
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
            if (Application.isEditor && (frames & 63) == 0 && Map.Stale(TideLibrary.BuildFolder)) LoadMap(); // The prefabs and scenes were built again
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
            for (var i = 0; i < count; i++) byRow[i] = null;
            FollowScenes(count);
            for (var i = 0; i < count; i++) ViewOf(i);
            gone.Clear();
            foreach (var pair in shown)
            {
                if (!pair.Value.Seen) gone.Add(pair.Key);
            }
            foreach (var id in gone)
            {
                var view = shown[id];
                shown.Remove(id);
                Release(view);
            }
            for (var i = 0; i < count; i++)
            {
                if (byRow[i] != null) Place(byRow[i], i);
            }
            Live(count);
            LiveSingletons(read);
            if (read) Hear(count);
        }

        int liveFrame;
        readonly List<TideComponent> liveScratch = new List<TideComponent>();

        // Each shown entity's components into its GameObject's TideComponents (Packages/com.tide.game's), one made
        // where it has none, before scripts' Update reads them; one whose component the entity no longer has goes. Only
        // with components generated from the schema of the build running, whose values they lay out.
        void Live(int count)
        {
            if (count == 0 || Schema == null || TideTypes.Hash != Schema.Hash) return;
            liveFrame++;
            foreach (var index in Schema.components)
            {
                var type = Schema.Type(index);
                var kind = TideTypes.Of(type?.name);
                if (kind == null || !typeof(TideComponent).IsAssignableFrom(kind)) continue;
                var n = Packet.Count((uint)index, out _);
                for (var r = 0; r < n; r++)
                {
                    var values = Packet.Row((uint)index, r, out var e);
                    if (values == null || (uint)e >= (uint)count || byRow[e] == null) continue;
                    // Its local fields: the entity's row of its component's local section
                    byte* locals = null;
                    if (type.local >= 0)
                    {
                        var localRow = RowOf(type.local, e);
                        if (localRow >= 0) locals = Packet.Row((uint)type.local, localRow, out _, true);
                    }
                    var made = byRow[e].gameObject;
                    var component = (TideComponent)(made.GetComponent(kind) ?? made.AddComponent(kind));
                    component.LiveFrame = liveFrame;
                    component.ReadLive(values, locals, Packet, this);
                }
            }
            for (var i = 0; i < count; i++)
            {
                if (byRow[i] == null) continue;
                byRow[i].GetComponents(liveScratch);
                foreach (var component in liveScratch)
                {
                    if (component.LiveFrame != liveFrame) Destroy(component);
                }
            }
        }

        readonly Dictionary<Type, TideSingleton> singletons = new Dictionary<Type, TideSingleton>();

        // The match's singletons into the TideSingletons the runner keeps (Get), as its components' go into theirs
        void LiveSingletons(bool read)
        {
            if (!read || !Packet.InMatch || Schema == null || TideTypes.Hash != Schema.Hash)
            {
                singletons.Clear();
                return;
            }
            foreach (var index in Schema.singletons)
            {
                var type = Schema.Type(index);
                var kind = TideTypes.Of(type?.name);
                if (kind == null || !typeof(TideSingleton).IsAssignableFrom(kind)) continue;
                var values = Packet.Singleton((uint)index, out var stride);
                if (values == null || stride != type.size) continue;
                var locals = type.local >= 0 ? Packet.Singleton((uint)type.local, out _, true) : null;
                if (!singletons.TryGetValue(kind, out var singleton))
                {
                    singleton = (TideSingleton)Activator.CreateInstance(kind);
                    singletons[kind] = singleton;
                }
                singleton.ReadLive(values, locals, Packet, this);
            }
        }

        // The match's singleton `T` this frame, as Tide's views see it: null outside a match.
        public T Get<T>() where T : TideSingleton
        {
            return singletons.TryGetValue(typeof(T), out var singleton) ? (T)singleton : null;
        }

        readonly Dictionary<Type, Delegate> hearing = new Dictionary<Type, Delegate>();
        readonly Dictionary<Type, Delegate> hearingVerified = new Dictionary<Type, Delegate>();

        // What hears the match's event `T` as this machine's local handlers would: each one, as soon as this machine
        // has it (as `local event(Hit hit)` does), which a correction can turn out not to have happened; or once the
        // server confirmed its tick (OnVerified, as `local event(verified Hit hit)`). Before scripts' Update, after the
        // frame's components are live. An entity's TideEntityView.On hears only those sent to it.
        public void On<T>(Action<T> heard) where T : TideEvent
        {
            Listen(hearing, heard, true);
        }

        public void OnVerified<T>(Action<T> heard) where T : TideEvent
        {
            Listen(hearingVerified, heard, true);
        }

        public void Off<T>(Action<T> heard) where T : TideEvent
        {
            Listen(hearing, heard, false);
        }

        public void OffVerified<T>(Action<T> heard) where T : TideEvent
        {
            Listen(hearingVerified, heard, false);
        }

        internal static void Listen<T>(Dictionary<Type, Delegate> table, Action<T> heard, bool on) where T : TideEvent
        {
            if (heard == null) return;
            table.TryGetValue(typeof(T), out var had);
            var now = on ? Delegate.Combine(had, heard) : Delegate.Remove(had, heard);
            if (now == null) table.Remove(typeof(T));
            else table[typeof(T)] = now;
        }

        internal static void Tell(Dictionary<Type, Delegate> table, TideEvent e, UnityEngine.Object context)
        {
            if (!table.TryGetValue(e.GetType(), out var heard)) return;
            foreach (var one in heard.GetInvocationList())
            {
                try
                {
                    one.DynamicInvoke(e);
                }
                catch (System.Reflection.TargetInvocationException thrown)
                {
                    Debug.LogException(thrown.InnerException ?? thrown, context);
                }
            }
        }

        // The packet's events to what hears them: the runner's, then the entity's view's
        void Hear(int count)
        {
            if (Packet.HeardCount == 0 || Schema == null || TideTypes.Hash != Schema.Hash) return;
            for (var i = 0; i < Packet.HeardCount; i++)
            {
                var record = Packet.Heard(i);
                var type = Schema.Type((int)record.Type);
                var kind = TideTypes.Of(type?.name);
                if (kind == null || !typeof(TideEvent).IsAssignableFrom(kind)) continue;
                var values = Packet.HeardValue(record, type.size);
                if (values == null) continue;
                var e = (TideEvent)Activator.CreateInstance(kind);
                e.Tick = record.Tick;
                e.Verified = record.Verified;
                e.TargetEntity = new TideEntityId { index = record.TargetIndex, generation = record.TargetGeneration };
                var view = record.HasTarget && record.TargetPlace < (uint)count ? byRow[record.TargetPlace] : null;
                e.Target = view != null ? view.gameObject : null;
                e.ReadLive(values, Packet, this);
                Tell(e.Verified ? hearingVerified : hearing, e, this);
                if (view != null) view.Hear(e);
            }
        }

        // The entity a GameObject shows this frame, for an entity field of a command: none for null or one that shows none.
        public TideEntityId EntityOf(GameObject shown)
        {
            if (shown == null) return default;
            var view = shown.GetComponent<TideEntityView>();
            return view != null && view.Runner == this && view.Row >= 0 ? (TideEntityId)view.Entity : default;
        }

        // The GameObject an entity shows through this frame, for an entity field of a TideComponent: null for none.
        public GameObject GameObjectOf(TideEntityId entity)
        {
            return !entity.IsNull && shown.TryGetValue(entity.Id, out var view) && view != null && view.Row >= 0 ? view.gameObject : null;
        }

        // The view of the packet's entity `row`, made or found the first time it's seen: by its UnityEngine.Source if it
        // has one, else by the view rules.
        TideEntityView ViewOf(int row)
        {
            if (byRow[row] != null) return byRow[row];
            var entity = Packet.Entity(row);
            if (shown.TryGetValue(entity.Id, out var view) && view != null && !Same(view, entity, row))
            {
                // A rollback gave the handle to another entity: the GameObject is the other one's
                shown.Remove(entity.Id);
                Release(view);
                view = null;
            }
            if (view == null)
            {
                if (makingRow == row) return null; // Its parents loop back to it
                var outer = makingRow;
                makingRow = row;
                view = Make(entity, row);
                makingRow = outer;
                if (view == null) return null;
                view.Runner = this;
                shown[entity.Id] = view;
            }
            view.Entity = entity;
            if (entity.Born != 0) view.Born = entity.Born;
            view.Row = row;
            view.Seen = true;
            byRow[row] = view;
            return view;
        }

        // Whether `view` shows the packet's entity `row`, the one it was made for: what it was spawned as, where both
        // are known, and its UnityEngine.Source are the same.
        bool Same(TideEntityView view, TideEntityRow entity, int row)
        {
            if (view.Born != 0 && entity.Born != 0 && view.Born != entity.Born) return false;
            SourceOf(row, out var asset, out var node);
            return asset == view.SourceAsset && node == view.SourceNode;
        }

        // Lets go of an entity's GameObject: a scene's, or one in a prefab's instance, hidden till its entity's back;
        // one made from a prefab, to the spawner, the pool or destroyed.
        void Release(TideEntityView view)
        {
            if (view == null) return; // Its instance went with its root's entity
            view.Row = -1;
            view.Runner = null;
            if (view.Authored)
            {
                view.gameObject.SetActive(false);
                return;
            }
            var from = view.MadeFrom;
            if (from != null && Spawner != null)
            {
                Spawner.Release(from, view.gameObject);
                return;
            }
            if (from != null && pool)
            {
                view.gameObject.SetActive(false);
                if (poolRoot == null)
                {
                    poolRoot = new GameObject("Tide pool").transform;
                    poolRoot.SetParent(transform, false);
                }
                view.transform.SetParent(poolRoot, false);
                if (!pooled.TryGetValue(from, out var free)) pooled[from] = free = new Stack<GameObject>();
                free.Push(view.gameObject);
                return;
            }
            Destroy(view.gameObject);
        }

        // A GameObject of `prefab` for an entity: the spawner's, one the pool kept, or a new one.
        GameObject MakeFrom(GameObject prefab, TideEntityRow entity)
        {
            GameObject made = null;
            if (Spawner != null) made = Spawner.Make(prefab);
            else if (pooled.TryGetValue(prefab, out var free))
            {
                while (made == null && free.Count > 0) made = free.Pop(); // One a scene's unloading took is null
                if (made != null)
                {
                    made.transform.SetParent(null, false);
                    UnityEngine.SceneManagement.SceneManager.MoveGameObjectToScene(made, UnityEngine.SceneManagement.SceneManager.GetActiveScene());
                    made.SetActive(true);
                }
            }
            if (made == null) made = Instantiate(prefab);
            made.name = $"{prefab.name} ({entity.Index})";
            return made;
        }

        TideEntityView Make(TideEntityRow entity, int row)
        {
            var view = MakeView(entity, row);
            if (view == null) return null;
            SourceOf(row, out view.SourceAsset, out view.SourceNode);
            view.Born = entity.Born;
            view.PlacedAt = -1;
            return view;
        }

        TideEntityView MakeView(TideEntityRow entity, int row)
        {
            if (!SourceOf(row, out var asset, out var node))
            {
                var prefab = RuleFor(entity, row);
                if (prefab == null) return null;
                var made = Ensure<TideEntityView>(MakeFrom(prefab, entity));
                made.Authored = false;
                made.MadeFrom = prefab;
                return made;
            }
            var a = Map.Of(asset);
            var path = a != null && (uint)node < (uint)a.nodes.Length ? a.nodes[node] : null;
            if (path == null) return null; // A build the map isn't of yet
            Transform found;
            if (a.scene)
            {
                // A scene's own GameObject, where the scene is open
                found = TideUnityMap.FindInScene(SceneOf(row, asset), path);
            }
            else if (node == 0)
            {
                // A prefab's root entity: an instance of it, with the GameObjects its other entities show through
                var prefab = PrefabOf(a.guid);
                if (prefab == null)
                {
                    Debug.LogWarning($"Tide: an entity of {a.path} came, which this build can't find: the scene links the prefabs the game spawns as it builds.", this);
                    return null;
                }
                var root = Ensure<TideEntityView>(MakeFrom(prefab, entity));
                root.Authored = false;
                root.MadeFrom = prefab;
                return root;
            }
            else
            {
                // Inside an instance of its prefab: the one made for the entity of its prefab's root above it
                found = null;
                for (var at = entity; at.HasParent && at.Parent < (uint)Packet.EntityCount;)
                {
                    var up = (int)at.Parent;
                    at = Packet.Entity(up);
                    if (!SourceOf(up, out var upAsset, out var upNode) || upAsset != asset || upNode != 0) continue;
                    var instance = ViewOf(up);
                    found = instance != null ? TideUnityMap.Find(instance.transform, path) : null;
                    break;
                }
            }
            if (found == null) return null;
            var view = Ensure<TideEntityView>(found.gameObject);
            view.Authored = true;
            if (!found.gameObject.activeSelf) found.gameObject.SetActive(true);
            return view;
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
        // A GameObject of Unity's scene or prefab instance stays where it is in Unity's hierarchy: its placement is
        // Tide's under its parent entity's, through the GameObjects between them. Its parent is placed first.
        void Place(TideEntityView view, int row)
        {
            if (view.PlacedAt == frames) return;
            view.PlacedAt = frames;
            var entity = view.Entity;
            var parent = entity.HasParent && entity.Parent < (uint)Packet.EntityCount ? byRow[entity.Parent] : null;
            if (parent != null) Place(parent, (int)entity.Parent);
            var under = parent != null ? parent.transform : null;
            if (!view.Authored && view.transform.parent != under) view.transform.SetParent(under, false);
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
            var at = new Vector3(p[0], p[1], p[2]);
            var turn = new Quaternion(q[0], q[1], q[2], q[3]);
            var size = new Vector3(s[0], s[1], s[2]);
            var moved = view.transform;
            if (moved.parent == under)
            {
                moved.localPosition = at;
                moved.localRotation = turn;
                moved.localScale = size;
                return;
            }
            if (under != null)
            {
                at = under.TransformPoint(at);
                turn = under.rotation * turn;
                size = Vector3.Scale(size, under.lossyScale);
            }
            moved.SetPositionAndRotation(at, turn);
            var between = moved.parent != null ? moved.parent.lossyScale : Vector3.one;
            moved.localScale = new Vector3(Divide(size.x, between.x), Divide(size.y, between.y), Divide(size.z, between.z));
        }

        // The project's scenes the match has (their entities' roots, UnityEngine.Source's node 0 of a scene's asset): each
        // root has its own instance of its Unity scene, one open as this started that no other root has, else one loaded
        // beside the others as it comes, unloaded as it goes. A scene the match loads twice is two. Build Settings' first
        // is one, which the match loads as Main starts (tide unity's UnityEngine.Begin).
        //
        // The match's scenes are Tide's to load (owner, 2026-10-22). One loaded by hand while this runs, in the editor or
        // by the game's C#, is local: nothing of it goes to the match, and no entity shows through it. One of the match's
        // that something else unloads is loaded again, as the match still has it.
        readonly Dictionary<ulong, int> sceneRoots = new Dictionary<ulong, int>(); // This frame's roots, and their assets
        readonly Dictionary<ulong, UnityEngine.SceneManagement.Scene> sceneOf = new Dictionary<ulong, UnityEngine.SceneManagement.Scene>();
        readonly List<(ulong root, string path)> sceneLoads = new List<(ulong, string)>(); // Begun, for roots, in order
        readonly HashSet<UnityEngine.SceneManagement.Scene> sceneUnloads = new HashSet<UnityEngine.SceneManagement.Scene>(); // Scenes this unloads
        readonly HashSet<UnityEngine.SceneManagement.Scene> scenesStarted = new HashSet<UnityEngine.SceneManagement.Scene>(); // Open as this started
        readonly List<ulong> scenesGone = new List<ulong>();
        bool openSent;
        readonly List<string> scenesOpen = new List<string>(); // What the editor has open as this started, till that's sent

        void FollowScenes(int count)
        {
            if (sourceType < 0) return;
            sceneRoots.Clear();
            for (var i = 0; i < count; i++)
            {
                if (!SourceOf(i, out var asset, out var node) || node != 0) continue;
                var a = Map.Of(asset);
                if (a != null && a.scene) sceneRoots[Packet.Entity(i).Id] = asset;
            }
            // What the match had and has no more
            scenesGone.Clear();
            foreach (var pair in sceneOf)
            {
                if (!sceneRoots.ContainsKey(pair.Key)) scenesGone.Add(pair.Key);
            }
            foreach (var root in scenesGone)
            {
                Unload(sceneOf[root]);
                sceneOf.Remove(root);
            }
            // What it has that's not shown yet
            foreach (var pair in sceneRoots)
            {
                if (sceneOf.ContainsKey(pair.Key) || Loading(pair.Key)) continue;
                var path = Map.Of(pair.Value).path;
                var open = Unclaimed(path);
                if (open.IsValid())
                {
                    sceneOf[pair.Key] = open;
                    continue;
                }
                if (LoadScene(path) != null) sceneLoads.Add((pair.Key, path));
            }
            OpenEditorScenes();
        }

        bool Loading(ulong root)
        {
            foreach (var load in sceneLoads)
            {
                if (load.root == root) return true;
            }
            return false;
        }

        // A loaded instance of the scene at `path`, open as this started, that no root has, or none.
        UnityEngine.SceneManagement.Scene Unclaimed(string path)
        {
            foreach (var scene in scenesStarted)
            {
                if (!scene.IsValid() || !scene.isLoaded || scene.path != path || sceneUnloads.Contains(scene)) continue;
                var claimed = false;
                foreach (var had in sceneOf.Values) claimed |= had == scene;
                if (!claimed) return scene;
            }
            return default;
        }

        void Unload(UnityEngine.SceneManagement.Scene scene)
        {
            scenesStarted.Remove(scene);
            if (!scene.IsValid() || !scene.isLoaded || UnityEngine.SceneManagement.SceneManager.sceneCount <= 1) return;
            sceneUnloads.Add(scene);
            UnityEngine.SceneManagement.SceneManager.UnloadSceneAsync(scene);
        }

        // The instance of its scene that shows the scene's entity `row` of `asset`: its root's, the entity up its
        // parents that's its scene's node 0; else the first of the match's instances of it. Never a scene loaded by hand.
        UnityEngine.SceneManagement.Scene SceneOf(int row, int asset)
        {
            for (var at = row; (uint)at < (uint)Packet.EntityCount;)
            {
                if (SourceOf(at, out var upAsset, out var upNode) && upAsset == asset && upNode == 0)
                {
                    return sceneOf.TryGetValue(Packet.Entity(at).Id, out var scene) ? scene : default;
                }
                var entity = Packet.Entity(at);
                if (!entity.HasParent) break;
                at = (int)entity.Parent;
            }
            var path = Map.Of(asset).path;
            foreach (var scene in sceneOf.Values)
            {
                if (scene.path == path) return scene;
            }
            return default;
        }

        // A scene loaded: one of this runner's for a root, which it shows (or unloads, if its root's gone meanwhile).
        // Any other is local, and left alone.
        void SceneLoaded(UnityEngine.SceneManagement.Scene scene, UnityEngine.SceneManagement.LoadSceneMode mode)
        {
            for (var i = 0; i < sceneLoads.Count; i++)
            {
                if (sceneLoads[i].path != scene.path) continue;
                var root = sceneLoads[i].root;
                sceneLoads.RemoveAt(i);
                if (sceneRoots.ContainsKey(root) && !sceneOf.ContainsKey(root)) sceneOf[root] = scene;
                else Unload(scene);
                return;
            }
        }

        // A scene unloaded: one this runner unloaded, or one of the match's another unloaded, which is loaded again.
        void SceneUnloaded(UnityEngine.SceneManagement.Scene scene)
        {
            scenesStarted.Remove(scene);
            if (sceneUnloads.Remove(scene)) return;
            foreach (var pair in sceneOf)
            {
                if (pair.Value != scene) continue;
                sceneOf.Remove(pair.Key);
                break;
            }
        }

        int AssetOfScene(string path)
        {
            for (var k = 0; k < Map.assets.Length; k++)
            {
                if (Map.assets[k].scene && Map.assets[k].path == path) return k;
            }
            return -1;
        }

        static AsyncOperation LoadScene(string path)
        {
            if (string.IsNullOrEmpty(path)) return null; // The editor's untitled scene, which has no file to load again
            var additive = new UnityEngine.SceneManagement.LoadSceneParameters(UnityEngine.SceneManagement.LoadSceneMode.Additive);
#if UNITY_EDITOR
            // In Play mode, any scene of the project, in Build Settings or not
            return UnityEditor.SceneManagement.EditorSceneManager.LoadSceneAsyncInPlayMode(path, additive);
#else
            var loading = UnityEngine.SceneManagement.SceneManager.LoadSceneAsync(path, additive);
            if (loading == null) Debug.LogError($"Tide: the match loaded {path}, which isn't in this build: add it to Build Settings.");
            return loading;
#endif
        }

        // In the editor, the match has the scenes the editor has open as Play starts, of those holding Tide entities,
        // rather than Build Settings' first: UnityEngine.OpenScene, which the match takes from its first player alone, the
        // server's as it started. Sent once, as this machine runs the match.
        void OpenEditorScenes()
        {
            if (openSent || !CanSendScenes()) return;
            openSent = true;
            var first = true;
            foreach (var path in scenesOpen)
            {
                var asset = AssetOfScene(path);
                if (asset < 0) continue;
                SendScene("UnityEngine.OpenScene", asset, first); // The first: these, and no others
                first = false;
            }
            scenesOpen.Clear();
        }

        bool CanSendScenes() => Application.isEditor && embed != IntPtr.Zero && status.server != 0 && State == TideSessionState.Connected;

        // UnityEngine.OpenScene for the scene `asset` (none for -1, or where this machine can't).
        void SendScene(string command, int asset, bool only)
        {
            if (asset < 0 || !CanSendScenes()) return;
            var type = Schema.TypeIndex(command);
            var t = Schema.Type(type);
            var assetField = t?.Field("asset");
            var onlyField = t?.Field("only");
            if (assetField == null || onlyField == null) return;
            var value = new byte[Math.Max(1, (int)t.size)];
            BitConverter.GetBytes(asset).CopyTo(value, (int)assetField.offset);
            value[onlyField.offset] = (byte)(only ? 1 : 0);
            fixed (byte* bytes = value) library.Command(embed, (uint)type, 0, 0, bytes, (uint)value.Length);
        }

        static T Ensure<T>(GameObject on) where T : Component
        {
            return on.TryGetComponent<T>(out var had) ? had : on.AddComponent<T>();
        }

        static float Divide(float a, float b)
        {
            return b != 0f ? a / b : a;
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
            if (text == null) return library.Request(embed, (uint)kind, null, port) != 0;
            var bytes = Encoding.UTF8.GetBytes(text + "\0");
            fixed (byte* p = bytes) return library.Request(embed, (uint)kind, p, port) != 0;
        }

        // A command, as local code's Send (Tide.Game's class of it): to the entity `target` shows, as local code's
        // entity.Send, or with none to the match. False for one that isn't the running build's, outside a match, text
        // that can't be sent, or one too big for a command.
        public bool Send(TideCommand command, GameObject target = null)
        {
            if (command == null || embed == IntPtr.Zero || Schema == null) return false;
            var type = Schema.TypeIndex(command.TideName);
            if (type < 0 || Schema.types[type].kind != "command") return false;
            var writer = new TideCommandWriter(this);
            command.Pack(writer);
            var bytes = writer.Bytes();
            if (bytes.Length < Schema.types[type].size) return false;
            var to = EntityOf(target);
            fixed (byte* p = bytes) return library.Command(embed, (uint)type, to.index, to.generation, p, (uint)bytes.Length) != 0;
        }

        // ...to a component's GameObject's entity
        public bool Send(TideCommand command, Component target)
        {
            return Send(command, target != null ? target.gameObject : null);
        }

        // A command's value as the schema lays it out: `T` is its Value (TideTypeAttribute names it). False for a type
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
            return library.Command(embed, (uint)type, index, generation, &value, (uint)sizeof(T)) != 0;
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
