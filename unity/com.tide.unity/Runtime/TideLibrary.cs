using System;
using System.IO;
using System.Runtime.InteropServices;
using UnityEngine;

namespace Tide
{
    // The embed's functions (tide/embed.h), from the library tide built for
    // this project. In the editor that's Library/Tide/tide_embed, loaded by
    // hand (a copy of it, so tide can write a new one while Unity runs), which
    // then loads the game's libraries tide builds beside it and swaps each new
    // one in. Called through function pointers: nothing is locked by DllImport.
    public sealed unsafe class TideLibrary : IDisposable
    {
        IntPtr handle;

#if UNITY_EDITOR
        // The editor explains which compiler was selected, including a developer override.
        public static Func<string> AbiMismatchHint;
#endif

        delegate* unmanaged[Cdecl]<TideEmbedDesc*, IntPtr> createFn;
        delegate* unmanaged[Cdecl]<IntPtr, void> destroyFn;
        delegate* unmanaged[Cdecl]<IntPtr, float, TideEmbedInput*, float, float, float, double> frameFn;
        delegate* unmanaged[Cdecl]<IntPtr, TideEmbedStatus*, void> statusFn;
        delegate* unmanaged[Cdecl]<IntPtr, uint, byte*, uint, byte> requestFn;
        delegate* unmanaged[Cdecl]<IntPtr, byte, void> backgroundFn;
        delegate* unmanaged[Cdecl]<IntPtr, float*, float*, void> cameraFn;
        delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*> renderFn;
        delegate* unmanaged[Cdecl]<IntPtr, void> renderResetFn;
        delegate* unmanaged[Cdecl]<IntPtr, float*, float*, int, int, uint*, byte*> renderViewFn;
        delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*> viewFn;
        delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*> schemaFn;
        delegate* unmanaged[Cdecl]<IntPtr, uint, uint, uint, void*, uint, byte> commandFn;
        delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*> drawFn;
        delegate* unmanaged[Cdecl]<IntPtr, float*, uint, uint, uint, void> audioFn;
        delegate* unmanaged[Cdecl]<IntPtr, float*, uint, uint, uint, void> microphoneFn;
        delegate* unmanaged[Cdecl]<IntPtr, TideTravel*, void> travelFn;
        delegate* unmanaged[Cdecl]<IntPtr, float, void> travelKeepFn;
        delegate* unmanaged[Cdecl]<IntPtr, byte, byte> pauseFn;
        delegate* unmanaged[Cdecl]<IntPtr, byte> stepFn;
        delegate* unmanaged[Cdecl]<IntPtr, uint, byte> showFn;
        delegate* unmanaged[Cdecl]<IntPtr, byte> continueFn;
        delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*> recordingFn;
        delegate* unmanaged[Cdecl]<IntPtr, byte*, uint, byte> replayFn;
        delegate* unmanaged[Cdecl]<IntPtr, void> replayEndFn;
        delegate* unmanaged[Cdecl]<uint> abiFn;

        // What tide/embed.h's TIDE_EMBED_ABI says: a library that says otherwise was built by another tide.
        public const uint Abi = 8;

        public static string LibraryName =>
            Application.platform == RuntimePlatform.WindowsEditor || Application.platform == RuntimePlatform.WindowsPlayer
                ? "tide_embed.dll"
                : Application.platform == RuntimePlatform.OSXEditor || Application.platform == RuntimePlatform.OSXPlayer
                    ? "libtide_embed.dylib"
                    : "libtide_embed.so";

        // The suffix of a library on this machine, as tide names the game's.
        public static string LibrarySuffix => Path.GetExtension(LibraryName);

        // The folder of what tide builds for the project: in the editor Library/Tide, beside Assets, where a new
        // build of the game is swapped in as it comes; in a player, the build's StreamingAssets/Tide, where Tide's
        // build processor put the embed, the game's library ("game") and its pack.
        public static string BuildFolder => Application.isEditor
            ? Path.Combine(Path.GetDirectoryName(Application.dataPath) ?? ".", "Library", "Tide")
            : Path.Combine(Application.streamingAssetsPath, "Tide");

        // A player's game, linked in by hand: tide_host_library of its library, and its pack's bytes (unmanaged,
        // for the process's life, as the library is). Zero in the editor, whose embed loads the game's builds itself.
        public IntPtr Game { get; private set; }
        public IntPtr Pack { get; private set; }
        public int PackSize { get; private set; }

