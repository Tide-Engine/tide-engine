// Written by tide unity from the game's schema. Do not edit: it changes with the game.
#pragma warning disable
namespace Tide.Game
{
    // Tide's component Ball: its fields as authored outside Play, its entity's values in Play (TideComponent)
    [global::UnityEngine.AddComponentMenu("Tide/Ball")]
    [global::UnityEngine.DisallowMultipleComponent]
    public sealed partial class Ball : global::Tide.TideComponent
    {
        public global::UnityEngine.Vector3 velocity = new global::UnityEngine.Vector3(0f, 0f, 0f);
        public global::UnityEngine.Color color = new global::UnityEngine.Color(0f, 0f, 0f, 0f);
        public int seat = 0;
        public int bounces = 0;

        public override string TideName => "Ball";

        protected override unsafe void ReadLive(byte* values, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var v = *(Value*)values;
            velocity = v.velocity;
            color = v.color;
            seat = v.seat;
            bounces = v.bounces;
        }

        // Tide's component Ball, as the game's C lays it out
        [global::Tide.TideType("Ball")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 36)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public global::UnityEngine.Vector3 velocity;
            [global::System.Runtime.InteropServices.FieldOffset(12)] public global::UnityEngine.Color color;
            [global::System.Runtime.InteropServices.FieldOffset(28)] public int seat;
            [global::System.Runtime.InteropServices.FieldOffset(32)] public int bounces;
        }
    }
}
