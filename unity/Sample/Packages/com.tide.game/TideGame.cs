// Written by tide unity from the game's schema. Do not edit: it changes with the game.
#pragma warning disable
namespace Tide.Game
{
    // What this game's C# was made from: a view packet of another build isn't read by it.
    public static class TideGameSchema
    {
        public const string Hash = "5B0119D514217A29";
        public const string Game = "Bounce";
        public const int TickRate = 30;
        public static readonly string[] Scenes = {  };
        public static readonly string[] Prefabs = { "UnityEngine.SceneContents.Sample.Bonus", "UnityEngine.SceneContents.Sample.Bounce", "UnityEngine.Prefabs.Sample.Roller" };
    }

    // The game's components, singletons and events as the package's classes, for the runner to read their values into
    static class TideGameTypes
    {
        [global::UnityEngine.RuntimeInitializeOnLoadMethod(global::UnityEngine.RuntimeInitializeLoadType.SubsystemRegistration)]
        static void Register()
        {
            global::Tide.TideTypes.Register(TideGameSchema.Hash, "Owner", typeof(global::Tide.Game.Owner));
            global::Tide.TideTypes.Register(TideGameSchema.Hash, "Spinner", typeof(global::Tide.Game.Spinner));
            global::Tide.TideTypes.Register(TideGameSchema.Hash, "Ball", typeof(global::Tide.Game.Ball));
            global::Tide.TideTypes.Register(TideGameSchema.Hash, "Time", typeof(global::Tide.Game.Time));
            global::Tide.TideTypes.Register(TideGameSchema.Hash, "Extras", typeof(global::Tide.Game.Extras));
            global::Tide.TideTypes.Register(TideGameSchema.Hash, "Room", typeof(global::Tide.Game.Room));
            global::Tide.TideTypes.Register(TideGameSchema.Hash, "UnityEngine.LoadedScenes", typeof(global::Tide.Game.UnityEngine.LoadedScenes));
            global::Tide.TideTypes.Register(TideGameSchema.Hash, "Bounced", typeof(global::Tide.Game.Bounced));
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
    public enum StencilTest : int
    {
        Always = 0,
        Never = 1,
        Less = 2,
        Equal = 3,
        LessEqual = 4,
        Greater = 5,
        NotEqual = 6,
        GreaterEqual = 7,
    }
}
namespace Tide.Game
{
    public enum StencilOp : int
    {
        Keep = 0,
        Replace = 1,
        Increment = 2,
        Decrement = 3,
        Zero = 4,
        Invert = 5,
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
    // Tide's struct Vertex, as Unity code has it
    [global::System.Serializable]
    public unsafe partial struct Vertex
    {
        public global::UnityEngine.Vector2 position;
        public global::UnityEngine.Vector2 uv;
        public global::UnityEngine.Color color;

        public static Vertex From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new Vertex();
            s.position = v.position;
            s.uv = v.uv;
            s.color = v.color;
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.position = position;
            v.uv = uv;
            v.color = color;
            return v;
        }

        // Tide's struct Vertex, as the game's C lays it out
        [global::Tide.TideType("Vertex")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 32)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Vector2 position;
            [global::System.Runtime.InteropServices.FieldOffset(8)] public global::UnityEngine.Vector2 uv;
            [global::System.Runtime.InteropServices.FieldOffset(16)] public global::UnityEngine.Color color;
        }
    }
}
namespace Tide.Game
{
    // Tide's struct Vertex3, as Unity code has it
    [global::System.Serializable]
    public unsafe partial struct Vertex3
    {
        public global::UnityEngine.Vector3 position;
        public global::UnityEngine.Vector2 uv;
        public global::UnityEngine.Color color;

        public static Vertex3 From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new Vertex3();
            s.position = v.position;
            s.uv = v.uv;
            s.color = v.color;
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.position = position;
            v.uv = uv;
            v.color = color;
            return v;
        }