        // Opens the embed tide built in `folder`, or null after saying why in the Console.
        public static TideLibrary Open(string folder)
        {
#if UNITY_WEBGL && !UNITY_EDITOR
            return OpenWeb();
#else
            var built = Path.Combine(folder, LibraryName);
            if (!File.Exists(built))
            {
                Debug.LogError(Application.isEditor
                    ? $"Tide: {built} isn't there yet: tide builds it while the editor is open (Project Settings > Tide), and says what's wrong in the Console."
                    : $"Tide: {built} isn't in this build: Tide's build processor puts it there as the player builds.");
                return null;
            }
            var load = built;
            if (Application.isEditor)
            {
                // A copy of its own, named for when it was built, so tide can write the next one meanwhile
                var loaded = Path.Combine(folder, "loaded");
                Directory.CreateDirectory(loaded);
                var stamp = File.GetLastWriteTimeUtc(built).Ticks;
                load = Path.Combine(loaded, Path.GetFileNameWithoutExtension(LibraryName) + "-" + stamp + LibrarySuffix);
                try
                {
                    if (!File.Exists(load)) File.Copy(built, load);
                }
                catch (IOException e)
                {
                    Debug.LogError($"Tide: can't copy {built} to load it: {e.Message}");
                    return null;
                }
            }
            var handle = NativeLoader.Load(load, out var error);
            if (handle == IntPtr.Zero)
            {
                Debug.LogError($"Tide: can't load {load}: {error}");
                return null;
            }
            var library = new TideLibrary { handle = handle };
            if (!library.Bind() || (!Application.isEditor && !library.LinkGame(folder)))
            {
                library.Dispose();
                return null;
            }
            return library;
#endif
        }

        // A player's: the game's library and its pack, from `folder`.
        bool LinkGame(string folder)
        {
            var path = Path.Combine(folder, "game" + LibrarySuffix);
            var game = NativeLoader.Load(path, out var error);
            if (game == IntPtr.Zero)
            {
                Debug.LogError($"Tide: can't load {path}: {error}");
                return false;
            }
            var entry = (delegate* unmanaged[Cdecl]<IntPtr>)NativeLoader.Find(game, "tide_host_library");
            Game = entry != null ? entry() : IntPtr.Zero;
            if (Game == IntPtr.Zero)
            {
                Debug.LogError($"Tide: {path} isn't a game's library: it has no tide_host_library.");
                return false;
            }
            var pack = Path.Combine(folder, "pack");
            if (File.Exists(pack))
            {
                var bytes = File.ReadAllBytes(pack);
                Pack = Marshal.AllocHGlobal(Math.Max(1, bytes.Length));
                Marshal.Copy(bytes, 0, Pack, bytes.Length);
                PackSize = bytes.Length;
            }
            return true;
        }

