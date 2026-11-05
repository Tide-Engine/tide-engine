using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using NUnit.Framework;
using Tide;
using UnityEngine;
using UnityEngine.TestTools;

// TideRender's replay of gpu.h's compute (the recorder's ops after DRAW_INDEXED): commands made here as
// platform/src/gpu_record.c writes them, replayed with no runner, its reads back answered to the test (ReadsTo). Its
// shaders are tidec's HLSL as tide unity writes it (tide_wgsl_translate --hlsl), in Resources/TideShaders:
// 00000000c0de0002.shader is compiler/tests/wgsl/storage_draw.wgsl's, and 00000000c0de0001.compute this kernel's:
//
//     struct Values { add: u32, count: u32, pad0: u32, pad1: u32 }
//     @group(0) @binding(0) var<uniform> u: Values;
//     @group(0) @binding(25) var<storage, read> given: array<u32>;
//     @group(0) @binding(26) var<storage, read_write> made: array<u32>;
//     @compute @workgroup_size(64) fn compute(@builtin(global_invocation_id) id: vec3<u32>,
//                                             @builtin(num_workgroups) groups: vec3<u32>) {
//         if (id.x >= u.count) { return; }
//         made[id.x] = given[id.x] * given[id.x] + u.add + groups.x * 1000u + groups.y * 100000u;
//     }
public sealed unsafe class ComputeReplayTests
{
    const ulong Kernel = 0xc0de0001, Shape = 0xc0de0002;

    enum Op : uint
    {
        Buffer = 1, BufferWrite = 2, Pipeline = 10, Begin = 15, End, Scissor, Use, Uniforms = 20, PipelineStorage = 26, Storage,
        ComputeBegin, ComputeEnd,
        Dispatch, DispatchIndirect, DrawIndirect, DrawIndexedIndirect, BufferCopy, BufferRead,
    }

    // tide_embed_render's commands: an op, its byte count, its words
    sealed class Commands
    {
        readonly List<uint> words = new List<uint>();

        public void Add(Op op, params uint[] given)
        {
            words.Add((uint)op);
            words.Add((uint)given.Length * 4);
            words.AddRange(given);
        }

        // A storage buffer of `size` bytes, its words given or zeros
        public void Buffer(uint id, uint size, uint[] given)
        {
            var w = new List<uint> { id, 2, size, given != null ? 1u : 0u };
            if (given != null) w.AddRange(given);
            Add(Op.Buffer, w.ToArray());
        }

        // A kernel's pipeline of `key`, with `uniforms` bytes of values and no textures, and its storage
        public void Kernel(uint id, ulong key, uint uniforms, uint storage, uint writes)
        {
            Pipeline(id, key, uniforms, 0);
            Add(Op.PipelineStorage, id, 1, storage, writes);
        }

        // ...or a draw's, which replaces what's there (blend 3), reading storage
        public void Draws(uint id, ulong key, uint uniforms, uint storage)
        {
            Pipeline(id, key, uniforms, 3);
            Add(Op.PipelineStorage, id, 0, storage, 0);
        }

        void Pipeline(uint id, ulong key, uint uniforms, uint blend)
        {
            var w = new List<uint> { id, (uint)key, (uint)(key >> 32), 0 };
            for (var i = 0; i < 3; i++) w.AddRange(new uint[] { 0, 0 });
            w.Add(0);
            for (var i = 0; i < 16; i++) w.AddRange(new uint[] { 0, 0, 0 });
            w.Add(uniforms);
            w.Add(0); // Textures
            w.AddRange(new uint[] { blend, 0, 0, 0, 0, 0, 1, 0 }); // Blend, depth (none), cull, stencil, into, outputs, alpha_to_coverage
            w.Add(4);
            w.Add(0x74736574); // "test"
            Add(Op.Pipeline, w.ToArray());
        }

        public byte[] Bytes()
        {
            var bytes = new byte[words.Count * 4];
            System.Buffer.BlockCopy(words.ToArray(), 0, bytes, 0, bytes.Length);
            return bytes;
        }
    }

