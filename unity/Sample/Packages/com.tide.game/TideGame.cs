// Written by tide unity from the game's schema. Do not edit: it changes with the game.
#pragma warning disable
namespace Tide.Game
{
    // What this game's C# was made from: a view packet of another build isn't read by it.
    public static class TideGameSchema
    {
        public const string Hash = "FE0AA9F947BDF299";
        public const string Game = "Bounce";
        public const int TickRate = 30;
        public static readonly string[] Scenes = {  };
        public static readonly string[] Prefabs = { "UnityEngine.SceneContents.Sample.Bonus", "UnityEngine.SceneContents.Sample.Bounce", "UnityEngine.Prefabs.Sample.Roller" };
    }

    // The game's components as TideComponents, for the runner to write its entities' values into
    static class TideGameComponents
    {
        [global::UnityEngine.RuntimeInitializeOnLoadMethod(global::UnityEngine.RuntimeInitializeLoadType.SubsystemRegistration)]
        static void Register()
        {
            global::Tide.TideComponents.Register(TideGameSchema.Hash, "Owner", typeof(global::Tide.Game.Owner));
            global::Tide.TideComponents.Register(TideGameSchema.Hash, "Spinner", typeof(global::Tide.Game.Spinner));
            global::Tide.TideComponents.Register(TideGameSchema.Hash, "Ball", typeof(global::Tide.Game.Ball));
        }
    }
}
namespace Tide.Game
{
    public enum Prediction : int
    {
        Rollback = 0,
        Delay = 1,
    }
}
namespace Tide.Game
{
    public enum IOError : int
    {
        None = 0,
        NotFound = 1,
        AlreadyExists = 2,
        AccessDenied = 3,
        InvalidPath = 4,
        NotDirectory = 5,
        DirectoryNotEmpty = 6,
        OutOfSpace = 7,
        StorageUnavailable = 8,
        InvalidData = 9,
        Failed = 10,
    }
}
namespace Tide.Game
{
    public enum Anchor : int
    {
        UpperLeft = 0,
        UpperCenter = 1,
        UpperRight = 2,
        MiddleLeft = 3,
        MiddleCenter = 4,
        MiddleRight = 5,
        LowerLeft = 6,
        LowerCenter = 7,
        LowerRight = 8,
    }
}
namespace Tide.Game
{
    public enum Filter : int
    {
        Point = 0,
        Bilinear = 1,
    }
}
namespace Tide.Game
{
    public enum Blend : int
    {
        Alpha = 0,
        Add = 1,
        Multiply = 2,
        Replace = 3,
    }
}
namespace Tide.Game
{
    public enum Font : int
    {
        Regular = 0,
        Bold = 1,
    }
}
namespace Tide.Game
{
    public enum SessionState : int
    {
        Offline = 0,
        Connecting = 1,
        Connected = 2,
    }
}
namespace Tide.Game
{
    public enum DisconnectReason : int
    {
        Left = 0,
        TimedOut = 1,
        Refused = 2,
        ServerLeft = 3,
        Failed = 4,
        Ended = 5,
        Kicked = 6,
    }
}
namespace Tide.Game
{
    // Tide's struct Vertex
    [global::Tide.TideType("Vertex")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 32)]
    public unsafe partial struct Vertex
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Vector2 position;
        [global::System.Runtime.InteropServices.FieldOffset(8)] public global::UnityEngine.Vector2 uv;
        [global::System.Runtime.InteropServices.FieldOffset(16)] public global::UnityEngine.Color color;
    }
}
namespace Tide.Game
{
    // Tide's struct Vertex3
    [global::Tide.TideType("Vertex3")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 36)]
    public unsafe partial struct Vertex3
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Vector3 position;
        [global::System.Runtime.InteropServices.FieldOffset(12)] public global::UnityEngine.Vector2 uv;
        [global::System.Runtime.InteropServices.FieldOffset(20)] public global::UnityEngine.Color color;
    }
}
namespace Tide.Game
{
    // Tide's struct Shape
    [global::Tide.TideType("Shape")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 84)]
    public unsafe partial struct Shape
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Vector2 center;
        [global::System.Runtime.InteropServices.FieldOffset(8)] public global::UnityEngine.Vector2 size;
        [global::System.Runtime.InteropServices.FieldOffset(16)] public float rotation;
        [global::System.Runtime.InteropServices.FieldOffset(20)] public float radius;
        [global::System.Runtime.InteropServices.FieldOffset(24)] public bool ellipse;
        [global::System.Runtime.InteropServices.FieldOffset(28)] public global::UnityEngine.Color color;
        [global::System.Runtime.InteropServices.FieldOffset(44)] public global::UnityEngine.Color color2;
        [global::System.Runtime.InteropServices.FieldOffset(60)] public float outline;
        [global::System.Runtime.InteropServices.FieldOffset(64)] public global::UnityEngine.Color outlineColor;
        [global::System.Runtime.InteropServices.FieldOffset(80)] public float soft;
    }
}
namespace Tide.Game
{
    // Tide's struct GUITheme
    [global::Tide.TideType("GUITheme")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 132)]
    public unsafe partial struct GUITheme
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Color text;
        [global::System.Runtime.InteropServices.FieldOffset(16)] public global::UnityEngine.Color panel;
        [global::System.Runtime.InteropServices.FieldOffset(32)] public global::UnityEngine.Color control;
        [global::System.Runtime.InteropServices.FieldOffset(48)] public global::UnityEngine.Color hot;
        [global::System.Runtime.InteropServices.FieldOffset(64)] public global::UnityEngine.Color pressed;
        [global::System.Runtime.InteropServices.FieldOffset(80)] public global::UnityEngine.Color field;
        [global::System.Runtime.InteropServices.FieldOffset(96)] public global::UnityEngine.Color fieldHot;
        [global::System.Runtime.InteropServices.FieldOffset(112)] public global::UnityEngine.Color accent;
        [global::System.Runtime.InteropServices.FieldOffset(128)] public float radius;
    }
}
namespace Tide.Game
{
    // Tide's struct GUIControl
    [global::Tide.TideType("GUIControl")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 4)]
    public unsafe partial struct GUIControl
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public bool hovered;
        [global::System.Runtime.InteropServices.FieldOffset(1)] public bool pressed;
        [global::System.Runtime.InteropServices.FieldOffset(2)] public bool focused;
        [global::System.Runtime.InteropServices.FieldOffset(3)] public bool clicked;
    }
}
namespace Tide.Game
{
    // Tide's struct tide_hierarchy_node
    [global::Tide.TideType("tide_hierarchy_node")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 32)]
    public unsafe partial struct tide_hierarchy_node
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public global::Tide.TideEntityId parent;
        [global::System.Runtime.InteropServices.FieldOffset(8)] public global::Tide.TideEntityId first;
        [global::System.Runtime.InteropServices.FieldOffset(16)] public global::Tide.TideEntityId next;
        [global::System.Runtime.InteropServices.FieldOffset(24)] public global::Tide.TideEntityId previous;
    }
}
namespace Tide.Game
{
    // Tide's component Main
    [global::Tide.TideType("Main")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 1)]
    public unsafe partial struct Main
    {
    }
}
namespace Tide.Game.UnityEngine
{
    // Tide's component UnityEngine.Source
    [global::Tide.TideType("UnityEngine.Source")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 8)]
    public unsafe partial struct Source
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public int asset;
        [global::System.Runtime.InteropServices.FieldOffset(4)] public int node;
    }
}
namespace Tide.Game.UnityEngine.Scenes.Sample
{
    // Tide's component UnityEngine.Scenes.Sample.Bonus
    [global::Tide.TideType("UnityEngine.Scenes.Sample.Bonus")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 1)]
    public unsafe partial struct Bonus
    {
    }
}
namespace Tide.Game.UnityEngine.Scenes.Sample
{
    // Tide's component UnityEngine.Scenes.Sample.Bounce
    [global::Tide.TideType("UnityEngine.Scenes.Sample.Bounce")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 1)]
    public unsafe partial struct Bounce
    {
    }
}
namespace Tide.Game
{
    // Tide's component Transform
    [global::Tide.TideType("Transform")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 40)]
    public unsafe partial struct Transform
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Vector3 localPosition;
        [global::System.Runtime.InteropServices.FieldOffset(12)] public global::UnityEngine.Quaternion localRotation;
        [global::System.Runtime.InteropServices.FieldOffset(28)] public global::UnityEngine.Vector3 localScale;
    }
}
namespace Tide.Game
{
    // Tide's singleton Time
    [global::Tide.TideType("Time")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 16)]
    public unsafe partial struct Time
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public float dt;
        [global::System.Runtime.InteropServices.FieldOffset(4)] public int tick;
        [global::System.Runtime.InteropServices.FieldOffset(8)] public float t;
        [global::System.Runtime.InteropServices.FieldOffset(12)] public int tickRate;
    }
}
namespace Tide.Game
{
    // Tide's struct Session
    [global::Tide.TideType("Session")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 16)]
    public unsafe partial struct Session
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public global::Tide.Game.SessionState state;
        [global::System.Runtime.InteropServices.FieldOffset(4)] public uint player;
        [global::System.Runtime.InteropServices.FieldOffset(8)] public int ping;
        [global::System.Runtime.InteropServices.FieldOffset(12)] public bool server;
        [global::System.Runtime.InteropServices.FieldOffset(13)] public bool open;
    }
}
namespace Tide.Game
{
    // Tide's singleton Extras
    [global::Tide.TideType("Extras")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 16)]
    public unsafe partial struct Extras
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public global::Tide.TideEntityId bonus;
        [global::System.Runtime.InteropServices.FieldOffset(8)] public global::Tide.TideEntityId tossed;
    }
}
namespace Tide.Game
{
    // Tide's singleton Room
    [global::Tide.TideType("Room")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 32)]
    public unsafe partial struct Room
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public int arrivals;
        [global::System.Runtime.InteropServices.FieldOffset(8)] public fixed byte balls[24]; // Map<PlayerID, Entity>#map-order-2: its bytes
    }
}
namespace Tide.Game.UnityEngine
{
    // Tide's singleton UnityEngine.LoadedScenes
    [global::Tide.TideType("UnityEngine.LoadedScenes")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 32)]
    public unsafe partial struct LoadedScenes
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public fixed byte loaded[24]; // Map<int, Entity>#map-order-2: its bytes
        [global::System.Runtime.InteropServices.FieldOffset(24)] public uint opener;
        [global::System.Runtime.InteropServices.FieldOffset(28)] public bool heard;
        [global::System.Runtime.InteropServices.FieldOffset(29)] public bool opened;
    }
}
namespace Tide.Game
{
    // Tide's singleton tide_hierarchy
    [global::Tide.TideType("tide_hierarchy")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 24)]
    public unsafe partial struct tide_hierarchy
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public fixed byte nodes[24]; // Map<Entity, tide_hierarchy_node>#map-order-2: its bytes
    }
}
namespace Tide.Game
{
    // Tide's singleton tide_command_state
    [global::Tide.TideType("tide_command_state")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 32)]
    public unsafe partial struct tide_command_state
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public int seq0;
        [global::System.Runtime.InteropServices.FieldOffset(4)] public int seq1;
        [global::System.Runtime.InteropServices.FieldOffset(8)] public int seq2;
        [global::System.Runtime.InteropServices.FieldOffset(12)] public int seq3;
        [global::System.Runtime.InteropServices.FieldOffset(16)] public int seq4;
        [global::System.Runtime.InteropServices.FieldOffset(20)] public int seq5;
        [global::System.Runtime.InteropServices.FieldOffset(24)] public int seq6;
        [global::System.Runtime.InteropServices.FieldOffset(28)] public int seq7;
    }
}
namespace Tide.Game
{
    // Tide's struct Roll
    [global::Tide.TideType("Roll")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 12)]
    public unsafe partial struct Roll
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Vector2 move;
        [global::System.Runtime.InteropServices.FieldOffset(8)] public bool jump;
    }
}
namespace Tide.Game
{
    // Tide's command ShowBonus
    [global::Tide.TideType("ShowBonus")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 1)]
    public unsafe partial struct ShowBonus
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public bool on;
    }
}
namespace Tide.Game
{
    // Tide's command Toss
    [global::Tide.TideType("Toss")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 1)]
    public unsafe partial struct Toss
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public bool on;
    }
}
namespace Tide.Game.UnityEngine
{
    // Tide's command UnityEngine.OpenScene
    [global::Tide.TideType("UnityEngine.OpenScene")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 8)]
    public unsafe partial struct OpenScene
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public int asset;
        [global::System.Runtime.InteropServices.FieldOffset(4)] public bool only;
    }
}
