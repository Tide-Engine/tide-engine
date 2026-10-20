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

        internal delegate* unmanaged[Cdecl]<TideEmbedDesc*, IntPtr> create;
        internal delegate* unmanaged[Cdecl]<IntPtr, void> destroy;
        internal delegate* unmanaged[Cdecl]<IntPtr, float, TideEmbedInput*, float, float, float, double> frame;
        internal delegate* unmanaged[Cdecl]<IntPtr, TideEmbedStatus*, void> status;
        internal delegate* unmanaged[Cdecl]<IntPtr, uint, byte*, uint, byte> request;
        internal delegate* unmanaged[Cdecl]<IntPtr, byte, void> background;
        internal delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*> view;
        internal delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*> schema;
        internal delegate* unmanaged[Cdecl]<IntPtr, uint, uint, uint, void*, uint, byte> command;
        internal delegate* unmanaged[Cdecl]<uint> abi;

        // What tide/embed.h's TIDE_EMBED_ABI says: a library that says otherwise was built by another tide.
        public const uint Abi = 1;

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
            abi = (delegate* unmanaged[Cdecl]<uint>)Find("tide_embed_abi");
            if (abi == null || abi() != Abi)
            {
                Debug.LogError($"Tide: {LibraryName} was built by a tide this package doesn't match (its ABI is {(abi == null ? 0 : abi())}, the package's {Abi}): update the package or tide so they agree.");
                return false;
            }
            create = (delegate* unmanaged[Cdecl]<TideEmbedDesc*, IntPtr>)Find("tide_embed_create");
            destroy = (delegate* unmanaged[Cdecl]<IntPtr, void>)Find("tide_embed_destroy");
            frame = (delegate* unmanaged[Cdecl]<IntPtr, float, TideEmbedInput*, float, float, float, double>)Find("tide_embed_frame");
            status = (delegate* unmanaged[Cdecl]<IntPtr, TideEmbedStatus*, void>)Find("tide_embed_status_of");
            request = (delegate* unmanaged[Cdecl]<IntPtr, uint, byte*, uint, byte>)Find("tide_embed_request");
            background = (delegate* unmanaged[Cdecl]<IntPtr, byte, void>)Find("tide_embed_background");
            view = (delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*>)Find("tide_embed_view");
            schema = (delegate* unmanaged[Cdecl]<IntPtr, uint*, byte*>)Find("tide_embed_schema");
            command = (delegate* unmanaged[Cdecl]<IntPtr, uint, uint, uint, void*, uint, byte>)Find("tide_embed_command");
            if (create == null || destroy == null || frame == null || status == null || request == null || background == null
                || view == null || schema == null || command == null)
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
