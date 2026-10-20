using System;
using System.Collections.Generic;
using System.Text;

namespace Tide
{
    // What a section of a view packet holds (tide/embed.h, the view packet).
    public enum TideSectionKind : uint
    {
        Component = 1,      // Rows (indices into the entities), then their values
        Singleton = 2,      // One value
        ComponentLocal = 3, // A match component's local fields: rows, then values
        SingletonLocal = 4, // A match singleton's local fields: one value
        Strings = 5,        // Text and lists: {offset, length} entries, then their bytes
    }

    // One entity the view packet holds.
    public struct TideEntityRow
    {
        public uint Index;
        public uint Generation;
        public uint Flags;  // TideEntityFlags
        public uint Prefab; // The schema's prefab index, or uint.MaxValue for none
        public uint Parent; // Its parent's index among the packet's entities, or uint.MaxValue for a root
        // What it was spawned as: another value on the same handle is another entity, which a rollback gave the
        // handle. 0: not known (it came in a world another machine sent)
        public uint Born;

        public bool Mine => (Flags & 1u) != 0;  // This machine owns it
        public bool Owned => (Flags & 2u) != 0; // Some player owns it
        public bool HasPrefab => Prefab != uint.MaxValue;
        public bool HasParent => Parent != uint.MaxValue;
        public ulong Id => ((ulong)Generation << 32) | Index;
    }

    // A frame's view packet, read in place: what Tide's views would see this
    // frame, blended between ticks. Every read is checked against the packet's
    // size, and a packet whose schema isn't the bindings' isn't read at all.
    public sealed unsafe class TidePacket
    {
        public const uint Magic = 0x56444954; // "TIDV"
        public const uint Version = 2;
        const int HeaderSize = 48;
        const int EntitySize = 24;
        const int SectionHeaderSize = 24;

        byte* bytes;
        uint size;

        public ulong SchemaHash { get; private set; }
        public bool InMatch { get; private set; }
        public uint Tick { get; private set; }
        public uint VerifiedTick { get; private set; }
        public float Alpha { get; private set; }
        public int EntityCount { get; private set; }

        struct Section
        {
            public uint Kind, Type, Count, Stride, Bytes;
            public uint Data; // Where its payload starts
        }

        readonly List<Section> sections = new List<Section>();
        uint entities; // Where the entities start
        int strings = -1;

        // Reads `size` bytes at `data`, the packet tide_embed_view gave: false for one that isn't whole or isn't one.
        public bool Read(byte* data, uint length)
        {
            bytes = null;
            size = 0;
            sections.Clear();
            strings = -1;
            EntityCount = 0;
            if (data == null || length < HeaderSize) return false;
            if (U32(data, 0) != Magic || U32(data, 4) != Version) return false;
            var declared = U32(data, 16);
            if (declared > length || declared < HeaderSize) return false;
            var entityCount = U32(data, 36);
            var sectionCount = U32(data, 40);
            if ((ulong)HeaderSize + (ulong)entityCount * EntitySize > declared) return false;
            SchemaHash = *(ulong*)(data + 8);
            InMatch = (U32(data, 20) & 1u) != 0;
            Tick = U32(data, 24);
            VerifiedTick = U32(data, 28);
            Alpha = *(float*)(data + 32);
            entities = HeaderSize;
            var at = (ulong)HeaderSize + (ulong)entityCount * EntitySize;
            for (uint i = 0; i < sectionCount; i++)
            {
                at = (at + 7) & ~7ul;
                if (at + SectionHeaderSize > declared) return false;
                var s = new Section
                {
                    Kind = U32(data, (uint)at), Type = U32(data, (uint)at + 4), Count = U32(data, (uint)at + 8),
                    Stride = U32(data, (uint)at + 12), Bytes = U32(data, (uint)at + 16), Data = (uint)at + SectionHeaderSize,
                };
                if ((ulong)s.Data + s.Bytes > declared) return false;
                if (!Fits(s)) return false;
                if (s.Kind == (uint)TideSectionKind.Strings) strings = sections.Count;
                sections.Add(s);
                at = (ulong)s.Data + s.Bytes;
            }
            bytes = data;
            size = declared;
            EntityCount = (int)entityCount;
            return true;
        }