        bool Bind()
        {
            abiFn = (delegate* unmanaged[Cdecl]<uint>)Find("tide_embed_abi");
            if (abiFn == null || abiFn() != Abi)
            {
                var hint = "";
#if UNITY_EDITOR
                hint = AbiMismatchHint?.Invoke() ?? "";
#endif
                Debug.LogError($"Tide: {LibraryName} was built by a tide this package doesn't match (its ABI is {(abiFn == null ? 0 : abiFn())}, the package's {Abi}): update the package or tide so they agree.{hint}");
                return false;
            }
            createFn = (delegate* unmanaged[Cdecl]<TideEmbedDesc*, IntPtr>)Find("tide_embed_create");
            destroyFn = (delegate* unmanaged[Cdecl]<IntPtr, void>)Find("tide_embed_destroy");
            frameFn = (delegate* unmanaged[Cdecl]<IntPtr, float, TideEmbedInput*, float, float, float, double>)Find("tide_embed_frame");
            statusFn = (delegate* unmanaged[Cdecl]<IntPtr, TideEmbedStatus*, void>)Find("tide_embed_status_of");
            requestFn = (delegate* unmanaged[Cdecl]<IntPtr, uint, byte*, uint, byte>)Find("tide_embed_request");
            backgroundFn = (delegate* unmanaged[Cdecl]<IntPtr, byte, void>)Find("tide_embed_background");
            cameraFn = (delegate* unmanaged[Cdecl]<IntPtr, float*, float*, void>)Find("tide_embed_camera");
            renderFn = (delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*>)Find("tide_embed_render");
            renderResetFn = (delegate* unmanaged[Cdecl]<IntPtr, void>)Find("tide_embed_render_reset");
            renderViewFn = (delegate* unmanaged[Cdecl]<IntPtr, float*, float*, int, int, uint*, byte*>)Find("tide_embed_render_view");
            viewFn = (delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*>)Find("tide_embed_view");
            schemaFn = (delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*>)Find("tide_embed_schema");
            commandFn = (delegate* unmanaged[Cdecl]<IntPtr, uint, uint, uint, void*, uint, byte>)Find("tide_embed_command");
            drawFn = (delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*>)Find("tide_embed_draw");
            audioFn = (delegate* unmanaged[Cdecl]<IntPtr, float*, uint, uint, uint, void>)Find("tide_embed_audio");
            microphoneFn = (delegate* unmanaged[Cdecl]<IntPtr, float*, uint, uint, uint, void>)Find("tide_embed_microphone");
            travelFn = (delegate* unmanaged[Cdecl]<IntPtr, TideTravel*, void>)Find("tide_embed_travel_of");
            travelKeepFn = (delegate* unmanaged[Cdecl]<IntPtr, float, void>)Find("tide_embed_travel_keep");
            pauseFn = (delegate* unmanaged[Cdecl]<IntPtr, byte, byte>)Find("tide_embed_pause");
            stepFn = (delegate* unmanaged[Cdecl]<IntPtr, byte>)Find("tide_embed_step");
            showFn = (delegate* unmanaged[Cdecl]<IntPtr, uint, byte>)Find("tide_embed_show");
            continueFn = (delegate* unmanaged[Cdecl]<IntPtr, byte>)Find("tide_embed_continue");
            recordingFn = (delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*>)Find("tide_embed_recording");
            replayFn = (delegate* unmanaged[Cdecl]<IntPtr, byte*, uint, byte>)Find("tide_embed_replay");
            replayEndFn = (delegate* unmanaged[Cdecl]<IntPtr, void>)Find("tide_embed_replay_end");
            if (createFn == null || destroyFn == null || frameFn == null || statusFn == null || requestFn == null || backgroundFn == null
                || cameraFn == null || renderFn == null || renderResetFn == null || renderViewFn == null
                || viewFn == null || schemaFn == null || commandFn == null || drawFn == null || audioFn == null || microphoneFn == null
                || travelFn == null || travelKeepFn == null || pauseFn == null || stepFn == null || showFn == null
                || continueFn == null || recordingFn == null || replayFn == null || replayEndFn == null)
            {
                Debug.LogError($"Tide: {LibraryName} lacks some of tide/embed.h's functions: rebuild it with this tide.");
                return false;
            }
            return true;
        }

        IntPtr Find(string name)
        {
            return NativeLoader.Find(handle, name);
        }

        // tide/embed.h's calls. On the web they go to Tide's own program beside Unity's (Tide.jslib), which copies
        // input in and the packet, schema and status out.
#if UNITY_WEBGL && !UNITY_EDITOR
        internal IntPtr Create(TideEmbedDesc* desc) => TideWebCreate(desc);
        public void Destroy(IntPtr embed) => TideWebDestroy();
        public double Frame(IntPtr embed, float seconds, TideEmbedInput* input, float width, float height, float scale) =>
            TideWebFrame(seconds, input, sizeof(TideEmbedInput), width, height, scale);
        public void Status(IntPtr embed, TideEmbedStatus* status) => TideWebStatus(status, sizeof(TideEmbedStatus));
        public byte Request(IntPtr embed, uint kind, byte* text, uint port) => (byte)TideWebRequest(kind, text, port);
        public void Background(IntPtr embed, byte hidden) => TideWebBackground(hidden);
        public void Camera(IntPtr embed, Matrix4x4* transform, Matrix4x4* projection)
        {
            var matrices = stackalloc Matrix4x4[2];
            if (transform != null && projection != null)
            {
                matrices[0] = *transform;
                matrices[1] = *projection;
            }
            TideWebCamera((float*)matrices, transform != null && projection != null ? 1 : 0);
        }
        public byte* Render(IntPtr embed, uint* size) => TideWebRender(size);
        public void RenderReset(IntPtr embed) => TideWebRenderReset();
        public byte* RenderView(IntPtr embed, Matrix4x4* transform, Matrix4x4* projection, int width, int height, uint* size)
        {
            *size = 0;
            return null;
        }
        public byte* View(IntPtr embed, uint* size) => TideWebView(size);
        public byte* Schema(IntPtr embed, uint* size) => TideWebSchema(size);
        public byte* Draw(IntPtr embed, uint* size) => TideWebDraw(size);
        public byte Command(IntPtr embed, uint type, uint index, uint generation, void* value, uint size) =>
            (byte)TideWebCommand(type, index, generation, value, size);
        // Tide's program plays through the page, and hears its microphone, itself
        public void Audio(IntPtr embed, float* samples, uint frames, uint rate, uint channels)
        {
        }
        public void Microphone(IntPtr embed, float* samples, uint frames, uint rate, uint channels)
        {
        }
        public void Travel(IntPtr embed, TideTravel* travel) => TideWebTravel(travel, sizeof(TideTravel));
        public void TravelKeep(IntPtr embed, float seconds) => TideWebTravelKeep(seconds);
        public byte Pause(IntPtr embed, byte paused) => (byte)TideWebPause(paused);
        public byte Step(IntPtr embed) => (byte)TideWebStep();
        public byte Show(IntPtr embed, uint tick) => (byte)TideWebShow(tick);
        public byte Continue(IntPtr embed) => (byte)TideWebContinue();
        public byte* Recording(IntPtr embed, uint* size) => TideWebRecording(size);
        public byte Replay(IntPtr embed, byte* data, uint size) => (byte)TideWebReplay(data, size);
        public void ReplayEnd(IntPtr embed) => TideWebReplayEnd();

