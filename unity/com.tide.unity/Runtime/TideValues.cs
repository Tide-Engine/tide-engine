using System.Runtime.InteropServices;

namespace Tide
{
    // A text field of a view packet's value: its handle in the packet's STRINGS (0 for empty).
    [StructLayout(LayoutKind.Sequential)]
    public struct TideText
    {
        public uint handle;

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
    // elements hold text or lists aren't in the packet: they read as empty.
    [StructLayout(LayoutKind.Sequential)]
    public struct TideList
    {
        public uint handle;

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

    // An entity field: Tide's handle of one, which the packet's entities have too (TideEntityRow).
    [StructLayout(LayoutKind.Sequential)]
    public struct TideEntityId
    {
        public uint index;
        public uint generation;

        public bool IsNull => generation == 0;
        public ulong Id => ((ulong)generation << 32) | index;

        public static implicit operator TideEntityId(TideEntityRow row)
        {
            return new TideEntityId { index = row.Index, generation = row.Generation };
        }
    }
}