    TideRender render;
    readonly Dictionary<uint, uint[]> answers = new Dictionary<uint, uint[]>();

    [SetUp]
    public void Make()
    {
        if (!TideRender.Supported) Assert.Ignore("This GPU doesn't replay Tide's renderer (TideRender.Supported)");
        render = new GameObject("Tide compute").AddComponent<TideRender>();
        answers.Clear();
        render.ReadsTo = (token, data, size) =>
        {
            uint[] words = null;
            if (data != IntPtr.Zero)
            {
                words = new uint[size / 4];
                var ints = new int[size / 4];
                Marshal.Copy(data, ints, 0, ints.Length);
                System.Buffer.BlockCopy(ints, 0, words, 0, (int)size);
            }
            answers[token] = words;
        };
    }

    [TearDown]
    public void Unmake()
    {
        if (render != null) UnityEngine.Object.Destroy(render.gameObject);
    }

    void Replay(Commands c, Camera camera = null)
    {
        var bytes = c.Bytes();
        fixed (byte* p = bytes) render.Replay(p, (uint)bytes.Length, camera);
    }

    IEnumerator Answers(int count)
    {
        for (var frame = 0; frame < 600 && answers.Count < count; frame++) yield return null;
        Assert.AreEqual(count, answers.Count, "reads back answered");
    }

    static uint Square(uint i, uint add, uint groupsX) => i * i + add + groupsX * 1000 + 100000;

    // A dispatch and an indirect one, each seeing its values, storage and TideGroups; a copy between buffers; a write
    // after the work, which lands after it; reads back of each in order; and a read of nothing failing at once.
    [UnityTest]
    public IEnumerator KernelsRunInOrderAndReadBack()
    {
        var given = new uint[100];
        for (var i = 0u; i < 100; i++) given[i] = i;
        var c = new Commands();
        c.Buffer(1, 400, given);
        c.Buffer(2, 400, null);
        c.Buffer(3, 32, new uint[] { 9, 9, 9, 9, 3, 1, 1, 0 }); // An indirect dispatch's numbers at 16
        c.Kernel(4, Kernel, 16, 2, 2);
        c.Add(Op.ComputeBegin);
        c.Add(Op.Use, 4);
        c.Add(Op.Uniforms, 16, 5, 100, 0, 0);
        c.Add(Op.Storage, 0, 1);
        c.Add(Op.Storage, 1, 2);
        c.Add(Op.Dispatch, 2, 1, 1);
        c.Add(Op.ComputeEnd);
        c.Add(Op.BufferRead, 1, 2, 0, 400);
        c.Add(Op.ComputeBegin);
        c.Add(Op.Use, 4);
        c.Add(Op.Uniforms, 16, 7, 100, 0, 0);
        c.Add(Op.Storage, 0, 1);
        c.Add(Op.Storage, 1, 2);
        c.Add(Op.DispatchIndirect, 3, 16);
        c.Add(Op.ComputeEnd);
        c.Add(Op.BufferRead, 2, 2, 0, 400);
        c.Add(Op.BufferCopy, 2, 4, 1, 8, 16); // made[1..4] over given[2..5]
        c.Add(Op.BufferRead, 3, 1, 0, 32);
        c.Add(Op.BufferWrite, 1, 0, 4, 77); // After the copy: the work before it is done first
        c.Add(Op.BufferRead, 4, 1, 0, 8);
        c.Add(Op.BufferRead, 5, 99, 0, 4); // No such buffer
        Replay(c);
        Assert.IsTrue(answers.ContainsKey(5) && answers[5] == null, "a read of nothing fails at once");
        yield return Answers(5);
        for (var i = 0u; i < 100; i++)
        {
            Assert.AreEqual(Square(i, 5, 2), answers[1][i], $"the dispatch's made[{i}]");
            Assert.AreEqual(Square(i, 7, 3), answers[2][i], $"the indirect dispatch's made[{i}]");
        }
        CollectionAssert.AreEqual(new[] { 0u, 1u, Square(1, 7, 3), Square(2, 7, 3), Square(3, 7, 3), Square(4, 7, 3), 6u, 7u }, answers[3]);
        CollectionAssert.AreEqual(new[] { 77u, 1u }, answers[4]);
    }