        [DllImport("__Internal")] static extern IntPtr TideWebCreate(TideEmbedDesc* desc);
        [DllImport("__Internal")] static extern void TideWebDestroy();
        [DllImport("__Internal")] static extern double TideWebFrame(float seconds, TideEmbedInput* input, int size, float width, float height, float scale);
        [DllImport("__Internal")] static extern void TideWebStatus(TideEmbedStatus* status, int size);
        [DllImport("__Internal")] static extern int TideWebRequest(uint kind, byte* text, uint port);
        [DllImport("__Internal")] static extern void TideWebBackground(byte hidden);
        [DllImport("__Internal")] static extern void TideWebCamera(float* matrices, int given);
        [DllImport("__Internal")] static extern byte* TideWebView(uint* size);
        [DllImport("__Internal")] static extern byte* TideWebSchema(uint* size);
        [DllImport("__Internal")] static extern byte* TideWebDraw(uint* size);
        [DllImport("__Internal")] static extern byte* TideWebRender(uint* size);
        [DllImport("__Internal")] static extern void TideWebRenderReset();
        [DllImport("__Internal")] static extern int TideWebCommand(uint type, uint index, uint generation, void* value, uint size);
        [DllImport("__Internal")] static extern void TideWebTravel(TideTravel* travel, int size);
        [DllImport("__Internal")] static extern void TideWebTravelKeep(float seconds);
        [DllImport("__Internal")] static extern int TideWebPause(byte paused);
        [DllImport("__Internal")] static extern int TideWebStep();
        [DllImport("__Internal")] static extern int TideWebShow(uint tick);
        [DllImport("__Internal")] static extern int TideWebContinue();
        [DllImport("__Internal")] static extern byte* TideWebRecording(uint* size);
        [DllImport("__Internal")] static extern int TideWebReplay(byte* data, uint size);
        [DllImport("__Internal")] static extern void TideWebReplayEnd();

