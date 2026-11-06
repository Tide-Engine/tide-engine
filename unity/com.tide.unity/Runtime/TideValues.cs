using System;
using System.Runtime.InteropServices;

namespace Tide
{
    // A text field of a view packet's value: its handle in the packet's STRINGS (0 for empty), in the first of the
    // 8 bytes the game's C has for one.
    [StructLayout(LayoutKind.Sequential)]
    public struct TideText
    {
        public uint handle;
        uint unused;

        public bool Empty => handle == 0;

        // Its characters, as this frame's packet has them.
        public string Read(TidePacket packet)
        {
            return packet != null ? packet.Text(handle) : string.Empty;
        }

        public string Read()
        {
            return Read(TideRunner.Current != null ? TideRunner.Current.Packet : null);
        }

        public override string ToString()
        {
            return Read();
        }
    }

    // A list field of a view packet's value: its elements, side by side, in the packet's STRINGS. Lists whose
    // elements hold text or lists aren't in the packet: they read as empty. Its handle is in the first of the 8 bytes
    // the game's C has for one.
    [StructLayout(LayoutKind.Sequential)]
    public struct TideList
    {
        public uint handle;
        uint unused;

        public unsafe int Count<T>(TidePacket packet = null) where T : unmanaged
        {
            packet = packet ?? TideRunner.Current?.Packet;
            if (packet == null) return 0;
            packet.List(handle, (uint)sizeof(T), out var count);
            return count;
        }

        // Its `i`th element, or zero past its end.
        public unsafe T Get<T>(int i, TidePacket packet = null) where T : unmanaged
        {
            packet = packet ?? TideRunner.Current?.Packet;
            if (packet == null) return default;
            var at = packet.List(handle, (uint)sizeof(T), out var count);
            return at != null && (uint)i < (uint)count ? ((T*)at)[i] : default;
        }

        public unsafe T[] ToArray<T>(TidePacket packet = null) where T : unmanaged
        {
            packet = packet ?? TideRunner.Current?.Packet;
            if (packet == null) return System.Array.Empty<T>();
            var at = (T*)packet.List(handle, (uint)sizeof(T), out var count);
            var items = new T[count];
            for (var i = 0; i < count; i++) items[i] = at[i];
            return items;
        }
    }

    // A map or set field of a view packet's value: its handle in the packet's STRINGS (0 for empty), in the first of the
    // 32 bytes the game's C has for one (TideRead.Map and Set read it).
    [StructLayout(LayoutKind.Explicit, Size = 32)]
    public struct TideMap
    {
        [FieldOffset(0)] public uint handle;
    }

    // A grid field of a view packet's value: its handle in the packet's STRINGS (TideRead.Grid2 and Grid3 read it), in
    // the first of the 8 bytes the game's C has for one.
    [StructLayout(LayoutKind.Sequential)]
    public struct TideGrid
    {
        public uint handle;
        uint unused;
    }

    // An entity field: Tide's handle of one, which the packet's entities have too (TideEntityRow).
    [StructLayout(LayoutKind.Sequential)]
    public struct TideEntityId : IEquatable<TideEntityId>
    {
        public ulong birth;
        public ulong lineage;
        public bool IsNull => birth == 0 && lineage == 0;
        public bool Equals(TideEntityId other) => birth == other.birth && lineage == other.lineage;
        public override bool Equals(object value) => value is TideEntityId other && Equals(other);
        public override int GetHashCode() => unchecked(birth.GetHashCode() * 397 ^ lineage.GetHashCode());
        public static bool operator ==(TideEntityId a, TideEntityId b) => a.Equals(b);
        public static bool operator !=(TideEntityId a, TideEntityId b) => !a.Equals(b);
        public override string ToString() => IsNull ? "none" : $"Entity({birth:x16}:{lineage:x16})";
        public static implicit operator TideEntityId(TideEntityRow row) => row.Id;
    }
}
