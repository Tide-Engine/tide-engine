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

    // Where the runner keeps a match's ticks to pause, step, show again and replay (TideRunner.timeTravel)
    public enum TideTimeTravel
    {
        Off,              // Nowhere
        Editor,           // In the editor's Play
        EditorAndPlayers, // ...and in player builds, so a tester's recording (TideRunner.Recording) comes with a bug report
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

        [Tooltip("Keep the game running while its window isn't the one in front (Application.runInBackground), as a Tide game's own window does: a match waits for nobody, and one this machine runs would stop for every player in it")]
        public bool runInBackground = true;

        [Tooltip("The camera the game's views start with, which Draw.WorldToScreen and Tide's 3D draws go through: the main camera when none is set")]
        public Camera viewCamera;

        [Tooltip("The mixer group the game's Tide sound plays through: none for Unity's master")]
        public UnityEngine.Audio.AudioMixerGroup audioOutput;

        [Tooltip("Keep a match this machine runs to pause, step, scrub back and replay (Window > Tide > Time Travel): what went into every tick is recorded, and the worlds of the latest ticks are kept")]
        public TideTimeTravel timeTravel = TideTimeTravel.Editor;

        [Tooltip("Seconds of the latest ticks whose worlds are kept to show at once; ticks before them are made again from the recording's start")]
        [Min(0.1f)]
        public float keepSeconds = 10f;

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

        // The room's code, where the match is in one: the same string until it changes, so a UI can read it each frame
        public string Room
        {
            get
            {
                fixed (byte* room = status.room) return roomText.Of(room, 64);
            }
        }

        // The status's text as strings, made again only as the bytes change
        struct StatusText
        {
            byte[] bytes;
            int length;
            string text;

            public string Of(byte* from, int capacity)
            {
                var n = 0;
                while (n < capacity && from[n] != 0) n++;
                if (text != null && n == length)
                {
                    var same = true;
                    for (var i = 0; i < n && same; i++) same = bytes[i] == from[i];
                    if (same) return text;
                }
                bytes ??= new byte[capacity];
                for (var i = 0; i < n; i++) bytes[i] = from[i];
                length = n;
                return text = Encoding.UTF8.GetString(from, n);
            }
        }

        StatusText roomText, messageText;

        // The game asked to quit (Application.Quit). Without a handler, play mode ends in the editor and the
        // player quits (UnityEngine.Application.Quit); with one, the handler decides, and the game runs on until
        // the runner is destroyed.
        public event Action QuitAsked;

        // A kick's or a ban's message, once this machine was kicked or banned
        public string Message
        {
            get
            {
                fixed (byte* message = status.message) return messageText.Of(message, 256);
            }
        }

        TideLibrary library;
        IntPtr embed;
        TideEmbedInput input;
        TideEmbedStatus status;
        bool cursorLocked;
        bool schemaWarned;
        bool quitHeard;

        readonly Dictionary<TideEntityId, TideEntity> shown = new Dictionary<TideEntityId, TideEntity>();
        readonly List<TideEntityId> gone = new List<TideEntityId>();
        TideEntity[] byRow = Array.Empty<TideEntity>();

        // Where each type's rows are: rows[type][entity] is the entity's row in that type's section, or -1, as of
        // frame rowsAt[type]
        int[][] rows = Array.Empty<int[]>();
        int[] rowsAt = Array.Empty<int>();
        bool[] rowsLocal = Array.Empty<bool>(); // Whether each type is a component's local fields
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
            quitHeard = false;
            Said(); // Why it didn't start, or what its start said
            if (embed == IntPtr.Zero)
            {
                Debug.LogError("Tide: the game didn't start: see the Console for what tide said.", this);
                enabled = false;
                return;
            }
            LoadSchema();
            KeepTicks();
            if (runInBackground && Application.isPlaying) Application.runInBackground = true;
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
                library.RenderFormats(embed, TideRender.BlockFormats); // The pack's compressed images go as they are
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

        // A read back Tide's renderer asked for (TideRender's), for the game's GPU list reads: `size` bytes, or null
        // for one Unity couldn't make (tide_embed_render_read).
        public void RenderRead(uint token, byte* data, uint size)
        {
            if (embed == IntPtr.Zero) return;
            library.RenderRead(embed, token, data, size);
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
            // An Android app's own files are where Unity says: the game's saves go beside Unity's
            if (Application.platform == RuntimePlatform.Android && !words.Contains("--data-dir"))
            {
                words.Add("--data-dir");
                words.Add(Application.persistentDataPath);
            }
            // The embed keeps what it's given (its arguments, its save folder's path): they last as long as it does
            FreeCreated();
            var strings = created;
            IntPtr Str(string s)
            {
                var bytes = Encoding.UTF8.GetBytes(s + "\0");
                var p = Marshal.AllocHGlobal(bytes.Length);
                Marshal.Copy(bytes, 0, p, bytes.Length);
                strings.Add(p);
                return p;
            }
            var argv = Marshal.AllocHGlobal(IntPtr.Size * words.Count);
            strings.Add(argv);
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
        }

        readonly List<IntPtr> created = new List<IntPtr>(); // What the last Create gave the embed, freed once it's gone

        void FreeCreated()
        {
            foreach (var p in created) Marshal.FreeHGlobal(p);
            created.Clear();
        }

        void LoadSchema()
        {
            uint size = 0;
            var bytes = library.Schema(embed, &size);
            Schema = bytes == null || size == 0 ? null : TideSchema.Parse(Encoding.UTF8.GetString(bytes, (int)size));
            schemaWarned = false;
            bound = null;
            rows = Array.Empty<int[]>(); // By the schema's types
            rowsAt = Array.Empty<int>();
            rowsLocal = Array.Empty<bool>();
            LoadMap();
        }

        // What each frame reads of the schema, found once for it rather than by name each frame: which of its
        // components, singletons and events the game's C# has classes of (TideTypes), and where Tide's Transform is.
        struct LiveComponent
        {
            public int type, local; // The schema's type, and its local fields' (-1 for none)
            public uint size;
            public Type kind;
        }

        struct LiveSingleton
        {
            public int type, local;
            public uint size;
            public Type kind;
            public Func<object> make;
        }

        struct LiveEvent
        {
            public Type kind; // null for a type that's no event of the C#'s
            public uint size;
            public Func<object> make;
        }

        TideSchema bound;   // The schema these are of...
        int boundTypes = -1; // ...and TideTypes.Version
        bool live;           // The C# is this build's: its classes lay out what the packet has
        LiveComponent[] liveComponents = Array.Empty<LiveComponent>();
        LiveSingleton[] liveSingletons = Array.Empty<LiveSingleton>();
        LiveEvent[] liveEvents = Array.Empty<LiveEvent>();
        int transformType = -1, transformPosition, transformRotation, transformScale;

        void Bind()
        {
            if (bound == Schema && boundTypes == TideTypes.Version) return;
            bound = Schema;
            boundTypes = TideTypes.Version;
            live = Schema != null && TideTypes.Hash == Schema.Hash;
            singletons.Clear();
            transformType = -1;
            liveComponents = Array.Empty<LiveComponent>();
            liveSingletons = Array.Empty<LiveSingleton>();
            liveEvents = Array.Empty<LiveEvent>();
            if (Schema == null) return;
            var placed = Schema.TypeIndex("Transform");
            var t = Schema.Type(placed);
            var position = t?.Field("localPosition");
            var rotation = t?.Field("localRotation");
            var scale = t?.Field("localScale");
            if (position != null && rotation != null && scale != null && position.offset + 12 <= t.size && rotation.offset + 16 <= t.size
                && scale.offset + 12 <= t.size)
            {
                transformType = placed;
                transformPosition = (int)position.offset;
                transformRotation = (int)rotation.offset;
                transformScale = (int)scale.offset;
            }
            if (!live) return;
            var components = new List<LiveComponent>();
            foreach (var index in Schema.components)
            {
                var type = Schema.Type(index);
                var kind = TideTypes.Of(type?.name);
                if (kind == null || !typeof(TideComponent).IsAssignableFrom(kind)) continue;
                components.Add(new LiveComponent { type = index, local = type.local, size = type.size, kind = kind });
            }
            liveComponents = components.ToArray();
            var values = new List<LiveSingleton>();
            foreach (var index in Schema.singletons)
            {
                var type = Schema.Type(index);
                var kind = TideTypes.Of(type?.name);
                if (kind == null || !typeof(TideSingleton).IsAssignableFrom(kind)) continue;
                values.Add(new LiveSingleton { type = index, local = type.local, size = type.size, kind = kind, make = TideTypes.Maker(type.name) });
            }
            liveSingletons = values.ToArray();
            liveEvents = new LiveEvent[Schema.types.Length];
            for (var i = 0; i < Schema.types.Length; i++)
            {
                var type = Schema.types[i];
                var kind = TideTypes.Of(type.name);
                if (kind == null || !typeof(TideEvent).IsAssignableFrom(kind)) continue;
                liveEvents[i] = new LiveEvent { kind = kind, size = type.size, make = TideTypes.Maker(type.name) };
            }
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
            FreeCreated();
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
            Frame(Time.unscaledDeltaTime);
        }

        // What the game and its host said since the last frame (tide_embed_log) into Unity's Console: the game's
        // Debug.Log lines as logs, and what went wrong on the host's side (a build that didn't load) as errors. With no
        // C# stack, which would be the runner's own.
        void Said()
        {
            if (library == null) return;
            uint size = 0;
            var records = library.Log(embed, &size);
            for (uint at = 0; records != null && at + 8 <= size;)
            {
                var kind = *(uint*)(records + at);
                var length = *(uint*)(records + at + 4);
                if (length > size - at - 8) break;
                var text = Encoding.UTF8.GetString(records + at + 8, (int)length);
                Debug.LogFormat(kind == 2 ? LogType.Error : LogType.Log, LogOption.NoStacktrace, this, "{0}", text);
                at += 8 + ((length + 3) & ~3u);
            }
        }

        // The runner's part of a frame in Unity's Profiler: the game's own frame (its session, ticks and views), its
        // drawing read for Unity's cameras, and the frame's entities shown through their GameObjects
        static readonly Unity.Profiling.ProfilerMarker frameMarker = new Unity.Profiling.ProfilerMarker("Tide.Frame");
        static readonly Unity.Profiling.ProfilerMarker renderMarker = new Unity.Profiling.ProfilerMarker("Tide.Render");
        static readonly Unity.Profiling.ProfilerMarker showMarker = new Unity.Profiling.ProfilerMarker("Tide.Show");

        // One frame of the game, `seconds` after the last: Unity's Update calls it.
        internal void Frame(float seconds)
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
            frameMarker.Begin();
            fixed (TideEmbedInput* p = &input)
            {
                library.Frame(embed, seconds, p, width, height, 1f);
            }
            frameMarker.End();
            Said();
            if (render != null && render.enabled)
            {
                renderMarker.Begin();
                if (render.Fresh) library.RenderReset(embed); // Everything it draws with, made again
                uint drawn = 0;
                var commands = library.Render(embed, &drawn);
                render.Replay(commands, drawn, seen);
                renderMarker.End();
            }
            fixed (TideEmbedStatus* p = &status) library.Status(embed, p);
            fixed (TideTravel* p = &travel) library.Travel(embed, p);
            if (audio != null) audio.Listen(status.microphone != 0);
            FollowCursor();
            FollowQuit();
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
            Bind();
            Show(true);
        }

        void FollowQuit()
        {
            if (status.quit == 0 || quitHeard) return;
            quitHeard = true;
            if (QuitAsked != null)
            {
                QuitAsked();
                return;
            }
#if UNITY_EDITOR
            UnityEditor.EditorApplication.isPlaying = false;
#else
            Application.Quit();
#endif
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
            using var showing = showMarker.Auto();
            foreach (var view in shown.Values)
            {
                view.Seen = false;
                view.Row = -1;
            }
            var count = read ? Packet.EntityCount : 0;
            if (byRow.Length < count) byRow = new TideEntity[Math.Max(count, byRow.Length * 2)];
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

        // Each shown entity's components into its GameObject's TideComponents (Packages/com.tide.game's), one made
        // where it has none, before scripts' Update reads them; one whose component the entity no longer has goes. Only
        // with components generated from the schema of the build running, whose values they lay out.
        void Live(int count)
        {
            if (count == 0 || !live) return;
            liveFrame++;
            for (var b = 0; b < liveComponents.Length; b++)
            {
                var binding = liveComponents[b];
                var n = Packet.Rows((uint)binding.type, out var stride, out var entities, out var values);
                if (n == 0 || stride != binding.size) continue;
                // Its local fields: each entity's row of its component's local section
                int[] localRows = null;
                byte* localValues = null;
                uint localStride = 0;
                var localCount = 0;
                if (binding.local >= 0)
                {
                    localCount = Packet.Rows((uint)binding.local, out localStride, out _, out localValues, true);
                    if (localCount > 0) localRows = RowsOf(binding.local);
                }
                for (var r = 0; r < n; r++, values += stride)
                {
                    var e = entities[r];
                    if (e >= (uint)count) continue;
                    var view = byRow[e];
                    if (view == null) continue;
                    var component = view.Live(binding.kind);
                    if (component == null) continue;
                    byte* locals = null;
                    if (localRows != null)
                    {
                        var row = localRows[e];
                        if ((uint)row < (uint)localCount) locals = localValues + (long)row * localStride;
                    }
                    if (view.LiveSeenAt != liveFrame)
                    {
                        view.LiveSeenAt = liveFrame;
                        view.LiveSeen = 0;
                    }
                    view.LiveSeen++;
                    component.LiveFrame = liveFrame;
                    component.ReadLive(values, locals, Packet, this);
                }
            }
            // A component the entity lost: only the GameObjects holding more than the entity has are looked through
            for (var i = 0; i < count; i++)
            {
                var view = byRow[i];
                if (view == null) continue;
                var seen = view.LiveSeenAt == liveFrame ? view.LiveSeen : 0;
                if (seen != view.LiveCount) view.DropStale(liveFrame);
            }
        }

        readonly Dictionary<Type, TideSingleton> singletons = new Dictionary<Type, TideSingleton>();

        // The match's singletons into the TideSingletons the runner keeps (Get), as its components' go into theirs
        void LiveSingletons(bool read)
        {
            if (!read || !Packet.InMatch || !live)
            {
                if (singletons.Count > 0) singletons.Clear();
                return;
            }
            for (var b = 0; b < liveSingletons.Length; b++)
            {
                var binding = liveSingletons[b];
                var values = Packet.Singleton((uint)binding.type, out var stride);
                if (values == null || stride != binding.size) continue;
                var locals = binding.local >= 0 ? Packet.Singleton((uint)binding.local, out _, true) : null;
                if (!singletons.TryGetValue(binding.kind, out var singleton))
                {
                    singleton = (TideSingleton)(binding.make != null ? binding.make() : Activator.CreateInstance(binding.kind));
                    singletons[binding.kind] = singleton;
                }
                singleton.ReadLive(values, locals, Packet, this);
            }
        }

        // The match's singleton `T` this frame, as Tide's views see it: null outside a match. Its lists, maps, sets and
        // grids are the ones it had the frame before, filled again: copy what should outlast the frame.
        public T Get<T>() where T : TideSingleton
        {
            return singletons.TryGetValue(typeof(T), out var singleton) ? (T)singleton : null;
        }

        readonly Dictionary<Type, TideHearing> hearing = new Dictionary<Type, TideHearing>();
        readonly Dictionary<Type, TideHearing> hearingVerified = new Dictionary<Type, TideHearing>();

        // What hears the match's event `T` as this machine's local handlers would: each one, as soon as this machine
        // has it (as `local event(Hit hit)` does), which a correction can turn out not to have happened; or once the
        // server confirmed its tick (OnVerified, as `local event(verified Hit hit)`). Before scripts' Update, after the
        // frame's components are live. An entity's TideEntity.On hears only those sent to it. Each event heard is an
        // object of its own, a handler's to keep.
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

        // The packet's events to what hears them: the runner's, then the entity's view's. One nothing hears isn't made.
        void Hear(int count)
        {
            var heard = Packet.HeardCount;
            if (heard == 0 || !live) return;
            for (var i = 0; i < heard; i++)
            {
                var record = Packet.Heard(i);
                if (record.Type >= (uint)liveEvents.Length) continue;
                var binding = liveEvents[record.Type];
                if (binding.kind == null) continue;
                var verified = record.Verified;
                var view = record.HasTarget && record.TargetPlace < (uint)count ? byRow[record.TargetPlace] : null;
                var runners = TideHearing.Hears(verified ? hearingVerified : hearing, binding.kind);
                var views = view != null && view.Hears(binding.kind, verified);
                if (!runners && !views) continue;
                var values = Packet.HeardValue(record, binding.size);
                if (values == null) continue;
                var e = (TideEvent)(binding.make != null ? binding.make() : Activator.CreateInstance(binding.kind));
                e.Tick = record.Tick;
                e.Verified = verified;
                e.TargetEntity = new TideEntityId { birth = record.TargetBirth, lineage = record.TargetLineage };
                e.Target = view != null ? view.gameObject : null;
                e.ReadLive(values, Packet, this);
                if (runners) TideHearing.Tell(verified ? hearingVerified : hearing, binding.kind, e, this);
                if (views && view != null) view.Hear(binding.kind, e);
            }
        }

        // The entity a GameObject shows this frame, for an entity field of a command: none for null or one that shows none.
        public TideEntityId EntityOf(GameObject shown)
        {
            if (shown == null) return default;
            var view = shown.GetComponent<TideEntity>();
            return view != null && view.Runner == this && view.Row >= 0 ? (TideEntityId)view.Entity : default;
        }

        // The GameObject an entity shows through this frame, for an entity field of a TideComponent: null for none.
        public GameObject GameObjectOf(TideEntityId entity)
        {
            return !entity.IsNull && shown.TryGetValue(entity, out var view) && view != null && view.Row >= 0 ? view.gameObject : null;
        }

        // The view of the packet's entity `row`, made or found the first time it's seen: by its UnityEngine.Source if it
        // has one, else by the view rules.
        TideEntity ViewOf(int row)
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
                view.CollectLive();
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
        bool Same(TideEntity view, TideEntityRow entity, int row)
        {
            if (view.Born != 0 && entity.Born != 0 && view.Born != entity.Born) return false;
            SourceOf(row, out var asset, out var node);
            return asset == view.SourceAsset && node == view.SourceNode;
        }

        // Lets go of an entity's GameObject: a scene's, or one in a prefab's instance, hidden till its entity's back;
        // one made from a prefab, to the spawner, the pool or destroyed.
        void Release(TideEntity view)
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
            made.name = $"{prefab.name} ({entity.Id})";
            return made;
        }

        TideEntity Make(TideEntityRow entity, int row)
        {
            var view = MakeView(entity, row);
            if (view == null) return null;
            SourceOf(row, out view.SourceAsset, out view.SourceNode);
            view.Born = entity.Born;
            view.PlacedAt = -1;
            return view;
        }

        TideEntity MakeView(TideEntityRow entity, int row)
        {
            if (!SourceOf(row, out var asset, out var node))
            {
                var prefab = RuleFor(entity, row);
                if (prefab == null) return null;
                var made = Ensure<TideEntity>(MakeFrom(prefab, entity));
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
                var root = Ensure<TideEntity>(MakeFrom(prefab, entity));
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
            var view = Ensure<TideEntity>(found.gameObject);
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
        void Place(TideEntity view, int row)
        {
            if (view.PlacedAt == frames) return;
            view.PlacedAt = frames;
            var entity = view.Entity;
            var parent = entity.HasParent && entity.Parent < (uint)Packet.EntityCount ? byRow[entity.Parent] : null;
            if (parent != null) Place(parent, (int)entity.Parent);
            var under = parent != null ? parent.transform : null;
            if (!view.Authored && view.transform.parent != under) view.transform.SetParent(under, false);
            if (transformType < 0) return;
            var r = RowOf(transformType, row);
            if (r < 0) return;
            var value = Packet.Row((uint)transformType, r, out _);
            if (value == null) return;
            var p = (float*)(value + transformPosition);
            var q = (float*)(value + transformRotation);
            var s = (float*)(value + transformScale);
            // Tide is left-handed, y up, z forward, as Unity: the same numbers
            var at = new Vector3(p[0], p[1], p[2]);
            var turn = new Quaternion(q[0], q[1], q[2], q[3]);
            var size = new Vector3(s[0], s[1], s[2]);
            var moved = view.transform;
            if (moved.parent == under)
            {
                // Written only where it isn't there already: an entity that stays put leaves its Transform, and what
                // Unity keeps by it (static batches, physics, children), untouched
                moved.GetLocalPositionAndRotation(out var wasAt, out var wasTurned);
                if (wasAt.x != at.x || wasAt.y != at.y || wasAt.z != at.z || wasTurned.x != turn.x || wasTurned.y != turn.y
                    || wasTurned.z != turn.z || wasTurned.w != turn.w)
                {
                    moved.SetLocalPositionAndRotation(at, turn);
                }
                var wasSize = moved.localScale;
                if (wasSize.x != size.x || wasSize.y != size.y || wasSize.z != size.z) moved.localScale = size;
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
        readonly Dictionary<TideEntityId, int> sceneRoots = new Dictionary<TideEntityId, int>(); // This frame's roots, and their assets
        readonly Dictionary<TideEntityId, UnityEngine.SceneManagement.Scene> sceneOf = new Dictionary<TideEntityId, UnityEngine.SceneManagement.Scene>();
        readonly List<(TideEntityId root, string path)> sceneLoads = new List<(TideEntityId, string)>(); // Begun, for roots, in order
        readonly HashSet<UnityEngine.SceneManagement.Scene> sceneUnloads = new HashSet<UnityEngine.SceneManagement.Scene>(); // Scenes this unloads
        readonly HashSet<UnityEngine.SceneManagement.Scene> scenesStarted = new HashSet<UnityEngine.SceneManagement.Scene>(); // Open as this started
        readonly List<TideEntityId> scenesGone = new List<TideEntityId>();
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

        bool Loading(TideEntityId root)
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
            if ((uint)entity >= (uint)Packet.EntityCount) return -1;
            var table = RowsOf(type);
            return table != null ? table[entity] : -1;
        }

        // Each of the packet's entities' row in type `type`'s section this frame, -1 for an entity without it: made
        // the first time a frame asks for the type, and null for a type the schema hasn't.
        int[] RowsOf(int type)
        {
            if (Schema == null || (uint)type >= (uint)Schema.types.Length) return null;
            var count = Packet.EntityCount;
            if (rows.Length < Schema.types.Length)
            {
                Array.Resize(ref rows, Schema.types.Length);
                Array.Resize(ref rowsAt, Schema.types.Length);
                Array.Resize(ref rowsLocal, Schema.types.Length);
                for (var i = 0; i < rowsLocal.Length; i++) rowsLocal[i] = Schema.types[i].kind == "local";
            }
            var table = rows[type];
            // Room to grow into, so entities coming one at a time don't make a table each
            if (table == null || table.Length < count) table = rows[type] = new int[Math.Max(Math.Max(count, 16), (table?.Length ?? 0) * 2)];
            if (rowsAt[type] != frames)
            {
                rowsAt[type] = frames;
                for (var i = 0; i < count; i++) table[i] = -1;
                var n = Packet.Rows((uint)type, out _, out var entities, out _, rowsLocal[type]);
                for (var k = 0; k < n; k++)
                {
                    var e = entities[k];
                    if (e < (uint)count) table[e] = k;
                }
            }
            return table;
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

        // ---------------------------------------------------------------------------------------------------------------
        // Time travel (tide/embed.h): a match this machine runs, recorded from its start, the worlds of its latest
        // ticks kept. Paused, it runs no tick, and the frames show the tick paused at or one shown again: its
        // entities, components and singletons in the packet, the GameObjects showing them and Tide's drawing. With
        // players of other machines in it, it never stops for them: it goes on behind the tick shown, and Resume shows
        // its latest again. Tick n is the world after n ticks.

        TideTravel travel;

        // Where time travel is, as of the last frame
        public TideTravel Travel => travel;

        public bool TimeTravelOn => timeTravel == TideTimeTravel.EditorAndPlayers || (timeTravel == TideTimeTravel.Editor && Application.isEditor);

        // Keeps what timeTravel and keepSeconds say from now on
        public void KeepTicks()
        {
            if (embed != IntPtr.Zero) library.TravelKeep(embed, TimeTravelOn ? Mathf.Max(0.1f, keepSeconds) : 0f);
        }

        // The match pauses at its latest tick: false when it can't (Travel.Why says why). Showing a recording, it
        // stops playing.
        public bool Pause() => TravelCall(() => library.Pause(embed, 1) != 0);

        // The match goes on from its latest tick (whatever's shown), or a recording plays from the tick shown.
        public bool Resume() => TravelCall(() => library.Pause(embed, 0) != 0);

        // Paused: one tick on, at the next frame at the latest tick; a recording's next tick.
        public bool Step() => TravelCall(() => library.Step(embed) != 0);

        // Paused or replaying: shows the world after `tick` ticks, from Travel.first to Travel.last.
        public bool ShowTick(uint tick) => TravelCall(() => library.Show(embed, tick) != 0);

        // Paused: the match goes back to the tick shown and on from there, the ticks after it gone, for the players of
        // other machines too. Which commands are in it stays as it was, so none comes twice.
        public bool ContinueFromHere() => TravelCall(() => library.Continue(embed) != 0);

        // The match's recording from its start, as bytes another machine's editor replays (Replay); null with none.
        public byte[] Recording()
        {
            if (embed == IntPtr.Zero) return null;
            uint size = 0;
            var bytes = library.Recording(embed, &size);
            if (bytes == null || size == 0) return null;
            var copy = new byte[size];
            Marshal.Copy((IntPtr)bytes, copy, 0, (int)size);
            return copy;
        }

        // ...written to a file (a .tiderec by Tide's editor window): false with nothing recorded, or the file unwritten.
        public bool SaveRecording(string path)
        {
            var bytes = Recording();
            if (bytes == null) return false;
            try
            {
                System.IO.File.WriteAllBytes(path, bytes);
                return true;
            }
            catch (Exception e)
            {
                Debug.LogError($"Tide: can't write the recording to {path}: {e.Message}", this);
                return false;
            }
        }

        // Shows a recording, from its first tick, paused: this build makes each tick again as it's shown, and checks its
        // world against the recording's (Travel.differs). The match, if any, pauses meanwhile. False, saying why in
        // Travel.Refused, for bytes that aren't a recording, one of another data layout, or a match that can't pause.
        public bool Replay(byte[] recording)
        {
            if (embed == IntPtr.Zero || recording == null || recording.Length == 0) return false;
            bool done;
            fixed (byte* p = recording) done = library.Replay(embed, p, (uint)recording.Length) != 0;
            fixed (TideTravel* t = &travel) library.Travel(embed, t);
            return done;
        }

        public bool ReplayFile(string path)
        {
            byte[] bytes;
            try
            {
                bytes = System.IO.File.ReadAllBytes(path);
            }
            catch (Exception e)
            {
                Debug.LogError($"Tide: can't read the recording {path}: {e.Message}", this);
                return false;
            }
            return Replay(bytes);
        }

        // Back to the match from a recording, paused where it was.
        public void EndReplay() => TravelCall(() =>
        {
            library.ReplayEnd(embed);
            return true;
        });

        // A time travel call, and its state read again at once
        bool TravelCall(Func<bool> call)
        {
            if (embed == IntPtr.Zero) return false;
            var done = call();
            fixed (TideTravel* p = &travel) library.Travel(embed, p);
            return done;
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
            writer ??= new TideCommandWriter(this);
            writer.Reset();
            command.Pack(writer);
            var bytes = writer.Bytes(out var size);
            if (size < Schema.types[type].size) return false;
            var to = EntityOf(target);
            fixed (byte* p = bytes) return library.Command(embed, (uint)type, to.birth, to.lineage, p, (uint)size) != 0;
        }

        TideCommandWriter writer; // Written again by each command sent

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
            return Send(value, target.Birth, target.Lineage);
        }

        bool Send<T>(T value, ulong birth, ulong lineage) where T : unmanaged
        {
            if (embed == IntPtr.Zero || Schema == null) return false;
            var type = TypeOf<T>.Index(Schema);
            if (type < 0) return false;
            return library.Command(embed, (uint)type, birth, lineage, &value, (uint)sizeof(T)) != 0;
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
