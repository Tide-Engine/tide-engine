// Written by tide unity from the game's schema. Do not edit: it changes with the game.
#pragma warning disable
namespace Tide.Game
{
    // Tide's component Owner: its fields as authored outside Play, its entity's values in Play (TideComponent)
    [global::UnityEngine.AddComponentMenu("Tide/Owner")]
    [global::UnityEngine.DisallowMultipleComponent]
    public sealed partial class Owner : global::Tide.TideComponent
    {
        public uint player = 0u;

        public override string TideName => "Owner";

        protected override unsafe void ReadLive(byte* values, byte* locals, global::Tide.TidePacket packet, global::Tide.TideRunner runner)
        {
            var v = *(Value*)values;
            player = v.player;
        }

        // Tide's component Owner, as the game's C lays it out
        [global::Tide.TideType("Owner")]
        [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = 4)]
        public unsafe partial struct Value
        {
            [global::System.Runtime.InteropServices.FieldOffset(0)] public uint player;
        }
    }
}
