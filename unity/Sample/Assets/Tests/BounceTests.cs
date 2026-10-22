using System.Collections;
using NUnit.Framework;
using Tide;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.TestTools;

// The sample in play mode: Tide runs Bounce's match in the editor, and Unity shows this machine's player's ball,
// placed and colored by the match.
public sealed unsafe class BounceTests
{
    const string Bounce = "Assets/Sample/Bounce.unity";
    const string Bonus = "Assets/Sample/Bonus.unity";

    // The runner goes on across scene loads (the match decides which are loaded): each test ends its own
    [UnityTearDown]
    public IEnumerator EndTheMatch()
    {
        if (TideRunner.Current != null) Object.Destroy(TideRunner.Current.gameObject);
        yield return null;
    }

    // Waits by real time (a batch run's frames are short) for `done`, up to `seconds`.
    static IEnumerator Until(System.Func<bool> done, float seconds)
    {
        var until = Time.realtimeSinceStartup + seconds;
        while (!done() && Time.realtimeSinceStartup < until) yield return null;
    }

    static TideEntityView Totem()
    {
        var scene = SceneManager.GetSceneByPath(Bonus);
        if (!scene.IsValid() || !scene.isLoaded) return null;
        foreach (var root in scene.GetRootGameObjects())
        {
            var totem = root.transform.Find("Totem");
            var view = totem != null ? totem.GetComponent<TideEntityView>() : null;
            if (view != null && view.Showing) return view;
        }
        return null;
    }