        // Tide's struct Vertex3, as the game's C lays it out
        [global::Tide.TideType("Vertex3")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 36)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Vector3 position;
            [global::System.Runtime.InteropServices.FieldOffset(12)] public global::UnityEngine.Vector2 uv;
            [global::System.Runtime.InteropServices.FieldOffset(20)] public global::UnityEngine.Color color;
        }
    }
}
namespace Tide.Game
{
    // Tide's struct Shape, as Unity code has it
    [global::System.Serializable]
    public unsafe partial struct Shape
    {
        public global::UnityEngine.Vector2 center;
        public global::UnityEngine.Vector2 size;
        public float rotation;
        public float radius;
        public bool ellipse;
        public global::UnityEngine.Color color;
        public global::UnityEngine.Color color2;
        public float outline;
        public global::UnityEngine.Color outlineColor;
        public float soft;

        public static Shape From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new Shape();
            s.center = v.center;
            s.size = v.size;
            s.rotation = v.rotation;
            s.radius = v.radius;
            s.ellipse = v.ellipse;
            s.color = v.color;
            s.color2 = v.color2;
            s.outline = v.outline;
            s.outlineColor = v.outlineColor;
            s.soft = v.soft;
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.center = center;
            v.size = size;
            v.rotation = rotation;
            v.radius = radius;
            v.ellipse = ellipse;
            v.color = color;
            v.color2 = color2;
            v.outline = outline;
            v.outlineColor = outlineColor;
            v.soft = soft;
            return v;
        }

        // Tide's struct Shape, as the game's C lays it out
        [global::Tide.TideType("Shape")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 84)]
        public unsafe partial struct Value
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
}
namespace Tide.Game
{
    // Tide's struct GUITheme, as Unity code has it
    [global::System.Serializable]
    public unsafe partial struct GUITheme
    {
        public global::UnityEngine.Color text;
        public global::UnityEngine.Color panel;
        public global::UnityEngine.Color control;
        public global::UnityEngine.Color hot;
        public global::UnityEngine.Color pressed;
        public global::UnityEngine.Color field;
        public global::UnityEngine.Color fieldHot;
        public global::UnityEngine.Color accent;
        public float radius;

        public static GUITheme From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new GUITheme();
            s.text = v.text;
            s.panel = v.panel;
            s.control = v.control;
            s.hot = v.hot;
            s.pressed = v.pressed;
            s.field = v.field;
            s.fieldHot = v.fieldHot;
            s.accent = v.accent;
            s.radius = v.radius;
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.text = text;
            v.panel = panel;
            v.control = control;
            v.hot = hot;
            v.pressed = pressed;
            v.field = field;
            v.fieldHot = fieldHot;
            v.accent = accent;
            v.radius = radius;
            return v;
        }

        // Tide's struct GUITheme, as the game's C lays it out
        [global::Tide.TideType("GUITheme")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 132)]
        public unsafe partial struct Value
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
}
namespace Tide.Game
{
    // Tide's struct GUIControl, as Unity code has it
    [global::System.Serializable]
    public unsafe partial struct GUIControl
    {
        public bool hovered;
        public bool pressed;
        public bool focused;
        public bool clicked;
        public bool down;
        public bool up;
        public bool cancelled;
        public global::UnityEngine.Vector2 position;

        public static GUIControl From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new GUIControl();
            s.hovered = v.hovered;
            s.pressed = v.pressed;
            s.focused = v.focused;
            s.clicked = v.clicked;
            s.down = v.down;
            s.up = v.up;
            s.cancelled = v.cancelled;
            s.position = v.position;
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.hovered = hovered;
            v.pressed = pressed;
            v.focused = focused;
            v.clicked = clicked;
            v.down = down;
            v.up = up;
            v.cancelled = cancelled;
            v.position = position;
            return v;
        }

        // Tide's struct GUIControl, as the game's C lays it out
        [global::Tide.TideType("GUIControl")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 16)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public bool hovered;
            [global::System.Runtime.InteropServices.FieldOffset(1)] public bool pressed;
            [global::System.Runtime.InteropServices.FieldOffset(2)] public bool focused;
            [global::System.Runtime.InteropServices.FieldOffset(3)] public bool clicked;
            [global::System.Runtime.InteropServices.FieldOffset(4)] public bool down;
            [global::System.Runtime.InteropServices.FieldOffset(5)] public bool up;
            [global::System.Runtime.InteropServices.FieldOffset(6)] public bool cancelled;
            [global::System.Runtime.InteropServices.FieldOffset(8)] public global::UnityEngine.Vector2 position;
        }
    }
}
namespace Tide.Game
{
    // Tide's struct Trail, as Unity code has it
    /// <summary>
    /// A ball's trail, which a prefab sets in Unity's inspector as any struct field
    /// <para>Tide's struct Trail, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    [global::System.Serializable]
    public unsafe partial struct Trail
    {
        public float length;
        public global::UnityEngine.Color tint;

