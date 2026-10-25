// Written by tide unity from the game's schema. Do not edit: it changes with the game.
#pragma warning disable
namespace Tide.Game
{
    // Tide's component Spinner: its fields as authored outside Play, its entity's values in Play (TideComponent)
    [global::UnityEngine.AddComponentMenu("Tide/Spinner")]
    [global::UnityEngine.DisallowMultipleComponent]
    [global::UnityEngine.Icon(global::Tide.TideIcon.Component)]
    public sealed partial class Spinner : global::Tide.TideComponent
    {
        public float speed = 1f;
        public int badge = 83;

        public override string TideName => "Spinner";

        protected override unsafe void ReadLive(byte* values, byte* locals, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var v = *(Value*)values;
            speed = v.speed;
            badge = v.badge;
        }

        // Tide's component Spinner, as the game's C lays it out
        [global::Tide.TideType("Spinner")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 8)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public float speed;
            [global::System.Runtime.InteropServices.FieldOffset(4)] public int badge;
        }
    }
}