    // The match loads Bonus's entities when its code says, and Unity loads the scene with them beside Bounce; and
    // unloads it as the match does.
    [UnityTest]
    public IEnumerator TheMatchLoadsAndUnloadsScenes()
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        yield return Until(() => runner.State == TideSessionState.Connected && runner.Packet.InMatch, 10f);
        Assert.AreEqual(TideSessionState.Connected, runner.State);
        Assert.IsFalse(SceneManager.GetSceneByPath(Bonus).isLoaded, "Bonus isn't the match's yet");
        Assert.IsTrue(runner.Send(new Tide.Game.ShowBonus { on = true }), "the command is the game's");
        yield return Until(() => Totem() != null, 10f);
        var totem = Totem();
        Assert.IsNotNull(totem, "Unity loaded Bonus as the match did, and the Totem shows its entity");
        Assert.AreEqual(3f, totem.GetComponent<Tide.Game.Spinner>()?.speed, "with the scene's Spinner, live");
        Assert.IsTrue(SceneManager.GetSceneByPath(Bounce).isLoaded, "beside Bounce");
        // Spinning is "without scene Bonus": the totem stays as Bonus has it
        var turn = totem.transform.rotation;
        yield return Until(() => false, 1f);
        Assert.Less(Quaternion.Angle(turn, totem.transform.rotation), 0.01f, "Bonus's totem doesn't spin");
        runner.Send(new Tide.Game.ShowBonus { on = false });
        yield return Until(() => !SceneManager.GetSceneByPath(Bonus).isLoaded, 10f);
        Assert.IsFalse(SceneManager.GetSceneByPath(Bonus).isLoaded, "and unloaded it as the match did");
    }

    // Every Bonus open with its Totem showing an entity
    static System.Collections.Generic.List<TideEntityView> Totems()
    {
        var totems = new System.Collections.Generic.List<TideEntityView>();
        for (var i = 0; i < SceneManager.sceneCount; i++)
        {
            var scene = SceneManager.GetSceneAt(i);
            if (scene.path != Bonus || !scene.isLoaded) continue;
            foreach (var root in scene.GetRootGameObjects())
            {
                var totem = root.transform.Find("Totem");
                var view = totem != null ? totem.GetComponent<TideEntityView>() : null;
                if (view != null && view.Showing) totems.Add(view);
            }
        }
        return totems;
    }

    static int Open(string path)
    {
        var open = 0;
        for (var i = 0; i < SceneManager.sceneCount; i++) open += SceneManager.GetSceneAt(i).path == path ? 1 : 0;
        return open;
    }

    // A scene the match loads twice is two of Unity's, each showing its own entities; both unloaded as the match
    // unloads them.
    [UnityTest]
    public IEnumerator ASceneLoadedTwiceIsTwo()
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        yield return Connected(runner);
        Assert.IsTrue(runner.Send(new Tide.Game.ShowBonus { on = true, twice = true }));
        yield return Until(() => Totems().Count == 2, 10f);
        var totems = Totems();
        Assert.AreEqual(2, totems.Count, "two of Bonus, each with its Totem showing");
        Assert.AreNotEqual(totems[0].Entity.Id, totems[1].Entity.Id, "each its own entity");
        Assert.IsTrue(totems[0].gameObject.scene != totems[1].gameObject.scene, "in a scene of its own");
        runner.Send(new Tide.Game.ShowBonus { on = false });
        yield return Until(() => Open(Bonus) == 0, 10f);
        Assert.AreEqual(0, Open(Bonus), "both unloaded as the match did");
    }

    // A scene loaded by hand during Play (in the editor, or by the game's own C#) is local: the match doesn't have it,
    // and its Totem shows no entity. The match loading Bonus loads its own beside it, whose Totem does, and which a
    // local scene's going leaves; the match unloading it leaves the local one.
    [UnityTest]
    public IEnumerator ScenesLoadedByHandAreLocal()
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        yield return Connected(runner);
        yield return SceneManager.LoadSceneAsync(Bonus, LoadSceneMode.Additive);
        var local = SceneManager.GetSceneAt(SceneManager.sceneCount - 1);
        Assert.AreEqual(Bonus, local.path);
        yield return Until(() => false, 1f);
        Assert.AreEqual(0, Totems().Count, "the local Bonus's Totem shows no entity");

        Assert.IsTrue(runner.Send(new Tide.Game.ShowBonus { on = true }));
        yield return Until(() => Totems().Count == 1 && Open(Bonus) == 2, 10f);
        Assert.AreEqual(2, Open(Bonus), "the match's Bonus, beside the local one");
        var totem = Totems();
        Assert.AreEqual(1, totem.Count, "one Totem shows the match's entity");
        Assert.IsTrue(totem[0].gameObject.scene != local, "the match's Bonus's");

        yield return SceneManager.UnloadSceneAsync(local);
        yield return Until(() => false, 1f);
        Assert.AreEqual(1, Open(Bonus), "the local one went alone");
        Assert.AreEqual(1, Totems().Count, "and the match's Totem still shows its entity");

        yield return SceneManager.LoadSceneAsync(Bonus, LoadSceneMode.Additive);
        local = SceneManager.GetSceneAt(SceneManager.sceneCount - 1);
        runner.Send(new Tide.Game.ShowBonus { on = false });
        yield return Until(() => Open(Bonus) == 1, 10f);
        Assert.AreEqual(1, Open(Bonus), "the match let its Bonus go");
        Assert.IsTrue(local.isLoaded, "and the local one stays");
        Assert.AreEqual(0, Totems().Count);
    }

    // With Bonus alone open, the match has it rather than Build Settings' first, Bounce, which it loaded as Main
    // started and then let go, and Unity with it.
    [UnityTest]
    public IEnumerator TheEditorsOpenScenesAloneAreTheMatchs()
    {
        SceneManager.LoadScene(Bonus);
        yield return null;
        new GameObject("Tide").AddComponent<TideRunner>();
        yield return Until(() => Totem() != null && !SceneManager.GetSceneByPath(Bounce).isLoaded, 10f);
        Assert.IsNotNull(Totem(), "the match has Bonus's entities");
        // Bounce came as Main started, and went with the editor's command
        yield return Until(() => !SceneManager.GetSceneByPath(Bounce).isLoaded, 5f);
        Assert.IsFalse(SceneManager.GetSceneByPath(Bounce).isLoaded, "and not Bounce's");
    }

    // In the editor, the match has the scenes the editor has open: Bonus too, without the game's code asking.
    [UnityTest]
    public IEnumerator TheEditorsOpenScenesAreTheMatchs()
    {
        SceneManager.LoadScene(Bounce);
        SceneManager.LoadScene(Bonus, LoadSceneMode.Additive);
        yield return null;
        yield return Until(() => Totem() != null, 10f);
        Assert.IsNotNull(Totem(), "the match loaded Bonus's entities, which the open scene shows");
        Assert.IsNotNull(GameObject.Find("Pillars")?.transform.GetChild(0).GetComponent<TideEntityView>(), "and Bounce's");
    }

    // Whether the frame's draw packet (tide/embed.h's tide_embed_draw) has a text command saying `words`.
    static bool DrewText(TideRunner runner, string words)
    {
        var bytes = runner.DrawPacket(out var length);
        if (bytes == null || length < 64) return false;
        var header = (uint*)bytes;
        ulong at = 64;
        ulong Part(uint count, uint each)
        {
            var from = (at + 7) & ~7ul;
            at = from + (ulong)count * each;
            return from;
        }
        var commands = (TideDrawCommand*)(bytes + Part(header[2], header[9]));
        Part(header[3], header[10]);
        Part(header[4], header[11]);
        Part(header[5], 4);
        var text = bytes + Part(header[6], 1);
        for (var i = 0; i < header[2]; i++)
        {
            if (commands[i].kind != 7 || commands[i].text >= header[6]) continue;
            var start = text + commands[i].text;
            var n = 0;
            while (start[n] != 0) n++;
            if (System.Text.Encoding.UTF8.GetString(start, n) == words) return true;
        }
        return false;
    }

    // Where the Ring view drew its ring (a wire circle of radius 24), in Tide's screen pixels
    static bool Ringed(TideRunner runner, out Vector2 at)
    {
        at = default;
        var bytes = runner.DrawPacket(out var length);
        if (bytes == null || length < 64) return false;
        var header = (uint*)bytes;
        var commands = (TideDrawCommand*)(bytes + 64);
        for (var i = 0; i < header[2]; i++)
        {
            if (commands[i].kind != 3 || commands[i].b.x != 24f) continue;
            at = commands[i].a;
            return true;
        }
        return false;
    }

    static TideEntityView Tossed()
    {
        foreach (var view in Object.FindObjectsByType<TideEntityView>())
        {
            if (view.Showing && view.TryGetComponent(out Tide.Game.Ball ball) && ball.seat == -1) return view;
        }
        return null;
    }

    static IEnumerator Connected(TideRunner runner)
    {
        yield return Until(() => runner.State == TideSessionState.Connected && runner.Packet.InMatch, 10f);
        Assert.AreEqual(TideSessionState.Connected, runner.State);
    }

    // With the pool on, the GameObject of an entity that goes is kept, inactive, and the next entity of its prefab
    // shows through it.
    [UnityTest]
    public IEnumerator ThePoolKeepsAGameObjectForTheNextEntity()
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        runner.pool = true;
        yield return Connected(runner);
        Assert.IsTrue(runner.Send(new Tide.Game.Toss { on = true }));
        yield return Until(() => Tossed() != null, 10f);
        var first = Tossed();
        Assert.IsNotNull(first, "the tossed ball shows");
        var made = first.gameObject;
        var entity = first.Entity.Id;
        Assert.IsTrue(runner.Send(new Tide.Game.Toss { on = false }));
        yield return Until(() => !made.activeSelf, 10f);
        Assert.IsFalse(made.activeSelf, "its GameObject is kept, inactive, as its entity went");
        Assert.AreEqual("Tide pool", made.transform.parent != null ? made.transform.parent.name : null, "in the pool");
        Assert.IsTrue(runner.Send(new Tide.Game.Toss { on = true }));
        yield return Until(() => Tossed() != null, 10f);
        var second = Tossed();
        Assert.IsNotNull(second, "the next tossed ball shows");
        Assert.AreNotEqual(entity, second.Entity.Id, "another entity");
        Assert.AreSame(made, second.gameObject, "through the GameObject the pool kept");
        Assert.IsTrue(made.activeInHierarchy && made.transform.parent == null, "active again, out of the pool");
    }

    sealed class CountingSpawner : ITideSpawner
    {
        public readonly System.Collections.Generic.List<GameObject> made = new System.Collections.Generic.List<GameObject>();
        public readonly System.Collections.Generic.List<GameObject> released = new System.Collections.Generic.List<GameObject>();

        public GameObject Make(GameObject prefab)
        {
            var view = Object.Instantiate(prefab);
            made.Add(view);
            return view;
        }

        public void Release(GameObject prefab, GameObject view)
        {
            released.Add(view);
            Object.Destroy(view);
        }
    }

    // A game's own spawner makes and lets go of the GameObjects of prefabs' entities in place of the runner.
    [UnityTest]
    public IEnumerator ASpawnerMakesAndLetsGoOfGameObjects()
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        var spawner = new CountingSpawner();
        runner.Spawner = spawner;
        yield return Connected(runner);
        Assert.IsTrue(runner.Send(new Tide.Game.Toss { on = true }));
        yield return Until(() => Tossed() != null, 10f);
        var tossed = Tossed();
        Assert.IsNotNull(tossed, "the tossed ball shows");
        var made = tossed.gameObject;
        Assert.Contains(made, spawner.made, "through a GameObject from the spawner");
        Assert.IsTrue(runner.Send(new Tide.Game.Toss { on = false }));
        yield return Until(() => spawner.released.Contains(made), 10f);
        Assert.Contains(made, spawner.released, "which it let go of as its entity went");
    }

    static TideEntityView MyBall()
    {
        foreach (var view in Object.FindObjectsByType<TideEntityView>())
        {
            if (view.Showing && view.Mine && view.GetComponent<Tide.Game.Ball>() != null) return view;
        }
        return null;
    }

    // The match's events reach C# as this machine's local handlers hear them: the ball's bounces as it falls onto the
    // floor, heard at once and again once confirmed, the ball's own view hearing those sent to it; and its local field
    // counts them as Tide's local handler does.
    [UnityTest]
    public IEnumerator EventsAreHeardAsLocalHandlersHearThem()
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        int heard = 0, verified = 0, own = 0;
        GameObject target = null;
        var speed = 0f;
        runner.On<Tide.Game.Bounced>(e =>
        {
            heard++;
            target = e.Target;
            speed = e.speed;
        });
        runner.OnVerified<Tide.Game.Bounced>(e => verified += e.Verified ? 1 : 0);
        TideEntityView ball = null;
        // As soon as the ball shows: it's dropped from 2 units up, and bounces as it lands
        yield return Until(() => (ball = MyBall()) != null, 10f);
        Assert.IsNotNull(ball, "the player's ball shows");
        ball.On<Tide.Game.Bounced>(e => own++);
        yield return Until(() => heard >= 2 && verified >= 2, 10f);
        Assert.GreaterOrEqual(heard, 2, "the ball bounced, as this machine heard it");
        Assert.AreEqual(heard, verified, "each heard again once the server confirmed it");
        Assert.AreEqual(heard, own, "the ball's view heard those sent to its entity");
        Assert.AreEqual(ball.gameObject, target, "sent to the ball");
        Assert.Greater(speed, 2f, "with the event's fields");
        Assert.AreEqual(heard, ball.GetComponent<Tide.Game.Ball>().heard, "its local field, as the local handler counted");
    }

    // The game's sound plays through Unity's: the ball's bonk as it lands (its local handler's Audio.Play), Tide's
    // mixer read on Unity's audio thread through the runner's AudioSource. Silent in a run nobody watches
    // (TIDE_HIDDEN), where the mixer's read all the same.
    [UnityTest]
    public IEnumerator TheGamesSoundPlaysThroughUnitys()
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        var audio = runner.GetComponentInChildren<TideAudio>();
        Assert.IsNotNull(audio, "the runner plays the game's sound");
        var source = audio.GetComponent<AudioSource>();
        yield return Until(() => source.isPlaying, 2f); // Once the camera's AudioListener is there
        Assert.IsTrue(source.isPlaying, "through an AudioSource");
        var loudest = 0f;
        yield return Until(() => (loudest = Mathf.Max(loudest, audio.TakeLoudest())) > 0.1f, 10f);
        Assert.Greater(audio.Reads, 0, "Unity's audio thread reads the mixer");
        Assert.Greater(loudest, 0.1f, "the bonk was heard as the ball landed");
        Assert.Less(loudest, 1f);
    }

    // The game's own input on top of Unity's devices (an on-screen stick, a bot, a replay): D held through
    // TideRunner.EditInput rolls this machine's ball to the right, as the key would.
    [UnityTest]
    public IEnumerator TheGamesOwnInputRollsTheBall()
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        TideEntityView ball = null;
        yield return Until(() => (ball = MyBall()) != null, 10f);
        Assert.IsNotNull(ball, "the player's ball shows");
        yield return Until(() => false, 1.5f); // On the floor
        var from = ball.transform.position.x;
        var held = true;
        TideRunner.InputEdit right = (ref TideEmbedInput input) => { if (held) Assert.IsTrue(input.Hold("d")); };
        runner.EditInput += right;
        yield return Until(() => ball.transform.position.x > from + 1f, 5f);
        held = false;
        runner.EditInput -= right;
        Assert.Greater(ball.transform.position.x, from + 1f, "the ball rolled right");
        var input = new TideEmbedInput();
        Assert.IsFalse(input.Hold("no such key"), "a key Tide's keyboard hasn't");
    }

    // A command's text reaches the match, a command goes to the entity a GameObject shows, and a singleton is live.
    [UnityTest]
    public IEnumerator CommandsCarryTextAndGoToGameObjects()
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        TideEntityView ball = null;
        yield return Until(() => (ball = MyBall()) != null, 10f);
        Assert.IsNotNull(ball, "the player's ball shows");
        var room = runner.Get<Tide.Game.Room>();
        Assert.IsNotNull(room, "the match's singleton");
        Assert.AreEqual(1, room.arrivals, "as the match has it");
        Assert.IsTrue(runner.Send(new Tide.Game.Say { text = "héllo, Tide" }));
        yield return Until(() => runner.Get<Tide.Game.Extras>()?.said == "héllo, Tide", 10f);
        var extras = runner.Get<Tide.Game.Extras>();
        Assert.AreEqual("héllo, Tide", extras?.said, "the command's text, kept by the match");
        // A set, a grid and a map of the match's, live as C#'s
        Assert.AreEqual(1, extras.speakers.Count, "who said it, in a set");
        Assert.AreEqual(new Vector3Int(8, 8, 0), extras.board.Size, "a grid's size");
        Assert.AreEqual(1, extras.board[3, 4], "and its cell the command counted");
        Assert.AreEqual(0, extras.board[5, 5], "a cell nothing set is zero");
        Assert.AreEqual(1, runner.Get<Tide.Game.Room>().balls.Count, "the room's map of balls");
        foreach (var pair in runner.Get<Tide.Game.Room>().balls) Assert.AreEqual(ball.gameObject, pair.Value, "its value the ball's GameObject");
        var red = new Color(1, 0, 0, 1);
        Assert.IsTrue(runner.Send(new Tide.Game.Paint { color = red }, ball.gameObject));
        var live = ball.GetComponent<Tide.Game.Ball>();
        yield return Until(() => live.color == red, 10f);
        Assert.AreEqual(red, live.color, "the command went to the ball's entity");
    }

    static bool Near(Color a, Color b, float by) =>
        Mathf.Abs(a.r - b.r) <= by && Mathf.Abs(a.g - b.g) <= by && Mathf.Abs(a.b - b.b) <= by;

    // Tide's renderer draws into Unity's frame (TideRender): the Hud's text, the ball's ring and the cube over it,
    // and the plates in the game's own frag, the one over Unity's floor showing and the one under it hidden by the
    // camera's depth. It needs a GPU: a batch run with -nographics has none, and leaves it out.
    [UnityTest]
    public IEnumerator TideDrawsIntoUnitysFrame()
    {
        if (SystemInfo.graphicsDeviceType == UnityEngine.Rendering.GraphicsDeviceType.Null)
        {
            Assert.Ignore("no GPU in this run (-nographics)");
            yield break;
        }
        yield return DrawsIntoUnitysFrame();
    }

    // ...in URP too, through its pass (Tide.Runtime.URP), with the project's pipeline URP's for the while.
    [UnityTest]
    public IEnumerator TideDrawsIntoUnitysFrameInURP()
    {
        if (SystemInfo.graphicsDeviceType == UnityEngine.Rendering.GraphicsDeviceType.Null)
        {
            Assert.Ignore("no GPU in this run (-nographics)");
            yield break;
        }
        var before = UnityEngine.Rendering.GraphicsSettings.defaultRenderPipeline;
        var quality = QualitySettings.renderPipeline;
        UnityEngine.Rendering.GraphicsSettings.defaultRenderPipeline = UniversalPipeline();
        QualitySettings.renderPipeline = null;
        try
        {
            yield return DrawsIntoUnitysFrame(true);
        }
        finally
        {
            UnityEngine.Rendering.GraphicsSettings.defaultRenderPipeline = before;
            QualitySettings.renderPipeline = quality;
        }
    }

    // The Sample's URP asset, made once (Assets/Sample/URP) where it isn't yet.
    static UnityEngine.Rendering.RenderPipelineAsset UniversalPipeline()
    {
#if UNITY_EDITOR
        const string folder = "Assets/Sample/URP";
        var asset = UnityEditor.AssetDatabase.LoadAssetAtPath<UnityEngine.Rendering.Universal.UniversalRenderPipelineAsset>(folder + "/Pipeline.asset");
        if (asset != null) return asset;
        if (!UnityEditor.AssetDatabase.IsValidFolder(folder)) UnityEditor.AssetDatabase.CreateFolder("Assets/Sample", "URP");
        var renderer = ScriptableObject.CreateInstance<UnityEngine.Rendering.Universal.UniversalRendererData>();
        UnityEditor.AssetDatabase.CreateAsset(renderer, folder + "/Renderer.asset");
        asset = UnityEngine.Rendering.Universal.UniversalRenderPipelineAsset.Create(renderer);
        UnityEditor.AssetDatabase.CreateAsset(asset, folder + "/Pipeline.asset");
        UnityEditor.AssetDatabase.SaveAssets();
        return asset;
#else
        return null;
#endif
    }

    IEnumerator DrawsIntoUnitysFrame(bool universal = false)
    {
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        var camera = Camera.main;
        var target = new RenderTexture(640, 480, 24);
        camera.targetTexture = target;
        yield return Connected(runner);
        Assert.IsTrue(runner.TryGetComponent(out TideRender render) && render.enabled, "Tide's renderer draws on this GPU");
        TideEntityView ball = null;
        yield return Until(() =>
        {
            foreach (var view in Object.FindObjectsByType<TideEntityView>())
            {
                if (view.Mine && view.GetComponent<Tide.Game.Ball>() != null) ball = view;
            }
            return ball != null;
        }, 10f);
        Assert.IsNotNull(ball, "the player's ball is shown");
        yield return Until(() => false, 2.5f); // It falls to the floor
        if (universal)
        {
            // The Sample's materials are the built-in pipeline's: URP's own in their place for the while, as a
            // project in URP has them, so the scene under Tide's drawing is lit as in the built-in pipeline
            var lit = Shader.Find("Universal Render Pipeline/Lit");
            foreach (var r in Object.FindObjectsByType<Renderer>())
            {
                var color = r.sharedMaterial != null && r.sharedMaterial.HasColor("_Color") ? r.sharedMaterial.color : Color.gray;
                r.material = new Material(lit) { color = color };
            }
        }
        camera.Render(); // A batch run's frames have no end to wait for: this frame's drawing, again, now
        var read = new Texture2D(640, 480, TextureFormat.RGBA32, false);
        RenderTexture.active = target;
        read.ReadPixels(new Rect(0, 0, 640, 480), 0, 0);
        read.Apply();
        RenderTexture.active = null;
        var dump = System.Environment.GetEnvironmentVariable("TIDE_UNITY_DUMP"); // A picture of it, to look at
        if (!string.IsNullOrEmpty(dump)) System.IO.File.WriteAllBytes(dump, read.EncodeToPNG());
        Color At(Vector3 world)
        {
            var p = camera.WorldToScreenPoint(world);
            return read.GetPixel(Mathf.RoundToInt(p.x), Mathf.RoundToInt(p.y));
        }
        var orange = new Color(1f, 0.5f, 0f);
        var blue = new Color(0.1f, 0.4f, 1f);
        Assert.IsTrue(Near(At(new Vector3(-1.1f, 0.02f, -2.25f)), orange, 0.03f), $"the plate over the floor shows its orange stripe: {At(new Vector3(-1.1f, 0.02f, -2.25f))}");
        Assert.IsTrue(Near(At(new Vector3(-0.375f, 0.02f, -2.25f)), blue, 0.03f), $"and its blue one: {At(new Vector3(-0.375f, 0.02f, -2.25f))}");
        var under = At(new Vector3(-1.1f, -0.05f, 1.75f));
        Assert.IsFalse(Near(under, orange, 0.2f) || Near(under, blue, 0.2f), $"the plate under the floor is hidden: {under}");
        var live = ball.GetComponent<Tide.Game.Ball>();
        var cube = At(ball.transform.position + new Vector3(0, 1.2f, 0));
        Assert.IsTrue(Near(cube, live.color, 0.03f), $"the cube over the ball has its color: {cube} for {live.color}");
        // The ring: a pixel of nearly the ball's color on its lower half, 24 pixels from its middle (its edge is
        // smooth, so the floor shows through some)
        var middle = camera.WorldToScreenPoint(ball.transform.position);
        var ringed = 0;
        for (var i = 0; i < 16; i++)
        {
            var angle = Mathf.PI * (1f + i / 16f);
            for (var r = 22; r <= 26; r++)
            {
                var x = Mathf.RoundToInt(middle.x + Mathf.Cos(angle) * r);
                var y = Mathf.RoundToInt(middle.y + Mathf.Sin(angle) * r);
                if (Near(read.GetPixel(x, y), live.color, 0.35f))
                {
                    ringed++;
                    break;
                }
            }
        }
        Assert.Greater(ringed, 10, "the ring is drawn around the ball");
        // The Hud's white text, from 16 pixels across and 30 down
        var white = 0;
        for (var x = 16; x < 110; x++)
        {
            for (var y = 480 - 52; y < 480 - 22; y++)
            {
                var c = read.GetPixel(x, y);
                if (c.r > 0.9f && c.g > 0.9f && c.b > 0.9f) white++;
            }
        }
        Assert.Greater(white, 20, "the Hud's text is drawn at the top left");
        // The render target, shown at the top right with its rows from the top: yellow at its top left, green
        // elsewhere. Unity's rows here are from the bottom
        var yellow = new Color(1f, 1f, 0f);
        var green = new Color(0f, 0.6f, 0f);
        Color Screen(int x, int y) => read.GetPixel(x, 480 - 1 - y);
        Assert.IsTrue(Near(Screen(640 - 80 + 8, 16 + 4), yellow, 0.03f), $"the target's top left is yellow: {Screen(640 - 80 + 8, 16 + 4)}");
        Assert.IsTrue(Near(Screen(640 - 80 + 8, 16 + 50), green, 0.03f), $"its bottom left green: {Screen(640 - 80 + 8, 16 + 50)}");
        Assert.IsTrue(Near(Screen(640 - 80 + 50, 16 + 4), green, 0.03f), $"its top right green: {Screen(640 - 80 + 50, 16 + 4)}");
        // Stencil: the square is cyan inside the circle alone
        var cyan = new Color(0f, 1f, 1f);
        Assert.IsTrue(Near(Screen(640 - 80, 480 - 80), cyan, 0.03f), $"the square shows inside the circle: {Screen(640 - 80, 480 - 80)}");
        Assert.IsFalse(Near(Screen(640 - 80 - 36, 480 - 80 - 36), cyan, 0.3f), "and not at its corners, outside it");
        // The multisampled target's depth, resolved: the cube nearer than the far limit in its middle, the far limit
        // at its corners
        var middleDepth = Screen(48, 480 - 16 - 32);
        var cornerDepth = Screen(18, 480 - 16 - 62);
        Assert.Less(middleDepth.r, 0.95f, $"the cube's depth shows in the middle: {middleDepth}");
        Assert.Greater(cornerDepth.r, 0.97f, $"and the far limit at the corner: {cornerDepth}");
        camera.targetTexture = null;
        Object.Destroy(target);
    }

    // An editor scene view shows the frame's 3D through its own camera (tide_embed_render_view): the cube over the ball
    // where that camera sees it, and none of the 2D (the Hud, the render target, the stencil square).
    [UnityTest]
    public IEnumerator SceneViewsShowTidesThreeD()
    {
        if (SystemInfo.graphicsDeviceType == UnityEngine.Rendering.GraphicsDeviceType.Null)
        {
            Assert.Ignore("no GPU in this run (-nographics)");
            yield break;
        }
        SceneManager.LoadScene(Bounce);
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        yield return Connected(runner);
        TideEntityView ball = null;
        yield return Until(() =>
        {
            foreach (var view in Object.FindObjectsByType<TideEntityView>())
            {
                if (view.Mine && view.GetComponent<Tide.Game.Ball>() != null) ball = view;
            }
            return ball != null;
        }, 10f);
        Assert.IsNotNull(ball, "the player's ball is shown");
        yield return Until(() => false, 2.5f);
        // A camera as a scene view's, looking down at the ball from above and to the side
        var scene = new GameObject("Scene View Camera").AddComponent<Camera>();
        scene.cameraType = CameraType.SceneView;
        scene.enabled = false;
        scene.clearFlags = CameraClearFlags.SolidColor;
        scene.backgroundColor = Color.black;
        scene.transform.position = ball.transform.position + new Vector3(4f, 6f, -4f);
        scene.transform.LookAt(ball.transform.position + new Vector3(0, 1.2f, 0));
        var target = new RenderTexture(320, 240, 24);
        scene.targetTexture = target;
        scene.Render();
        var read = new Texture2D(320, 240, TextureFormat.RGBA32, false);
        RenderTexture.active = target;
        read.ReadPixels(new Rect(0, 0, 320, 240), 0, 0);
        read.Apply();
        RenderTexture.active = null;
        var dump = System.Environment.GetEnvironmentVariable("TIDE_UNITY_SCENE_DUMP");
        if (!string.IsNullOrEmpty(dump)) System.IO.File.WriteAllBytes(dump, read.EncodeToPNG());
        var live = ball.GetComponent<Tide.Game.Ball>();
        var p = scene.WorldToScreenPoint(ball.transform.position + new Vector3(0, 1.2f, 0));
        var cube = read.GetPixel(Mathf.RoundToInt(p.x), Mathf.RoundToInt(p.y));
        Assert.IsTrue(Near(cube, live.color, 0.03f), $"the cube over the ball, where the scene view sees it: {cube} for {live.color}");
        var white = 0;
        for (var x = 0; x < 120; x++)
        {
            for (var y = 240 - 60; y < 240; y++)
            {
                var c = read.GetPixel(x, y);
                if (c.r > 0.9f && c.g > 0.9f && c.b > 0.9f) white++;
            }
        }
        Assert.AreEqual(0, white, "and no Hud text");
        scene.targetTexture = null;
        Object.Destroy(target);
        Object.Destroy(scene.gameObject);
    }

    [UnityTest]
    public IEnumerator TheMatchShowsItsBall()
    {
        SceneManager.LoadScene("Assets/Sample/Bounce.unity");
        yield return null;
        var runner = Object.FindAnyObjectByType<TideRunner>();
        Assert.IsNotNull(runner, "the scene has its TideRunner");
        TideEntityView ball = null;
        for (var frame = 0; frame < 300 && ball == null; frame++)
        {
            yield return null;
            foreach (var view in Object.FindObjectsByType<TideEntityView>())
            {
                if (view.GetComponent<Tide.Game.Ball>() != null) ball = view;
            }
        }
        Assert.IsTrue(runner.Running, "the game started");
        Assert.AreEqual(TideSessionState.Connected, runner.State);
        Assert.IsNotNull(ball, "the player's ball is shown");
        Assert.IsTrue(ball.Mine, "and it's this machine's");
        // Its Ball component is the entity's in Play: the color the match gave it, as the packet has it
        var live = ball.GetComponent<Tide.Game.Ball>();
        Assert.Greater(live.color.r + live.color.g + live.color.b, 0.1f, "with the color the match gave it");
        Assert.IsTrue(ball.TryGet(out Tide.Game.Ball.Value value), "and its value, read through the generated layout");
        Assert.AreEqual(value.color, live.color);
        // Dropped from 2 units up, the ball falls onto the floor and stays on it: Tide's clock is the frames' real
        // lengths, which a run in batch mode makes short, so this waits by time
        var pillars = GameObject.Find("Pillars").transform;
        var first = pillars.GetChild(0).rotation;
        var second = pillars.GetChild(1).rotation;
        var until = Time.realtimeSinceStartup + 3f;
        while (Time.realtimeSinceStartup < until) yield return null;
        var y = ball.transform.position.y;
        Assert.That(y, Is.InRange(0.45f, 0.6f), "it fell to the floor: " + y);
        Assert.Greater(Tide.Game.TideGameSchema.Hash.Length, 0, "the bindings were made from the game's schema");
        Assert.IsTrue(DrewText(runner, "Balls: 1"), "the draw packet has the Hud's text");
        // The Ring view's ring is where Unity's camera sees the ball: Tide's views go through Unity's camera
        Assert.IsTrue(Ringed(runner, out var ring), "the draw packet has the ball's ring");
        var seen = Camera.main.WorldToScreenPoint(ball.transform.position);
        var expected = new Vector2(seen.x, Screen.height - seen.y); // Tide's screen is from the top left, y down
        Assert.Less(Vector2.Distance(ring, expected), 1.5f, $"the ring at {ring} is around the ball at {expected}");
        for (var i = 0; i < 2; i++)
        {
            var pillar = pillars.GetChild(i);
            var view = pillar.GetComponent<TideEntityView>();
            Assert.IsNotNull(view, $"pillar {i} shows an entity");
            var spinner = view.GetComponent<Tide.Game.Spinner>();
            Assert.IsNotNull(spinner, $"pillar {i} has its Spinner");
            Assert.AreEqual(i == 0 ? 1.5f : -0.75f, spinner.speed, 1e-6f, "with the speed the scene gave it");
            Assert.AreEqual('S', spinner.badge, "and its char's default, its code point");
            Assert.AreEqual(i == 0 ? -3.5f : 3.5f, pillar.position.x, 1e-3f, "placed as Unity has it, under Pillars");
            Assert.AreEqual(3.5f, pillar.position.z, 1e-3f);
            Assert.Greater(Quaternion.Angle(i == 0 ? first : second, pillar.rotation), 10f, $"pillar {i} turned");
        }
    }
}
