using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace Tide
{
    // A view packet's lists, maps, sets, grids and arrays (tide/embed_game.h's STRINGS) as C#'s, each element made
    // Unity code's by `conv`: what the generated classes and structs read their fields with.
    public static unsafe class TideRead
    {
        public static List<C> List<L, C>(TidePacket packet, TideRunner runner, uint handle, Func<L, TidePacket, TideRunner, C> conv)
            where L : unmanaged
        {
            var items = new List<C>();
            var count = 0;
            var at = packet != null ? (L*)packet.List(handle, (uint)sizeof(L), out count) : null;
            for (var i = 0; at != null && i < count; i++) items.Add(conv(at[i], packet, runner));
            return items;
        }

        public static TideSet<C> Set<L, C>(TidePacket packet, TideRunner runner, uint handle, Func<L, TidePacket, TideRunner, C> conv)
            where L : unmanaged
        {
            var set = new TideSet<C>();
            var count = Counted(packet, handle, (uint)sizeof(L), 0, out var keys, out _);
            for (var i = 0; i < count; i++) set.Add(conv(((L*)keys)[i], packet, runner));
            return set;
        }

        public static Dictionary<CK, CV> Map<LK, LV, CK, CV>(TidePacket packet, TideRunner runner, uint handle,
            Func<LK, TidePacket, TideRunner, CK> key, Func<LV, TidePacket, TideRunner, CV> value) where LK : unmanaged where LV : unmanaged
        {
            var map = new Dictionary<CK, CV>();
            var count = Counted(packet, handle, (uint)sizeof(LK), (uint)sizeof(LV), out var keys, out var values);
            for (var i = 0; i < count; i++)
            {
                var k = key(((LK*)keys)[i], packet, runner);
                if (k != null) map[k] = value(((LV*)values)[i], packet, runner);
            }
            return map;
        }

        // A map's or set's entry: its count, its keys after 8 bytes, its values after them (to 8 bytes); 0 where it
        // doesn't hold what it says.
        static int Counted(TidePacket packet, uint handle, uint keySize, uint valueSize, out byte* keys, out byte* values)
        {
            keys = values = null;
            uint length = 0;
            var at = packet != null ? packet.Bytes(handle, out length) : null;
            if (at == null || length < 8) return 0;
            var count = *(uint*)at;
            var keyBytes = ((ulong)count * keySize + 7) & ~7ul;
            if (8 + keyBytes + (ulong)count * valueSize > length) return 0;
            keys = at + 8;
            values = at + 8 + keyBytes;
            return (int)count;
        }

        public static TideGrid2<C> Grid2<L, C>(TidePacket packet, TideRunner runner, uint handle, Func<L, TidePacket, TideRunner, C> conv)
            where L : unmanaged
        {
            var grid = new TideGrid2<C>();
            grid.Read(packet, runner, handle, (uint)sizeof(L), (cells, i) => conv(((L*)cells.ToPointer())[i], packet, runner));
            return grid;
        }

        public static TideGrid3<C> Grid3<L, C>(TidePacket packet, TideRunner runner, uint handle, Func<L, TidePacket, TideRunner, C> conv)
            where L : unmanaged
        {
            var grid = new TideGrid3<C>();
            grid.Read(packet, runner, handle, (uint)sizeof(L), (cells, i) => conv(((L*)cells.ToPointer())[i], packet, runner));
            return grid;
        }

        // A fixed-length array, `length` elements at `at` (its Value's bytes)
        public static C[] Array<L, C>(TidePacket packet, TideRunner runner, byte* at, int length, Func<L, TidePacket, TideRunner, C> conv)
            where L : unmanaged
        {
            var items = new C[length];
            for (var i = 0; i < length; i++) items[i] = conv(((L*)at)[i], packet, runner);
            return items;
        }
    }

    // Tide's Set<T>: its keys in the order they were added. Serializable, so a component's set is authored in Unity.
    [Serializable]
    public sealed class TideSet<T> : IEnumerable<T>
    {
        [SerializeField] List<T> items = new List<T>();
        [NonSerialized] HashSet<T> lookup;

        public int Count => items.Count;

        public bool Contains(T item)
        {
            lookup ??= new HashSet<T>(items);
            return lookup.Contains(item);
        }

        public bool Add(T item)
        {
            if (Contains(item)) return false;
            items.Add(item);
            lookup.Add(item);
            return true;
        }

        public bool Remove(T item)
        {
            if (!Contains(item)) return false;
            items.Remove(item);
            lookup.Remove(item);
            return true;
        }

        public void Clear()
        {
            items.Clear();
            lookup = null;
        }

        public IEnumerator<T> GetEnumerator() => items.GetEnumerator();
        IEnumerator IEnumerable.GetEnumerator() => items.GetEnumerator();
    }

    // Tide's Grid2<T> and Grid3<T> as a view packet has them: the chunks it has, a cell anywhere else zero (default).
    public abstract unsafe class TideGrid<T>
    {
        protected readonly Dictionary<(int, int, int), T[]> chunks = new Dictionary<(int, int, int), T[]>();
        protected int shiftX, shiftY, shiftZ;

        // Cells along each axis, 0 where it's open
        public Vector3Int Size { get; private set; }

        public int ChunkCount => chunks.Count;

        internal void Read(TidePacket packet, TideRunner runner, uint handle, uint cellSize, Func<IntPtr, int, T> cell)
        {
            chunks.Clear();
            uint length = 0;
            var at = packet != null ? packet.Bytes(handle, out length) : null;
            if (at == null || length < 48) return;
            var head = (int*)at;
            if (head[1] != cellSize || head[2] < 0 || head[2] > 12 || head[3] < 0 || head[3] > 12 || head[4] < 0 || head[4] > 12) return;
            shiftX = head[2];
            shiftY = head[3];
            shiftZ = head[4];
            Size = new Vector3Int(head[5], head[6], head[7]);
            var count = head[8];
            var cells = 1 << (shiftX + shiftY + shiftZ);
            var each = 16 + ((cells * (int)cellSize + 7) & ~7);
            if (count < 0 || 48 + (long)count * each > length) return;
            for (var c = 0; c < count; c++)
            {
                var chunk = at + 48 + c * each;
                var position = (int*)chunk;
                var values = new T[cells];
                for (var i = 0; i < cells; i++) values[i] = cell((IntPtr)(chunk + 16), i);
                chunks[(position[0], position[1], position[2])] = values;
            }
        }

        protected T At(int x, int y, int z)
        {
            if ((Size.x > 0 && (uint)x >= (uint)Size.x) || (Size.y > 0 && (uint)y >= (uint)Size.y) || (Size.z > 0 && (uint)z >= (uint)Size.z))
            {
                return default;
            }
            if (!chunks.TryGetValue((x >> shiftX, y >> shiftY, z >> shiftZ), out var values)) return default;
            var place = (x & ((1 << shiftX) - 1)) | (y & ((1 << shiftY) - 1)) << shiftX | (z & ((1 << shiftZ) - 1)) << (shiftX + shiftY);
            return values[place];
        }
    }

    public sealed class TideGrid2<T> : TideGrid<T>
    {
        public T this[int x, int y] => At(x, y, 0);
        public T this[Vector2Int at] => At(at.x, at.y, 0);
    }

    public sealed class TideGrid3<T> : TideGrid<T>
    {
        public T this[int x, int y, int z] => At(x, y, z);
        public T this[Vector3Int at] => At(at.x, at.y, at.z);
    }
}