        public static Trail From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new Trail();
            s.length = v.length;
            s.tint = v.tint;
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.length = length;
            v.tint = tint;
            return v;
        }

        // Tide's struct Trail, as the game's C lays it out
        [global::Tide.TideType("Trail")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 20)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public float length;
            [global::System.Runtime.InteropServices.FieldOffset(4)] public global::UnityEngine.Color tint;
        }
    }
}
namespace Tide.Game
{
    // Tide's struct PlateCorner, as Unity code has it
    /// <summary>
    /// <para>Tide's struct PlateCorner, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    [global::System.Serializable]
    public unsafe partial struct PlateCorner
    {
        public global::UnityEngine.Vector3 position;
        public global::UnityEngine.Vector2 uv;

        public static PlateCorner From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new PlateCorner();
            s.position = v.position;
            s.uv = v.uv;
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.position = position;
            v.uv = uv;
            return v;
        }

        // Tide's struct PlateCorner, as the game's C lays it out
        [global::Tide.TideType("PlateCorner")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 20)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Vector3 position;
            [global::System.Runtime.InteropServices.FieldOffset(12)] public global::UnityEngine.Vector2 uv;
        }
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
    // Tide's singleton Time, as Tide's views see it this frame: TideRunner.Get (TideSingleton)
    public sealed partial class Time : global::Tide.TideSingleton
    {
        public float dt;
        public int tick;
        public float t;
        public int tickRate;

        public override string TideName => "Time";

        protected override unsafe void ReadLive(byte* values, byte* locals, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var v = *(Value*)values;
            dt = v.dt;
            tick = v.tick;
            t = v.t;
            tickRate = v.tickRate;
        }

        // Tide's singleton Time, as the game's C lays it out
        [global::Tide.TideType("Time")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 16)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public float dt;
            [global::System.Runtime.InteropServices.FieldOffset(4)] public int tick;
            [global::System.Runtime.InteropServices.FieldOffset(8)] public float t;
            [global::System.Runtime.InteropServices.FieldOffset(12)] public int tickRate;
        }
    }
}
namespace Tide.Game
{
    // Tide's struct Session, as Unity code has it
    [global::System.Serializable]
    public unsafe partial struct Session
    {
        public global::Tide.Game.SessionState state;
        public uint player;
        public int ping;
        public bool server;
        public bool open;

        public static Session From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new Session();
            s.state = v.state;
            s.player = v.player;
            s.ping = v.ping;
            s.server = v.server;
            s.open = v.open;
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.state = state;
            v.player = player;
            v.ping = ping;
            v.server = server;
            v.open = open;
            return v;
        }