    // Indirect draws reading storage in their vertex and fragment stages, into the camera: the left half's draw before a
    // kernel writes the colors' buffer reads it as it was (red), though the camera draws after the kernel ran, and the
    // right half's, after it, what the kernel wrote (green).
    [UnityTest]
    public IEnumerator IndirectDrawsReadStorageWhereTheyAre()
    {
        var camera = new GameObject("Tide compute camera").AddComponent<Camera>();
        var target = new RenderTexture(64, 64, 24);
        try
        {
            camera.targetTexture = target;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            const uint red = 0xff0000ff, green = 0xff00ff00;
            var c = new Commands();
            c.Buffer(1, 24, new[] { Bits(-1), Bits(-1), Bits(3), Bits(-1), Bits(-1), Bits(3) }); // A triangle over everything
            c.Buffer(2, 16, new[] { red, 0u, 0u, 0u });
            c.Buffer(3, 16, new uint[] { 3, 1, 0, 0 }); // Three vertices, once
            c.Buffer(4, 16, null);
            c.Draws(5, Shape, 16, 2);
            c.Kernel(6, Kernel, 16, 2, 2);
            foreach (var half in new uint[] { 0, 32 })
            {
                if (half != 0)
                {
                    // given[0] (0) squared + add + 1000 + 100000 into the colors' first
                    c.Add(Op.ComputeBegin);
                    c.Add(Op.Use, 6);
                    c.Add(Op.Uniforms, 16, green - 101000, 1, 0, 0);
                    c.Add(Op.Storage, 0, 4);
                    c.Add(Op.Storage, 1, 2);
                    c.Add(Op.Dispatch, 1, 1, 1);
                    c.Add(Op.ComputeEnd);
                }
                c.Add(Op.Begin, 0, 0);
                c.Add(Op.Scissor, half, 0, 32, 64);
                c.Add(Op.Use, 5);
                c.Add(Op.Uniforms, 16, 0, 0, 0, 0);
                c.Add(Op.Storage, 0, 1);
                c.Add(Op.Storage, 1, 2);
                c.Add(Op.DrawIndirect, 3, 0);
                c.Add(Op.End);
            }
            c.Add(Op.BufferRead, 1, 2, 0, 4);
            Replay(c, camera);
            camera.Render();
            var read = new Texture2D(64, 64, TextureFormat.RGBA32, false);
            RenderTexture.active = target;
            read.ReadPixels(new Rect(0, 0, 64, 64), 0, 0);
            RenderTexture.active = null;
            Color32 left = read.GetPixel(16, 32), right = read.GetPixel(48, 32);
            UnityEngine.Object.Destroy(read);
            Assert.AreEqual(new Color32(255, 0, 0, 255), left, "the draw before the kernel");
            Assert.AreEqual(new Color32(0, 255, 0, 255), right, "the draw after it");
            yield return Answers(1);
            CollectionAssert.AreEqual(new[] { green }, answers[1]);
        }
        finally
        {
            camera.targetTexture = null;
            UnityEngine.Object.Destroy(camera.gameObject);
            target.Release();
            UnityEngine.Object.Destroy(target);
        }
    }

    static uint Bits(float f) => (uint)BitConverter.SingleToInt32Bits(f);

    // The next frame's commands go on with the objects made, and a storage buffer made with nothing is zeros.
    [UnityTest]
    public IEnumerator BuffersStayAndStartAsZeros()
    {
        var c = new Commands();
        c.Buffer(1, 64, null);
        c.Add(Op.BufferRead, 1, 1, 0, 64);
        Replay(c);
        yield return Answers(1);
        CollectionAssert.AreEqual(new uint[16], answers[1]);
        yield return null;
        c = new Commands();
        c.Add(Op.BufferWrite, 1, 60, 4, 42);
        c.Add(Op.BufferRead, 2, 1, 56, 8);
        Replay(c);
        yield return Answers(2);
        CollectionAssert.AreEqual(new[] { 0u, 42u }, answers[2]);
    }
}
