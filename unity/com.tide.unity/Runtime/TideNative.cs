using System;
using System.Runtime.InteropServices;

namespace Tide
{
    // tide/embed.h, laid out byte for byte: both structs have no padding the C
    // compiler adds, and tide/embed.h asserts their sizes.

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct TideEmbedTouch
    {
        public uint phase;  // 0 began, 1 moved, 2 ended, 3 canceled
        public uint source; // The system's number for the finger while it touches
        public float x, y;  // Logical pixels from the top left, y down
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct TideEmbedInput
    {
        public const int TextMax = 32;
        public const int TouchMax = 32;

        public fixed byte keys[16]; // Held keys, a bit each, in Tide's keyboard order (TideInputReader.Keys)
        public float mouseX, mouseY; // Logical pixels from the top left, y down
        public float wheelX, wheelY; // This frame's scroll, y positive away from the user
        public float movedX, movedY; // This frame's movement while the cursor is locked, y down
        public uint mouseButtons;    // Left, right, middle, back, forward
        public byte mouseLocked;
        public byte gamepad;
        public byte touchscreen;
        public byte pad;
        public float leftX, leftY, rightX, rightY; // Sticks, y down as gamepads report it
        public float leftTrigger, rightTrigger;
        public uint gamepadButtons; // tide/devices.h's order: tide_gamepad's buttons, the d-pad's after
        public uint textCount;
        public fixed uint text[TextMax];
        public uint touchCount;
        public fixed byte touches[TouchMax * 16]; // TideEmbedTouch each

        public const int Size = 724;

        // Holds the key named as the Input System names it ("a", "space", "leftArrow": TideInputReader.Keys), or lets
        // it go: false for a name Tide's keyboard hasn't.
        public bool Hold(string key, bool held = true)
        {
            var i = System.Array.IndexOf(TideInputReader.Keys, key);
            if (i < 0) return false;
            if (held) keys[i / 8] |= (byte)(1 << (i % 8));
            else keys[i / 8] &= (byte)~(1 << (i % 8));
            return true;
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct TideEmbedStatus
    {
        public uint state;          // TideSessionState
        public uint reason;         // Why it's offline again
        public uint player;         // This machine's player: its index + 1, 0 for none
        public uint pingMs;
        public uint verifiedTick;
        public uint predictedTick;
        public uint delayTicks;
        public uint resyncs;
        public ulong sentBytes;
        public ulong receivedBytes;
        public byte server;
        public byte open;
        public byte typing;
        public byte cursorLocked;
        public fixed byte room[64];
        public fixed byte message[256];
        public byte microphone;     // The game listens to the microphone (Clip.Microphone)
        public fixed byte pad[3];

        public const int Size = 376;
    }

    // tide_embed_travel_state: what time travel is doing (TideRunner.Travel)
    public enum TideTravelState : uint
    {
        Off = 0,     // Nothing kept: TideRunner.timeTravel is off
        None = 1,    // No match it can pause (TideTravel.Why says why), and no recording shown
        Live = 2,    // The match runs, recorded
        Paused = 3,  // Paused, showing TideTravel.shown; with `background`, the match goes on behind it
        Replay = 4,  // Showing a recording; the match, if any, is paused
    }

    // tide_embed_travel: where time travel is. Tick n is the world after n ticks.
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct TideTravel
    {
        public TideTravelState state;
        public uint first;      // The first tick that shows
        public uint kept;       // ...the first that shows at once (the ones before are made again from the start)
        public uint last;       // ...and the last
        public uint shown;      // The tick shown
        public uint tickRate;
        public uint checkedTo;  // Replaying: ticks made again and checked so far
        public uint differs;    // ...the first whose world wasn't the recording's (0: none)
        public uint unreadable; // ...the first this build couldn't replay (0: none)
        public byte playing;    // Replaying: it plays at the tick rate
        public byte otherBuild; // The recording was made by a build whose simulation code was another
        public byte background; // Other machines are in the match: it goes on behind what's shown
        public byte pad;
        public ulong hash;      // The shown world's
        public ulong expected, got; // At `differs`: the recording's hash, and this build's
        public fixed byte why[128];
        public fixed byte refused[128];

        public const int Size = 320;

        // Why the match can't pause now, "" when it can
        public string Why
        {
            get { fixed (byte* p = why) return Nul(p, 128); }
        }

        // Why the last recording given to replay wasn't, "" when it was
        public string Refused
        {
            get { fixed (byte* p = refused) return Nul(p, 128); }
        }

        static string Nul(byte* bytes, int capacity)
        {
            var n = 0;
            while (n < capacity && bytes[n] != 0) n++;
            return System.Text.Encoding.UTF8.GetString(bytes, n);
        }
    }

    public enum TideSessionState : uint
    {
        Offline = 0,
        Connecting = 1,
        Connected = 2,
    }

    // tide_session_request_kind: what Session calls ask.
    public enum TideRequest : uint
    {
        Start = 1,   // Main's match
        Join = 2,    // A room, by its code
        Connect = 3, // A machine, by its address
        Leave = 4,
        Open = 5,    // Other machines can join the match this machine runs
        Close = 6,   // ...and no longer
        KickAll = 8, // With a message
        End = 9,     // The match this machine runs ends, for everyone
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TideEmbedDesc
    {
        public IntPtr game;        // const tide_host_game *: a game linked in
        public IntPtr pack;        // const uint8_t *
        public UIntPtr packSize;
        public int tickRate;
        public int argc;
        public IntPtr argv;        // char **
        public IntPtr gameName;    // const char *
        public IntPtr relay;       // const char *
        public IntPtr libraryDir;  // const char *: the folder tide writes game-<n> libraries to
    }
}