        // Tide's struct Session, as the game's C lays it out
        [global::Tide.TideType("Session")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 16)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public global::Tide.Game.SessionState state;
            [global::System.Runtime.InteropServices.FieldOffset(4)] public uint player;
            [global::System.Runtime.InteropServices.FieldOffset(8)] public int ping;
            [global::System.Runtime.InteropServices.FieldOffset(12)] public bool server;
            [global::System.Runtime.InteropServices.FieldOffset(13)] public bool open;
        }
    }
}
namespace Tide.Game
{
    // Tide's singleton Extras, as Tide's views see it this frame: TideRunner.Get (TideSingleton)
    /// <summary>
    /// <para>Tide's singleton Extras, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    public sealed partial class Extras : global::Tide.TideSingleton
    {
        public global::UnityEngine.GameObject bonus;
        /// <summary>Bonus, twice</summary>
        public global::UnityEngine.GameObject again;
        public global::UnityEngine.GameObject tossed;
        public string said;
        /// <summary>Who's said something</summary>
        public global::Tide.TideSet<uint> speakers;
        /// <summary>A cell for each line said</summary>
        public global::Tide.TideGrid2<byte> board;

        public override string TideName => "Extras";

        protected override unsafe void ReadLive(byte* values, byte* locals, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var v = *(Value*)values;
            bonus = runner.GameObjectOf(v.bonus);
            again = runner.GameObjectOf(v.again);
            tossed = runner.GameObjectOf(v.tossed);
            said = v.said.Read(packet);
            speakers = global::Tide.TideRead.Set<uint, uint>(packet, runner, v.speakers.handle, (e0, p0, r0) => e0);
            board = global::Tide.TideRead.Grid2<byte, byte>(packet, runner, v.board.handle, (e0, p0, r0) => e0);
        }

        // Tide's singleton Extras, as the game's C lays it out
        [global::Tide.TideType("Extras")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 64)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public global::Tide.TideEntityId bonus;
            [global::System.Runtime.InteropServices.FieldOffset(8)] public global::Tide.TideEntityId again;
            [global::System.Runtime.InteropServices.FieldOffset(16)] public global::Tide.TideEntityId tossed;
            [global::System.Runtime.InteropServices.FieldOffset(24)] public global::Tide.TideText said;
            [global::System.Runtime.InteropServices.FieldOffset(32)] public global::Tide.TideMap speakers;
            [global::System.Runtime.InteropServices.FieldOffset(56)] public global::Tide.TideGrid board;
        }
    }
}
namespace Tide.Game
{
    // Tide's singleton Room, as Tide's views see it this frame: TideRunner.Get (TideSingleton)
    /// <summary>
    /// <para>Tide's singleton Room, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    public sealed partial class Room : global::Tide.TideSingleton
    {
        public int arrivals;
        public global::System.Collections.Generic.Dictionary<uint, global::UnityEngine.GameObject> balls;

        public override string TideName => "Room";

        protected override unsafe void ReadLive(byte* values, byte* locals, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var v = *(Value*)values;
            arrivals = v.arrivals;
            balls = global::Tide.TideRead.Map<uint, global::Tide.TideEntityId, uint, global::UnityEngine.GameObject>(packet, runner, v.balls.handle, (k0, p0, r0) => k0, (e0, p0, r0) => r0.GameObjectOf(e0));
        }

        // Tide's singleton Room, as the game's C lays it out
        [global::Tide.TideType("Room")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 32)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public int arrivals;
            [global::System.Runtime.InteropServices.FieldOffset(8)] public global::Tide.TideMap balls;
        }
    }
}
namespace Tide.Game
{
    // Tide's struct Plates, as Unity code has it
    /// <summary>
    /// <para>Tide's struct Plates, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    [global::System.Serializable]
    public unsafe partial struct Plates
    {
        public global::System.Collections.Generic.List<global::Tide.Game.PlateCorner> corners;
        public global::System.Collections.Generic.List<int> faces;

        public static Plates From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new Plates();
            s.corners = global::Tide.TideRead.List<global::Tide.Game.PlateCorner.Value, global::Tide.Game.PlateCorner>(packet, runner, v.corners.handle, (e0, p0, r0) => global::Tide.Game.PlateCorner.From(e0, p0, r0));
            s.faces = global::Tide.TideRead.List<int, int>(packet, runner, v.faces.handle, (e0, p0, r0) => e0);
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.faces = writer.List(faces);
            return v;
        }

        // Tide's struct Plates, as the game's C lays it out
        [global::Tide.TideType("Plates")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 8)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public global::Tide.TideList corners;
            [global::System.Runtime.InteropServices.FieldOffset(4)] public global::Tide.TideList faces;
        }
    }
}
namespace Tide.Game
{
    // Tide's struct Badge, as Unity code has it
    /// <summary>
    /// A render target Tide draws into, shown at the top right (yellow over its top left, green elsewhere), and stencil:
    /// a circle storing 1 where it is, and a cyan square drawn only there
    /// <para>Tide's struct Badge, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    [global::System.Serializable]
    public unsafe partial struct Badge
    {
        public bool made;

        public static Badge From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new Badge();
            s.made = v.made;
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.made = made;
            return v;
        }