        // Whether a section's counts fit in its bytes.
        static bool Fits(Section s)
        {
            switch ((TideSectionKind)s.Kind)
            {
                case TideSectionKind.Component:
                case TideSectionKind.ComponentLocal:
                    var rows = ((ulong)s.Count * 4 + 7) & ~7ul;
                    return rows + (ulong)s.Count * s.Stride <= s.Bytes;
                case TideSectionKind.Singleton:
                case TideSectionKind.SingletonLocal:
                    return s.Stride <= s.Bytes;
                case TideSectionKind.Strings:
                    return (ulong)s.Count * 8 <= s.Bytes;
                default:
                    return true; // A kind of a newer tide: skipped
            }
        }

        static uint U32(byte* data, uint at) => *(uint*)(data + at);

        public TideEntityRow Entity(int i)
        {
            if ((uint)i >= (uint)EntityCount) throw new ArgumentOutOfRangeException(nameof(i));
            var p = (uint*)(bytes + entities + (uint)i * EntitySize);
            return new TideEntityRow { Index = p[0], Generation = p[1], Flags = p[2], Prefab = p[3], Parent = p[4], Born = p[5] };
        }

        int Find(TideSectionKind kind, uint type)
        {
            for (var i = 0; i < sections.Count; i++)
            {
                if (sections[i].Kind == (uint)kind && sections[i].Type == type) return i;
            }
            return -1;
        }

        // How many entities have component `type` (the schema's type index), and the stride of its values.
        public int Count(uint type, out uint stride, bool local = false)
        {
            var i = Find(local ? TideSectionKind.ComponentLocal : TideSectionKind.Component, type);
            stride = i < 0 ? 0 : sections[i].Stride;
            return i < 0 ? 0 : (int)sections[i].Count;
        }

        // The `row`th entity with component `type`: its index among the packet's entities, and its value's bytes.
        public byte* Row(uint type, int row, out int entity, bool local = false)
        {
            entity = -1;
            var i = Find(local ? TideSectionKind.ComponentLocal : TideSectionKind.Component, type);
            if (i < 0 || (uint)row >= sections[i].Count) return null;
            var s = sections[i];
            entity = (int)U32(bytes, s.Data + (uint)row * 4);
            if ((uint)entity >= (uint)EntityCount) entity = -1;
            var values = s.Data + (uint)(((ulong)s.Count * 4 + 7) & ~7ul);
            return bytes + values + (uint)row * s.Stride;
        }

        // A singleton's value (the schema's type index), or null outside a match or for one the packet hasn't.
        public byte* Singleton(uint type, out uint stride, bool local = false)
        {
            var i = Find(local ? TideSectionKind.SingletonLocal : TideSectionKind.Singleton, type);
            stride = i < 0 ? 0 : sections[i].Stride;
            return i < 0 ? null : bytes + sections[i].Data;
        }

        // A text field's value: the packet's handle for it (0 for empty).
        public string Text(uint handle)
        {
            if (!Entry(handle, out var at, out var length)) return string.Empty;
            return Encoding.UTF8.GetString(bytes + at, (int)length);
        }

        // A list field's elements: the packet's handle for it, and each element's size.
        public byte* List(uint handle, uint elementSize, out int count)
        {
            count = 0;
            if (elementSize == 0 || !Entry(handle, out var at, out var length)) return null;
            count = (int)(length / elementSize);
            return bytes + at;
        }

        bool Entry(uint handle, out uint at, out uint length)
        {
            at = length = 0;
            if (handle == 0 || strings < 0) return false;
            var s = sections[strings];
            if (handle > s.Count) return false;
            var entry = (uint*)(bytes + s.Data + (handle - 1) * 8);
            var area = s.Data + s.Count * 8;
            if ((ulong)entry[0] + entry[1] > s.Bytes - s.Count * 8) return false;
            at = area + entry[0];
            length = entry[1];
            return true;
        }
    }
}
