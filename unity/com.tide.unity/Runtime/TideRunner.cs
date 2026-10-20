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
    public sealed unsafe class TideRunner : MonoBehaviour
    {
        [Tooltip("As the game's command line: --host, --join <code>, --connect <address>, --seed <n>")]
        public string arguments = "";

        [Tooltip("Another relay's address; empty for Tide's")]
        public string relay = "";

        [Tooltip("Draw what the game's Tide views draw (Draw's 2D calls) over Unity's frame")]
        public bool drawTide = true;

        [Tooltip("Keep the GameObject of an entity that goes, inactive, for the next entity of its prefab: reset what Unity alone keeps on it (an Animator's state, particles) in OnDisable or OnEnable")]
        public bool pool;

        // Makes and lets go of the GameObjects of the prefabs entities show through, in place of Instantiate and
        // Destroy, or the pool: null for those
        public ITideSpawner Spawner { get; set; }

        public List<TideViewRule> views = new List<TideViewRule>();

        [HideInInspector] public List<TidePrefabLink> prefabs = new List<TidePrefabLink>();

        // A player's map of the Unity prefabs and scenes (unity.json), which the build put here: a web player can't
        // read files of its own
        [HideInInspector] public string map = "";

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
            for (var i = 0; i < UnityEngine.SceneManagement.SceneManager.sceneCount; i++)
            {
                scenesOpen.Add(UnityEngine.SceneManagement.SceneManager.GetSceneAt(i).path);
            }
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
            if (drawTide)
            {
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
            if (embed != IntPtr.Zero) library.Destroy(embed);
            embed = IntPtr.Zero;
            library = null;
            if (Current == this) Current = null;
        }

        void OnApplicationPause(bool paused)
        {
            if (embed != IntPtr.Zero) library.Background(embed, (byte)(paused ? 1 : 0));
        }

        void Update()
        {
            if (embed == IntPtr.Zero) return;
            TideInputReader.Read(ref input, 1f);
            fixed (TideEmbedInput* p = &input)
            {
                library.Frame(embed, Time.unscaledDeltaTime, p, Screen.width, Screen.height, 1f);
            }
            fixed (TideEmbedStatus* p = &status) library.Status(embed, p);
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
                found = TideUnityMap.FindInScene(UnityEngine.SceneManagement.SceneManager.GetSceneByPath(a.path), path);
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
        // loaded beside the others as its root comes, if it isn't open already, and unloaded as it goes. Build
        // Settings' first is one, which the match loads as Main starts (tide unity's UnityEngine.Begin).
        readonly HashSet<int> scenesWanted = new HashSet<int>();
        readonly HashSet<string> scenesLoading = new HashSet<string>();
        readonly HashSet<string> scenesShown = new HashSet<string>(); // ...that the match has had while this ran
        bool openSent;
        readonly List<string> scenesOpen = new List<string>(); // What the editor had open as this started

        void FollowScenes(int count)
        {
            if (sourceType < 0) return;
            scenesWanted.Clear();
            for (var i = 0; i < count; i++)
            {
                if (!SourceOf(i, out var asset, out var node) || node != 0) continue;
                var a = Map.Of(asset);
                if (a != null && a.scene) scenesWanted.Add(asset);
            }
            foreach (var asset in scenesWanted)
            {
                var path = Map.Of(asset).path;
                scenesShown.Add(path);
                var scene = UnityEngine.SceneManagement.SceneManager.GetSceneByPath(path);
                if ((scene.IsValid() && scene.isLoaded) || scenesLoading.Contains(path)) continue;
                var loading = LoadScene(path);
                if (loading == null) continue;
                scenesLoading.Add(path);
                loading.completed += _ => scenesLoading.Remove(path);
            }
            // What the match had and has no more
            foreach (var a in Map.assets)
            {
                if (!a.scene || !scenesShown.Contains(a.path)) continue;
                var still = false;
                foreach (var asset in scenesWanted) still |= Map.Of(asset).path == a.path;
                if (still) continue;
                if (scenesLoading.Contains(a.path)) continue; // Let go of while it loads: unloaded once it has
                var scene = UnityEngine.SceneManagement.SceneManager.GetSceneByPath(a.path);
                if (scene.IsValid() && scene.isLoaded && UnityEngine.SceneManagement.SceneManager.sceneCount > 1)
                {
                    UnityEngine.SceneManagement.SceneManager.UnloadSceneAsync(scene);
                }
                scenesShown.Remove(a.path);
            }
            OpenEditorScenes();
        }

        static AsyncOperation LoadScene(string path)
        {
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

        // In the editor, the match has the scenes the editor has open, of those holding Tide entities, rather than
        // Build Settings' first: UnityEngine.OpenScene, which the match takes from its first player alone, the server's as it
        // started. Sent once, as this machine runs the match.
        void OpenEditorScenes()
        {
            if (!Application.isEditor || openSent || status.server == 0 || State != TideSessionState.Connected) return;
            openSent = true;
            var type = Schema.TypeIndex("UnityEngine.OpenScene");
            var t = Schema.Type(type);
            var assetField = t?.Field("asset");
            var onlyField = t?.Field("only");
            if (assetField == null || onlyField == null) return;
            var open = new List<int>();
            foreach (var path in scenesOpen)
            {
                for (var k = 0; k < Map.assets.Length; k++)
                {
                    if (Map.assets[k].scene && Map.assets[k].path == path) open.Add(k);
                }
            }
            var value = new byte[Math.Max(1, (int)t.size)];
            for (var i = 0; i < open.Count; i++)
            {
                Array.Clear(value, 0, value.Length);
                BitConverter.GetBytes(open[i]).CopyTo(value, (int)assetField.offset);
                value[onlyField.offset] = (byte)(i == 0 ? 1 : 0); // The first: these, and no others
                fixed (byte* bytes = value) library.Command(embed, (uint)type, 0, 0, bytes, (uint)value.Length);
            }
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