        // Tide's struct Badge, as the game's C lays it out
        [global::Tide.TideType("Badge")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 32)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public fixed byte image[24]; // RenderTarget: its bytes
            [global::System.Runtime.InteropServices.FieldOffset(24)] public bool made;
        }
    }
}
namespace Tide.Game
{
    // Tide's struct Depths, as Unity code has it
    /// <summary>
    /// A multisampled target's depth, which Unity resolves to one sample a pixel for a frag to read: a cube drawn into it,
    /// its depth shown at the bottom left, darker where the cube is nearer than the far limit
    /// <para>Tide's struct Depths, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    [global::System.Serializable]
    public unsafe partial struct Depths
    {
        public bool made;

        public static Depths From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new Depths();
            s.made = v.made;
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.made = made;
            return v;
        }

        // Tide's struct Depths, as the game's C lays it out
        [global::Tide.TideType("Depths")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 56)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public fixed byte image[24]; // RenderTarget: its bytes
            [global::System.Runtime.InteropServices.FieldOffset(24)] public fixed byte depth[24]; // DepthTexture: its bytes
            [global::System.Runtime.InteropServices.FieldOffset(48)] public bool made;
        }
    }
}
namespace Tide.Game.UnityEngine
{
    // Tide's singleton UnityEngine.LoadedScenes, as Tide's views see it this frame: TideRunner.Get (TideSingleton)
    public sealed partial class LoadedScenes : global::Tide.TideSingleton
    {
        public global::System.Collections.Generic.Dictionary<int, global::UnityEngine.GameObject> loaded;
        public uint opener;
        public bool heard;
        public bool opened;

        public override string TideName => "UnityEngine.LoadedScenes";

        protected override unsafe void ReadLive(byte* values, byte* locals, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var v = *(Value*)values;
            loaded = global::Tide.TideRead.Map<int, global::Tide.TideEntityId, int, global::UnityEngine.GameObject>(packet, runner, v.loaded.handle, (k0, p0, r0) => k0, (e0, p0, r0) => r0.GameObjectOf(e0));
            opener = v.opener;
            heard = v.heard;
            opened = v.opened;
        }

        // Tide's singleton UnityEngine.LoadedScenes, as the game's C lays it out
        [global::Tide.TideType("UnityEngine.LoadedScenes")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 32)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public global::Tide.TideMap loaded;
            [global::System.Runtime.InteropServices.FieldOffset(24)] public uint opener;
            [global::System.Runtime.InteropServices.FieldOffset(28)] public bool heard;
            [global::System.Runtime.InteropServices.FieldOffset(29)] public bool opened;
        }
    }
}
namespace Tide.Game
{
    // Tide's singleton tide_hierarchy
    [global::Tide.TideType("tide_hierarchy")]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 24)]
    public unsafe partial struct tide_hierarchy
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public global::Tide.TideMap nodes;
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
    // Tide's local Ball's local fields
    [global::Tide.TideType("Ball", true)]
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 4)]
    public unsafe partial struct BallLocal
    {
        [global::System.Runtime.InteropServices.FieldOffset(0)] public int heard;
    }
}
namespace Tide.Game
{
    // Tide's struct Roll, as Unity code has it
    /// <summary>
    /// <para>Tide's struct Roll, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    [global::System.Serializable]
    public unsafe partial struct Roll
    {
        public global::UnityEngine.Vector2 move;
        public bool jump;

        public static Roll From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var s = new Roll();
            s.move = v.move;
            s.jump = v.jump;
            return s;
        }

        public Value Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.move = move;
            v.jump = jump;
            return v;
        }

        // Tide's struct Roll, as the game's C lays it out
        [global::Tide.TideType("Roll")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 12)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Vector2 move;
            [global::System.Runtime.InteropServices.FieldOffset(8)] public bool jump;
        }
    }
}
namespace Tide.Game
{
    // Tide's command ShowBonus: TideRunner.Send sends it, as local code's Send does (TideCommand)
    /// <summary>
    /// Bonus.unity's entities, while a player has them: the match loads the scene, and Unity loads it with them. Twice:
    /// the scene again beside it, which Unity loads another of
    /// <para>Tide's command ShowBonus, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    public sealed partial class ShowBonus : global::Tide.TideCommand
    {
        public bool on = false;
        public bool twice = false;

        public override string TideName => "ShowBonus";

        protected override void Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.on = on;
            v.twice = twice;
            writer.Value(v);
        }

