using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace Tide
{
    // A view packet's lists, maps, sets, grids and arrays (tide/embed_game.h's STRINGS) as C#'s, each element made
    // Unity code's by `conv`: what the generated classes and structs read their fields with. Read into `into`, the
    // field's own from the frame before, where it has one: a value read every frame fills the list, map, set or grid
    // it has, and makes a new one only the first time. So what a script reads there is this frame's, and one it keeps
    // changes with the match: copy what should stay.
    public static unsafe class TideRead
    {
        public static List<C> List<L, C>(TidePacket packet, TideRunner runner, uint handle, Func<L, TidePacket, TideRunner, C> conv,
            List<C> into = null) where L : unmanaged
        {
            var items = into ?? new List<C>();
            items.Clear();
            var count = 0;
            var at = packet != null ? (L*)packet.List(handle, (uint)sizeof(L), out count) : null;
            if (at == null) return items;
            if (items.Capacity < count) items.Capacity = count;
            if (typeof(L) == typeof(C))
            {
                // The same values as they're laid out: no call each
                var same = (List<L>)(object)items;
                for (var i = 0; i < count; i++) same.Add(at[i]);
                return items;
            }
            for (var i = 0; i < count; i++) items.Add(conv(at[i], packet, runner));
            return items;
        }

        // A list of text: each line that's what the list had there the frame before stays the string it was, so a
        // chat's lines or players' names read every frame make a string only as one changes.
        public static List<string> Texts(TidePacket packet, uint handle, List<string> into = null)
        {
            var items = into ?? new List<string>();
            var count = 0;
            var at = packet != null ? (TideText*)packet.List(handle, (uint)sizeof(TideText), out count) : null;
            if (at == null) count = 0;
            for (var i = 0; i < count; i++)
            {
                if (i < items.Count) items[i] = at[i].Read(packet, items[i]);
                else items.Add(at[i].Read(packet));
            }
            if (items.Count > count) items.RemoveRange(count, items.Count - count);
            return items;
        }

        public static TideSet<C> Set<L, C>(TidePacket packet, TideRunner runner, uint handle, Func<L, TidePacket, TideRunner, C> conv,
            TideSet<C> into = null) where L : unmanaged
        {
            var set = into ?? new TideSet<C>();
            set.Clear();
            var count = Counted(packet, handle, (uint)sizeof(L), 0, out var keys, out _);
            for (var i = 0; i < count; i++) set.Add(conv(((L*)keys)[i], packet, runner));
            return set;
        }

        public static Dictionary<CK, CV> Map<LK, LV, CK, CV>(TidePacket packet, TideRunner runner, uint handle,
            Func<LK, TidePacket, TideRunner, CK> key, Func<LV, TidePacket, TideRunner, CV> value, Dictionary<CK, CV> into = null)
            where LK : unmanaged where LV : unmanaged
        {
            var map = into ?? new Dictionary<CK, CV>();
            map.Clear();
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

        public static TideGrid2<C> Grid2<L, C>(TidePacket packet, TideRunner runner, uint handle, Func<L, TidePacket, TideRunner, C> conv,
            TideGrid2<C> into = null) where L : unmanaged
        {
            var grid = into ?? new TideGrid2<C>();
            Fill(grid, packet, runner, handle, conv);
            return grid;
        }

        public static TideGrid3<C> Grid3<L, C>(TidePacket packet, TideRunner runner, uint handle, Func<L, TidePacket, TideRunner, C> conv,
            TideGrid3<C> into = null) where L : unmanaged
        {
            var grid = into ?? new TideGrid3<C>();
            Fill(grid, packet, runner, handle, conv);
            return grid;
        }

        // A grid's chunks into the arrays it has for them: a chunk it had keeps its array, one that's new gets one, and
        // one the grid has no more goes. Cells laid out as Unity code has them are copied whole.
        static void Fill<L, C>(TideGrid<C> grid, TidePacket packet, TideRunner runner, uint handle, Func<L, TidePacket, TideRunner, C> conv)
            where L : unmanaged
        {
            if (!grid.Begin(packet, handle, (uint)sizeof(L), out var at, out var count, out var cells, out var each)) return;
            var same = typeof(L) == typeof(C);
            for (var c = 0; c < count; c++)
            {
                var chunk = at + 48 + (long)c * each;
                var position = (int*)chunk;
                var from = (L*)(chunk + 16);
                var values = grid.Chunk(position[0], position[1], position[2], cells);
                if (same)
                {
                    fixed (L* to = (L[])(object)values) Buffer.MemoryCopy(from, to, (long)cells * sizeof(L), (long)cells * sizeof(L));
                    continue;
                }
                for (var i = 0; i < cells; i++) values[i] = conv(from[i], packet, runner);
            }
            grid.End(count);
        }

        // A fixed-length array, `length` elements at `at` (its Value's bytes)
        public static C[] Array<L, C>(TidePacket packet, TideRunner runner, byte* at, int length, Func<L, TidePacket, TideRunner, C> conv,
            C[] into = null) where L : unmanaged
        {
            var items = into != null && into.Length == length ? into : new C[length];
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
            lookup?.Clear();
        }

        public List<T>.Enumerator GetEnumerator() => items.GetEnumerator();
        IEnumerator<T> IEnumerable<T>.GetEnumerator() => items.GetEnumerator();
        IEnumerator IEnumerable.GetEnumerator() => items.GetEnumerator();
    }

    // Tide's Grid2<T> and Grid3<T> as a view packet has them: the chunks it has, a cell anywhere else zero (default).
    public abstract unsafe class TideGrid<T>
    {
        sealed class Held
        {
            public T[] values;
            public int read; // The read that last had it
        }

        readonly Dictionary<(int, int, int), Held> chunks = new Dictionary<(int, int, int), Held>();
        readonly List<(int, int, int)> gone = new List<(int, int, int)>();
        protected int shiftX, shiftY, shiftZ;
        int reads;

        // Cells along each axis, 0 where it's open
        public Vector3Int Size { get; private set; }

        public int ChunkCount => chunks.Count;

        // A read's start: the packet's entry of `handle`, its header checked. False, the grid empty, for none or one
        // that isn't what it says; else its chunks from `at` + 48, `count` of them of `cells` cells, `each` bytes apart.
        internal bool Begin(TidePacket packet, uint handle, uint cellSize, out byte* at, out int count, out int cells, out int each)
        {
            count = cells = each = 0;
            uint length = 0;
            at = packet != null ? packet.Bytes(handle, out length) : null;
            var head = (int*)at;
            if (at == null || length < 48 || head[1] != cellSize || head[2] < 0 || head[2] > 12 || head[3] < 0 || head[3] > 12
                || head[4] < 0 || head[4] > 12 || head[2] + head[3] + head[4] > 24)
            {
                chunks.Clear();
                return false;
            }
            cells = 1 << (head[2] + head[3] + head[4]);
            each = 16 + (int)(((long)cells * cellSize + 7) & ~7L);
            count = head[8];
            if (count < 0 || 48 + (long)count * each > length)
            {
                chunks.Clear();
                return false;
            }
            if (shiftX != head[2] || shiftY != head[3] || shiftZ != head[4]) chunks.Clear(); // Chunks of another size
            shiftX = head[2];
            shiftY = head[3];
            shiftZ = head[4];
            Size = new Vector3Int(head[5], head[6], head[7]);
            reads++;
            return true;
        }

        // The cells of the chunk at a position, for this read to fill: the array it had, or a new one.
        internal T[] Chunk(int x, int y, int z, int cells)
        {
            if (!chunks.TryGetValue((x, y, z), out var held) || held.values.Length != cells)
            {
                chunks[(x, y, z)] = held = new Held { values = new T[cells] };
            }
            held.read = reads;
            return held.values;
        }

        // A read's end, having filled `count` chunks: those it didn't have go.
        internal void End(int count)
        {
            if (chunks.Count == count) return;
            gone.Clear();
            foreach (var pair in chunks)
            {
                if (pair.Value.read != reads) gone.Add(pair.Key);
            }
            foreach (var key in gone) chunks.Remove(key);
        }

        protected T At(int x, int y, int z)
        {
            if ((Size.x > 0 && (uint)x >= (uint)Size.x) || (Size.y > 0 && (uint)y >= (uint)Size.y) || (Size.z > 0 && (uint)z >= (uint)Size.z))
            {
                return default;
            }
            if (!chunks.TryGetValue((x >> shiftX, y >> shiftY, z >> shiftZ), out var held)) return default;
            var place = (x & ((1 << shiftX) - 1)) | (y & ((1 << shiftY) - 1)) << shiftX | (z & ((1 << shiftZ) - 1)) << (shiftX + shiftY);
            return held.values[place];
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