        // Tide's program on the page: it loads beside Unity's (TideWebBuild put it in the page), and starts when
        // TideRunner does.
        static TideLibrary OpenWeb() => new TideLibrary();
#else
        internal IntPtr Create(TideEmbedDesc* desc) => createFn(desc);
        public void Destroy(IntPtr embed) => destroyFn(embed);
        public double Frame(IntPtr embed, float seconds, TideEmbedInput* input, float width, float height, float scale) =>
            frameFn(embed, seconds, input, width, height, scale);
        public void Status(IntPtr embed, TideEmbedStatus* status) => statusFn(embed, status);
        public byte Request(IntPtr embed, uint kind, byte* text, uint port) => requestFn(embed, kind, text, port);
        public void Background(IntPtr embed, byte hidden) => backgroundFn(embed, hidden);
        public void Camera(IntPtr embed, Matrix4x4* transform, Matrix4x4* projection) =>
            cameraFn(embed, (float*)transform, (float*)projection);
        public byte* Render(IntPtr embed, uint* size) => renderFn(embed, size);
        public void RenderReset(IntPtr embed) => renderResetFn(embed);
        public byte* RenderView(IntPtr embed, Matrix4x4* transform, Matrix4x4* projection, int width, int height, uint* size) =>
            renderViewFn(embed, (float*)transform, (float*)projection, width, height, size);
        public byte* View(IntPtr embed, uint* size) => viewFn(embed, size);
        public byte* Schema(IntPtr embed, uint* size) => schemaFn(embed, size);
        public byte* Draw(IntPtr embed, uint* size) => drawFn(embed, size);
        public byte Command(IntPtr embed, uint type, uint index, uint generation, void* value, uint size) =>
            commandFn(embed, type, index, generation, value, size);
        public void Audio(IntPtr embed, float* samples, uint frames, uint rate, uint channels) =>
            audioFn(embed, samples, frames, rate, channels);
        public void Microphone(IntPtr embed, float* samples, uint frames, uint rate, uint channels) =>
            microphoneFn(embed, samples, frames, rate, channels);
        public void Travel(IntPtr embed, TideTravel* travel) => travelFn(embed, travel);
        public void TravelKeep(IntPtr embed, float seconds) => travelKeepFn(embed, seconds);
        public byte Pause(IntPtr embed, byte paused) => pauseFn(embed, paused);
        public byte Step(IntPtr embed) => stepFn(embed);
        public byte Show(IntPtr embed, uint tick) => showFn(embed, tick);
        public byte Continue(IntPtr embed) => continueFn(embed);
        public byte* Recording(IntPtr embed, uint* size) => recordingFn(embed, size);
        public byte Replay(IntPtr embed, byte* data, uint size) => replayFn(embed, data, size);
        public void ReplayEnd(IntPtr embed) => replayEndFn(embed);
#endif

        // The library stays loaded for the editor's life: a game running in it may hold memory and threads of it.
        public void Dispose()
        {
            handle = IntPtr.Zero;
        }
    }

    // LoadLibrary and dlopen, which Unity's Mono has no wrapper of.
    static class NativeLoader
    {
        public static IntPtr Load(string path, out string error)
        {
            error = null;
            IntPtr handle;
            if (Application.platform == RuntimePlatform.WindowsEditor || Application.platform == RuntimePlatform.WindowsPlayer)
            {
                handle = LoadLibraryW(path);
                if (handle == IntPtr.Zero) error = "error " + Marshal.GetLastWin32Error();
                return handle;
            }
            handle = Application.platform == RuntimePlatform.OSXEditor || Application.platform == RuntimePlatform.OSXPlayer
                ? MacDlopen(path, 2) // RTLD_NOW
                : LinuxDlopen(path, 2);
            if (handle == IntPtr.Zero) error = DlError();
            return handle;
        }

        public static IntPtr Find(IntPtr handle, string name)
        {
            if (handle == IntPtr.Zero) return IntPtr.Zero;
            if (Application.platform == RuntimePlatform.WindowsEditor || Application.platform == RuntimePlatform.WindowsPlayer)
                return GetProcAddress(handle, name);
            return Application.platform == RuntimePlatform.OSXEditor || Application.platform == RuntimePlatform.OSXPlayer
                ? MacDlsym(handle, name)
                : LinuxDlsym(handle, name);
        }

        static string DlError()
        {
            var e = Application.platform == RuntimePlatform.OSXEditor || Application.platform == RuntimePlatform.OSXPlayer
                ? MacDlerror()
                : LinuxDlerror();
            return e == IntPtr.Zero ? "unknown error" : Marshal.PtrToStringAnsi(e);
        }

        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr LoadLibraryW(string path);

        [DllImport("kernel32", CharSet = CharSet.Ansi, ExactSpelling = true)]
        static extern IntPtr GetProcAddress(IntPtr module, string name);

        [DllImport("/usr/lib/libSystem.B.dylib", EntryPoint = "dlopen")]
        static extern IntPtr MacDlopen(string path, int flags);

        [DllImport("/usr/lib/libSystem.B.dylib", EntryPoint = "dlsym")]
        static extern IntPtr MacDlsym(IntPtr handle, string name);

        [DllImport("/usr/lib/libSystem.B.dylib", EntryPoint = "dlerror")]
        static extern IntPtr MacDlerror();

        [DllImport("libdl.so.2", EntryPoint = "dlopen")]
        static extern IntPtr LinuxDlopen(string path, int flags);

        [DllImport("libdl.so.2", EntryPoint = "dlsym")]
        static extern IntPtr LinuxDlsym(IntPtr handle, string name);

        [DllImport("libdl.so.2", EntryPoint = "dlerror")]
        static extern IntPtr LinuxDlerror();
    }
}