        // Tide's command ShowBonus, as the game's C lays it out
        [global::Tide.TideType("ShowBonus")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 2)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public bool on;
            [global::System.Runtime.InteropServices.FieldOffset(1)] public bool twice;
        }
    }
}
namespace Tide.Game
{
    // Tide's command Say: TideRunner.Send sends it, as local code's Send does (TideCommand)
    /// <summary>
    /// A line from Unity's UI, which the match keeps for everyone: TideRunner.Get&lt;Extras&gt;().said in C#
    /// <para>Tide's command Say, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    public sealed partial class Say : global::Tide.TideCommand
    {
        public string text = "";

        public override string TideName => "Say";

        protected override void Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.text = writer.Text(text);
            writer.Value(v);
        }

        // Tide's command Say, as the game's C lays it out
        [global::Tide.TideType("Say")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 4)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public global::Tide.TideText text;
        }
    }
}
namespace Tide.Game
{
    // Tide's command Toss: TideRunner.Send sends it, as local code's Send does (TideCommand)
    /// <summary>
    /// A ball nobody rolls, in and out of the box: a GameObject made and let go of as its entity comes and goes, which
    /// TideRunner's pool keeps for the next
    /// <para>Tide's command Toss, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    public sealed partial class Toss : global::Tide.TideCommand
    {
        public bool on = false;

        public override string TideName => "Toss";

        protected override void Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.on = on;
            writer.Value(v);
        }

        // Tide's command Toss, as the game's C lays it out
        [global::Tide.TideType("Toss")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 1)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public bool on;
        }
    }
}
namespace Tide.Game
{
    // Tide's command Paint: TideRunner.Send sends it, as local code's Send does (TideCommand)
    /// <summary>
    /// Sent to a ball: TideRunner.Send(new Paint { ... }, ballGameObject) in C#
    /// <para>Tide's command Paint, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    public sealed partial class Paint : global::Tide.TideCommand
    {
        public global::UnityEngine.Color color = new global::UnityEngine.Color(0f, 0f, 0f, 0f);

        public override string TideName => "Paint";

        protected override void Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.color = color;
            writer.Value(v);
        }

        // Tide's command Paint, as the game's C lays it out
        [global::Tide.TideType("Paint")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 16)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Color color;
        }
    }
}
namespace Tide.Game.UnityEngine
{
    // Tide's command UnityEngine.OpenScene: TideRunner.Send sends it, as local code's Send does (TideCommand)
    public sealed partial class OpenScene : global::Tide.TideCommand
    {
        public int asset = 0;
        public bool only = false;

        public override string TideName => "UnityEngine.OpenScene";

        protected override void Pack(global::Tide.TideCommandWriter writer)
        {
            var v = new Value();
            v.asset = asset;
            v.only = only;
            writer.Value(v);
        }

        // Tide's command UnityEngine.OpenScene, as the game's C lays it out
        [global::Tide.TideType("UnityEngine.OpenScene")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 8)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public int asset;
            [global::System.Runtime.InteropServices.FieldOffset(4)] public bool only;
        }
    }
}
namespace Tide.Game
{
    // Tide's event Bounced, as this machine's local handlers hear it: TideRunner.On, OnVerified (TideEvent)
    /// <summary>
    /// A ball hitting the floor: C# hears it as local handlers do (TideRunner.On, OnVerified, the ball's TideEntityView.On)
    /// <para>Tide's event Bounced, declared in Assets/Tide/Bounce.tide.</para>
    /// </summary>
    public sealed partial class Bounced : global::Tide.TideEvent
    {
        public float speed;

        public override string TideName => "Bounced";

        protected override unsafe void ReadLive(byte* values, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var v = *(Value*)values;
            speed = v.speed;
        }

        // Tide's event Bounced, as the game's C lays it out
        [global::Tide.TideType("Bounced")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 4)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public float speed;
        }
    }
}
