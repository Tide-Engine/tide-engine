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
//     @group(0) @binding(33) var<storage, read> given: array<u32>;
//     @group(0) @binding(34) var<storage, read_write> made: array<u32>;
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
        Buffer = 1, BufferWrite = 2, Pipeline = 10, Begin = 15, End, Scissor, Use, Uniforms = 20, Indices = 23, PipelineStorage = 26,
        Storage, ComputeBegin, ComputeEnd,
        Dispatch, DispatchIndirect, DrawIndirect, DrawIndexedIndirect, BufferCopy, BufferRead, DrawIndexedIndirectCount,
        Target = 12, Image = 21, Draw = 24, Sampler = 37, Volume = 44, VolumeWrite, PipelineImages,
        Texture = 4, Mipmaps = 6, Cube = 8, CubeFace = 9, Array = 38, ArrayLayer = 39,
        ArrayOf = 40, TextureBlocks = 42, ArrayBlocks = 43, DepthBias = 47,
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

        // ...with depth tested and written (tide_gpu_depth 1)
        public void DepthDraws(uint id, ulong key, uint uniforms, uint storage)
        {
            Pipeline(id, key, uniforms, 3, null, false, 1);
            Add(Op.PipelineStorage, id, 0, storage, 0);
        }

        public void Images(uint id, ulong key, bool compute, uint format = 0)
        {
            Pipeline(id, key, 32, 3, new uint[] { 0, 8 }, !compute);
            if (compute)
            {
                Add(Op.PipelineStorage, id, 1, 0, 0);
                Add(Op.PipelineImages, id, 3, 2, format, format);
            }
        }

        public void LinearCopy(uint id) => Pipeline(id, 0xc0de0015, 16, 3, new uint[] { 0 }, true);

        public void PixelRead(uint id) {
            Pipeline(id, 0xc0de0014, 48, 3, new uint[] { 0 }, true);
            Add(Op.PipelineStorage, id, 1, 3, 1);
        }

        // One of the renderer's read kernels: an image of kind `read`, the floats' pool and the ints', the run's record
        public void ReadKernel(uint id, ulong key, uint uniforms, uint read, bool sampled, bool ints)
        {
            Pipeline(id, key, uniforms, 3, new uint[] { read }, sampled);
            Add(Op.PipelineStorage, id, 1, 3, ints ? 2u : 1u);
        }

        public void Numeric(uint id, ulong key, uint read = 0, uint format = 0, bool kernel = false)
        {
            Pipeline(id, key, 16, 0, kernel ? new uint[] { read } : null);
            if (kernel)
            {
                Add(Op.PipelineStorage, id, 1, 0, 0);
                Add(Op.PipelineImages, id, 1, 1, format);
            }
        }

        public void IntegerReader(uint id, bool volume)
        {
            Pipeline(id, volume ? 0xc0de0051UL : 0xc0de0050UL, 16, 3, new uint[] { volume ? 12u : 10u });
            Add(Op.PipelineStorage, id, 1, 1, 1);
        }

        public void NumericMixed(uint id) => Pipeline(id, 0xc0de0024, 16, 0, null, false, 0, 3);

        public void NumericClear(uint id, uint depth) => Pipeline(id, 0xc0de0048, 64, 3, null, false, depth, 4);

        public void DepthImage(uint id, uint depth) => Pipeline(id, 0xc0de0012, 32, 3, new uint[] { 1 }, true, depth);
        public void LayeredImage(uint id) => Pipeline(id, 0xc0de0013, 32, 3, new uint[] { 3, 6 }, true);

        void Pipeline(uint id, ulong key, uint uniforms, uint blend, uint[] reads = null, bool sampled = false, uint depth = 0, uint outputs = 1)
        {
            var w = new List<uint> { id, (uint)key, (uint)(key >> 32), 0 };
            for (var i = 0; i < 3; i++) w.AddRange(new uint[] { 0, 0 });
            w.Add(0);
            for (var i = 0; i < 16; i++) w.AddRange(new uint[] { 0, 0, 0 });
            w.Add(uniforms);
            w.Add((uint)(reads?.Length ?? 0));
            if (reads != null) w.AddRange(reads);
            w.Add(sampled ? 1u : 0u);
            if (sampled) w.Add(0); // One shared color sampler at binding 2
            if (reads != null) foreach (var _ in reads) w.Add(sampled ? 1u : 0u);
            w.AddRange(new uint[] { blend, depth, 0, 0, 0, 0, outputs, 0 }); // Blend, depth, cull, stencil, into, outputs, alpha_to_coverage
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

    // A draw of records as many as a count says (gpu.h's draw_indexed_indirect_count): each of its records drawn, those
    // past the count of no instances: the left half's first record draws, the right half's are all of none.
    [UnityTest]
    public IEnumerator CountedDrawsDrawTheirRecords()
    {
        var camera = new GameObject("Tide count camera").AddComponent<Camera>();
        var target = new RenderTexture(64, 64, 24);
        try
        {
            camera.targetTexture = target;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            var c = new Commands();
            c.Buffer(1, 24, new[] { Bits(-1), Bits(-1), Bits(3), Bits(-1), Bits(-1), Bits(3) });
            c.Buffer(2, 16, new uint[] { 0xff0000ff, 0, 0, 0 });
            // Records, five words each: three corners once, then none; then two of none
            c.Buffer(3, 60, new uint[] { 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0 });
            c.Buffer(4, 16, new uint[] { 1, 0, 0, 0 }); // The counts: one, and none
            c.Add(Op.Buffer, 5, 1, 12, 1, 0, 1, 2); // The corners' indices, 32 bits each
            c.Draws(6, Shape, 16, 2);
            foreach (var half in new uint[] { 0, 32 })
            {
                c.Add(Op.Begin, 0, 0);
                c.Add(Op.Scissor, half, 0, 32, 64);
                c.Add(Op.Use, 6);
                c.Add(Op.Uniforms, 16, 0, 0, 0, 0);
                c.Add(Op.Storage, 0, 1);
                c.Add(Op.Storage, 1, 2);
                c.Add(Op.Indices, 5, 1);
                c.Add(Op.DrawIndexedIndirectCount, 3, half == 0 ? 0u : 20u, 2, 4, half == 0 ? 0u : 4u);
                c.Add(Op.End);
            }
            Replay(c, camera);
            camera.Render();
            var read = new Texture2D(64, 64, TextureFormat.RGBA32, false);
            RenderTexture.active = target;
            read.ReadPixels(new Rect(0, 0, 64, 64), 0, 0);
            RenderTexture.active = null;
            Color32 left = read.GetPixel(16, 32), right = read.GetPixel(48, 32);
            UnityEngine.Object.Destroy(read);
            Assert.AreEqual(new Color32(255, 0, 0, 255), left, "the record of three corners");
            Assert.AreEqual(new Color32(0, 0, 0, 255), right, "records of none");
            yield return null;
        }
        finally
        {
            camera.targetTexture = null;
            UnityEngine.Object.Destroy(camera.gameObject);
            target.Release();
            UnityEngine.Object.Destroy(target);
        }
    }

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

    // The WGSL sources beside these shaders exercise the same translator as a game's kernels and frags.
    const ulong ImageKernel = 0xc0de0010, ImageDraw = 0xc0de0011;

    static Commands ImageObjects(uint volumeFormat = 0, uint targetFormat = 1, uint storageFormat = 0)
    {
        var c = new Commands();
        c.Add(Op.Target, 10, targetFormat, 64, 64, 11, 0, 0, 0, 12);
        c.Add(Op.Volume, 20, volumeFormat, 2, 2, 2);
        c.Images(30, ImageKernel, true, storageFormat);
        c.Images(31, ImageDraw, false);
        return c;
    }

    static void ImageValues(Commands c, float x, float y, float z, float kind, Color color)
    {
        c.Add(Op.Uniforms, 32, Bits(x), Bits(y), Bits(z), Bits(kind), Bits(color.r), Bits(color.g), Bits(color.b), Bits(color.a));
    }

    static void ImageQuad(Commands c, uint target, uint x, uint y, uint width, uint height,
                          float u, float v, float w, float kind, Color color, uint filter = 1, uint wrap = 0, uint source = 11)
    {
        c.Add(Op.Begin, target, 0);
        c.Add(Op.Scissor, x, y, width, height);
        c.Add(Op.Use, 31);
        ImageValues(c, u, v, w, kind, color);
        c.Add(Op.Image, 0, target == 10 ? 0u : source);
        c.Add(Op.Image, 1, 20);
        c.Add(Op.Sampler, 0, filter, wrap);
        c.Add(Op.Draw, 0, 3, 1);
        c.Add(Op.End);
    }

    static void FillImages(Commands c)
    {
        c.Add(Op.ComputeBegin);
        c.Add(Op.Use, 30);
        // Green in the image, the invocation's xyz gradient in the volume
        ImageValues(c, 0, 1, 0, 1, new Color(1, 0, 0, 0));
        c.Add(Op.Image, 0, 11);
        c.Add(Op.Image, 1, 20);
        c.Add(Op.Dispatch, 16, 16, 1);
        c.Add(Op.ComputeEnd);
    }

    static Texture2D Pixels(Camera camera, RenderTexture target)
    {
        camera.Render();
        var pixels = new Texture2D(64, 64, TextureFormat.RGBA32, false);
        RenderTexture.active = target;
        pixels.ReadPixels(new Rect(0, 0, 64, 64), 0, 0);
        RenderTexture.active = null;
        return pixels;
    }

    static void Filtered(Color32 expected, Color32 actual, string why)
    {
        // Filtering and UNorm conversion may round a half-byte either way. This still rejects nearest-cell reads
        // or blending only two of the axes; the images' unfiltered order witnesses above remain byte-exact.
        Assert.That((int)actual.r, Is.InRange(Math.Max(0, expected.r - 1), Math.Min(255, expected.r + 1)), why + " red");
        Assert.That((int)actual.g, Is.InRange(Math.Max(0, expected.g - 1), Math.Min(255, expected.g + 1)), why + " green");
        Assert.That((int)actual.b, Is.InRange(Math.Max(0, expected.b - 1), Math.Min(255, expected.b + 1)), why + " blue");
        Assert.AreEqual(expected.a, actual.a, why + " alpha");
    }

    Texture ImageTexture(uint id)
    {
        var images = (System.Collections.IDictionary)typeof(TideRender).GetField("images", System.Reflection.BindingFlags.Instance
            | System.Reflection.BindingFlags.NonPublic).GetValue(render);
        var image = images[id];
        return (Texture)image.GetType().GetField("texture").GetValue(image);
    }

    // Integer Texture readback is not exposed by Unity's OpenGL API. Load to an integer buffer first, using
    // the same portable shader path as a game, then verify every bit through the replay's ordinary buffer reads.
    IEnumerator ReadIntegerImage(uint image, bool volume, uint count)
    {
        answers.Clear();
        var c = new Commands();
        c.Buffer(140, count * 4, null);
        c.IntegerReader(141, volume);
        c.Add(Op.ComputeBegin);
        c.Add(Op.Use, 141);
        c.Add(Op.Uniforms, 16, 0, 0, 0, 0);
        c.Add(Op.Image, 0, image);
        c.Add(Op.Storage, 0, 140);
        c.Add(Op.Dispatch, 1, 1, 1);
        c.Add(Op.ComputeEnd);
        c.Add(Op.BufferRead, 1, 140, 0, count * 4);
        Replay(c);
        yield return Answers(1);
        Assert.IsNotNull(answers[1], "integer image load/readback");
    }

    // These shaders' outputs are numbers, so each replaces its image under Alpha, and a fourth component
    // remains a number. The large signed ID detects a float conversion on any replay API.
    [UnityTest]
    public IEnumerator NumericOutputsKeepTheirExactValues()
    {
        foreach (var format in new uint[] { 2, 3, 4, 5, 6, 7, 8, 9 })
        {
            var scalar = format == 3 || format == 5 || format == 7;
            var pair = format == 4 || format == 6;
            var integer = format == 9;
            var key = integer ? 0xc0de0023UL : pair ? 0xc0de0022UL : scalar ? 0xc0de0021UL : 0xc0de0020UL;
            var c = new Commands();
            c.Add(Op.Target, 100, format, 4, 4, 101, 0, 0, 0, 102);
            c.Numeric(110, key);
            c.Add(Op.Begin, 100, 1);
            c.Add(Op.Use, 110);
            c.Add(Op.Uniforms, 16, 0, 0, 0, 0);
            c.Add(Op.Draw, 0, 3, 1);
            c.Add(Op.End);
            Replay(c);
            if (integer)
            {
                yield return ReadIntegerImage(101, false, 16);
                foreach (var value in answers[1]) Assert.AreEqual(-16777217, unchecked((int)value), "signed ID stays exact");
            }
            else
            {
                var read = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(101), 0, TextureFormat.RGBAFloat);
                while (!read.done) yield return null;
                Assert.IsFalse(read.hasError, $"format {format} readback");
                var values = read.GetData<float>();
                var expected = new[] { format == 3 || format == 4 ? 1f : 1.5f,
                    scalar ? 0f : format == 4 ? 0f : -0.5f, scalar || pair ? 0f : 2f, scalar || pair ? 1f : 0.125f };
                for (var i = 0; i < values.Length; i++) if (i % 4 < (scalar ? 1 : pair ? 2 : 4))
                    Assert.AreEqual(expected[i % 4], values[i], $"format {format} component {i}");
            }
            // New ids replace and release the previous target's attachments through the normal replay path.
        }
    }

    [UnityTest]
    public IEnumerator NumericOutputsReplaceWhileColorStillBlends()
    {
        var c = new Commands();
        c.Add(Op.Target, 300, 0x982, 4, 4, 301, 302, 303, 0, 304);
        c.NumericMixed(310);
        c.Add(Op.Begin, 300, 1);
        c.Add(Op.Use, 310);
        c.Add(Op.Uniforms, 16, 0, 0, 0, 0);
        c.Add(Op.Draw, 0, 3, 1);
        c.Add(Op.Draw, 0, 3, 1);
        c.Add(Op.End);
        Replay(c);
        foreach (var id in new uint[] { 301, 302 })
        {
            var read = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(id), 0, TextureFormat.RGBAFloat);
            while (!read.done) yield return null;
            Assert.IsFalse(read.hasError);
            var values = read.GetData<float>();
            var expected = id == 301 ? new[] { 0.75f, 0f, 0f, 1f } : new[] { 1.5f, -0.5f, 2f, 0.125f };
            for (var i = 0; i < values.Length; i++) Assert.AreEqual(expected[i % 4], values[i], $"mixed image {id} component {i}");
        }
        yield return ReadIntegerImage(303, false, 16);
        foreach (var value in answers[1]) Assert.AreEqual(-16777217, unchecked((int)value), "mixed signed ID stays exact");
    }

    [UnityTest]
    public IEnumerator NumericClearsKeepValuesClipsAndDepthOnly()
    {
        var c = new Commands();
        c.Add(Op.Target, 200, 0x9867, 4, 4, 201, 202, 203, 204, 205);
        c.NumericClear(210, 3);
        c.NumericClear(211, 6); // RESET_ALONE clears depth without touching even the integer attachment
        c.Add(Op.Begin, 200, 1);
        c.Add(Op.Use, 210);
        c.Add(Op.Uniforms, 64, Bits(1.5f), 0, 0, 0, Bits(2.5f), Bits(-0.5f), 0, 0,
            Bits(3.5f), Bits(-0.5f), Bits(2), Bits(0.25f), unchecked((uint)-16777217), 0, 0, 0);
        c.Add(Op.Draw, 0, 3, 1);
        c.Add(Op.Scissor, 0, 0, 2, 2);
        c.Add(Op.Uniforms, 64, Bits(4.5f), 0, 0, 0, Bits(5.5f), Bits(-0.25f), 0, 0,
            Bits(6.5f), Bits(-0.25f), Bits(4), Bits(0.125f), unchecked((uint)-33554433), 0, 0, 0);
        c.Add(Op.Draw, 0, 3, 1);
        c.Add(Op.Scissor, 0, 0, 4, 4);
        c.Add(Op.Use, 211);
        c.Add(Op.Uniforms, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        c.Add(Op.Draw, 0, 3, 1);
        c.Add(Op.End);
        Replay(c);
        for (uint image = 201; image <= 204; image++)
        {
            var integer = image == 204;
            var changed = 0;
            if (integer)
            {
                yield return ReadIntegerImage(image, false, 16);
                foreach (var word in answers[1])
                {
                    var value = unchecked((int)word);
                    if (value == -33554433) changed++;
                    else Assert.AreEqual(-16777217, value, "integer clear is exact and depth-only keeps it");
                }
            }
            else
            {
                var read = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(image), 0, TextureFormat.RGBAFloat);
                while (!read.done) yield return null;
                Assert.IsFalse(read.hasError, $"clear image {image}");
                var values = read.GetData<float>();
                var oldRed = image == 201 ? 1.5f : image == 202 ? 2.5f : 3.5f;
                for (var i = 0; i < values.Length; i += 4)
                {
                    if (values[i] == oldRed + 3) changed++;
                    else Assert.AreEqual(oldRed, values[i], "float clear past one survives depth-only");
                    if (image >= 202) Assert.AreEqual(values[i] == oldRed ? -0.5f : -0.25f, values[i + 1]);
                    if (image == 203) Assert.AreEqual(values[i] == oldRed ? 0.25f : 0.125f, values[i + 3], "fourth channel is a free number");
                }
            }
            Assert.AreEqual(4, changed, $"clear image {image} obeys its two-by-two clip");
        }
    }

    [UnityTest]
    public IEnumerator NumericVolumesKeepTheirExactValues()
    {
        foreach (var format in new uint[] { 5, 6, 7 }) // tide_gpu_format R32F, RGBA32F, R32I
        {
            var integer = format == 7;
            var c = new Commands();
            c.Add(Op.Volume, 120, format, 2, 2, 2);
            c.Numeric(130, 0xc0de0030UL + format - 5, integer ? 12u : 11u, format, true);
            c.Add(Op.ComputeBegin);
            c.Add(Op.Use, 130);
            c.Add(Op.Uniforms, 16, 0, 0, 0, 0);
            c.Add(Op.Image, 0, 120);
            c.Add(Op.Dispatch, 2, 2, 2);
            c.Add(Op.ComputeEnd);
            Replay(c);
            if (integer)
            {
                yield return ReadIntegerImage(120, true, 8);
                foreach (var value in answers[1]) Assert.AreEqual(-16777217, unchecked((int)value), "volume signed ID stays exact");
            }
            else
            {
                var read = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(120), 0, TextureFormat.RGBAFloat);
                while (!read.done) yield return null;
                Assert.IsFalse(read.hasError, $"numeric volume {format} readback");
                for (var layer = 0; layer < read.layerCount; layer++)
                {
                    var values = read.GetData<float>(layer);
                    var expected = format == 5 ? new[] { 1.5f, 0f, 0f, 1f } : new[] { 1.5f, -0.5f, 2f, 0.125f };
                    for (var i = 0; i < values.Length; i++) if (format != 5 || i % 4 == 0)
                        Assert.AreEqual(expected[i % 4], values[i], $"volume {format} component {i}");
                }
            }
        }
    }

    [UnityTest]
    public IEnumerator SrgbImagesDecodeBeforeFilteringAndKeepFormatAcrossUploads()
    {
        if (QualitySettings.activeColorSpace != ColorSpace.Linear) Assert.Ignore("Unity disables sRGB texture views in Gamma projects; the refusal is tested separately.");
        var camera = new GameObject("Tide sRGB camera").AddComponent<Camera>();
        var target = new RenderTexture(64, 64, 24);
        Texture2D pixels = null;
        try
        {
            camera.targetTexture = target;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            var c = ImageObjects();
            c.Add(Op.Texture, 40, 0, 2, 1, 1, 0xff808080, 0xffffffff);
            c.Add(Op.Texture, 41, 8, 2, 1, 1, 0xff808080, 0xffffffff);
            c.Add(Op.Array, 42, 10, 2, 1, 2, 1, 0, 0xff808080, 0xffffffff, 0xff808080, 0xff808080);
            var faces = new List<uint> { 43, 10, 2, 1 };
            for (var i = 0; i < 6; i++) faces.AddRange(new uint[] { 0xff808080, 0xffffffff, 0xff808080, 0xffffffff });
            c.Add(Op.Cube, faces.ToArray());
            ImageQuad(c, 0, 0, 0, 16, 64, 0.25f, 0.5f, 0, 0, Color.black, 0, source: 40);
            ImageQuad(c, 0, 16, 0, 16, 64, 0.25f, 0.5f, 0, 0, Color.black, 0, source: 41);
            ImageQuad(c, 0, 32, 0, 16, 64, 0.5f, 0.5f, 0, 0, Color.black, source: 40);
            ImageQuad(c, 0, 48, 0, 16, 64, 0.5f, 0.5f, 0, 0, Color.black, source: 41);
            Replay(c, camera);
            Assert.AreEqual(UnityEngine.Experimental.Rendering.GraphicsFormat.R8G8B8A8_SRGB, ImageTexture(41).graphicsFormat, "marked texture actual format");
            pixels = Pixels(camera, target);
            Filtered(new Color32(128, 128, 128, 255), (Color32)pixels.GetPixel(8, 32), "unmarked point reads stored numbers");
            Filtered(new Color32(55, 55, 55, 255), (Color32)pixels.GetPixel(24, 32), "marked point decodes sRGB");
            Filtered(new Color32(192, 192, 192, 255), (Color32)pixels.GetPixel(40, 32), "unmarked midpoint filters stored numbers");
            Filtered(new Color32(155, 155, 155, 255), (Color32)pixels.GetPixel(56, 32), "marked midpoint filters linear light");
            Assert.AreEqual(UnityEngine.Experimental.Rendering.GraphicsFormat.R8G8B8A8_SRGB, ImageTexture(41).graphicsFormat);
            c = new Commands();
            c.Add(Op.Mipmaps, 41);
            c.Add(Op.Mipmaps, 43);
            Replay(c);
            var read = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(41), 1);
            while (!read.done) yield return null;
            Assert.IsFalse(read.hasError, "marked mip readback");
            var mip = read.GetData<byte>();
            Assert.That((int)mip[0], Is.InRange(204, 206), "marked mip is encoded from the mean linear light");
            Assert.AreEqual(255, mip[3], "alpha never follows the color transfer curve");
            var arrayRead = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(42), 1);
            var cubeRead = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(43), 1);
            while (!arrayRead.done || !cubeRead.done) yield return null;
            Assert.IsFalse(arrayRead.hasError || cubeRead.hasError, "layered sRGB mip readback");
            Assert.That((int)arrayRead.GetData<byte>(0)[0], Is.InRange(204, 206), "array mip uses linear light");
            Assert.That((int)arrayRead.GetData<byte>(1)[0], Is.InRange(127, 129), "other array layer stays independent");
            for (var face = 0; face < 6; face++)
                Assert.That((int)cubeRead.GetData<byte>(face)[0], Is.InRange(204, 206), "cube face mip " + face);
            yield return null;
        }
        finally
        {
            if (pixels != null) UnityEngine.Object.Destroy(pixels);
            camera.targetTexture = null;
            UnityEngine.Object.Destroy(camera.gameObject);
            target.Release();
            UnityEngine.Object.Destroy(target);
        }
    }

    [Test]
    public void SrgbLayeredAndBlockImagesKeepTheirStorageFormatsAndMipCounts()
    {
        if (QualitySettings.activeColorSpace != ColorSpace.Linear) Assert.Ignore("Unity disables sRGB texture views in Gamma projects; the refusal is tested separately.");
        var c = new Commands();
        c.Add(Op.Cube, 40, 10, 1, 1, 0xff808080, 0xff808080, 0xff808080, 0xff808080, 0xff808080, 0xff808080);
        c.Add(Op.Array, 41, 10, 1, 1, 2, 1, 0, 0xff808080, 0xffffffff);
        c.Add(Op.ArrayOf, 42, 8, 2, 2, 2);
        var rgba = new List<uint> { 43, 4, 4, 0x104, 3, 84 };
        for (var i = 0; i < 21; i++) rgba.Add(0xff808080);
        c.Add(Op.TextureBlocks, rgba.ToArray());
        rgba = new List<uint> { 44, 4, 4, 1, 0x104, 3, 84 };
        for (var i = 0; i < 21; i++) rgba.Add(0xff808080);
        c.Add(Op.ArrayBlocks, rgba.ToArray());
        var bc1 = SystemInfo.SupportsTextureFormat(TextureFormat.DXT1);
        var bc3 = SystemInfo.SupportsTextureFormat(TextureFormat.DXT5);
        if (bc1) c.Add(Op.TextureBlocks, 45, 4, 4, 0x101, 3, 24, 0x84108410, 0, 0x84108410, 0, 0x84108410, 0);
        if (bc3) c.Add(Op.ArrayBlocks, 46, 4, 4, 1, 0x103, 3, 48,
            0xffff, 0, 0x84108410, 0, 0xffff, 0, 0x84108410, 0, 0xffff, 0, 0x84108410, 0);
        Replay(c);
        foreach (var id in new uint[] { 40, 41, 42, 43, 44 })
            Assert.AreEqual(UnityEngine.Experimental.Rendering.GraphicsFormat.R8G8B8A8_SRGB, ImageTexture(id).graphicsFormat, "sRGB image " + id);
        Assert.AreEqual(3, ImageTexture(43).mipmapCount, "RGBA8 chain levels retained");
        Assert.AreEqual(3, ImageTexture(44).mipmapCount, "array chain levels retained");
        if (bc1) Assert.AreEqual(UnityEngine.Experimental.Rendering.GraphicsFormat.RGBA_DXT1_SRGB, ImageTexture(45).graphicsFormat);
        if (bc3) Assert.AreEqual(UnityEngine.Experimental.Rendering.GraphicsFormat.RGBA_DXT5_SRGB, ImageTexture(46).graphicsFormat);
    }

    [Test]
    public void GammaProjectsExplainMarkedImagesAndKeepUnmarkedImages()
    {
        if (QualitySettings.activeColorSpace != ColorSpace.Gamma) Assert.Ignore("This witness needs a Gamma project.");
        var c = new Commands();
        c.Add(Op.Texture, 40, 0, 1, 1, 1, 0xff808080);
        c.Add(Op.Texture, 41, 8, 1, 1, 1, 0xff808080);
        c.Add(Op.ArrayOf, 42, 8, 1, 1, 1); // The same reason is reported just once per renderer.
        LogAssert.Expect(LogType.Error, "Tide: [Linear] textures require Unity's Linear color space. Set Project Settings > Player > Other Settings > Color Space to Linear; Unity's Gamma mode disables sRGB image formats.");
        Replay(c);
        Assert.AreEqual(UnityEngine.Experimental.Rendering.GraphicsFormat.R8G8B8A8_UNorm, ImageTexture(40).graphicsFormat);
        var images = (System.Collections.IDictionary)typeof(TideRender).GetField("images", System.Reflection.BindingFlags.Instance
            | System.Reflection.BindingFlags.NonPublic).GetValue(render);
        Assert.IsFalse(images.Contains(41u), "marked image was refused");
        Assert.IsFalse(images.Contains(42u), "marked array was refused");
    }

    [UnityTest]
    public IEnumerator MarkedTargetSamplingCopyConvertsPremultipliedColorAndKeepsTheOriginal()
    {
        if (QualitySettings.activeColorSpace != ColorSpace.Linear) Assert.Ignore("Unity disables sRGB texture views in Gamma projects.");
        var c = ImageObjects();
        // Hand-written GPU shaders output premultiplied channels: straight (.5,.25,1), alpha .5.
        ImageQuad(c, 10, 0, 0, 64, 64, 0, 0, 0, -1, new Color(0.25f, 0.125f, 0.5f, 0.5f));
        Replay(c);
        var beforeRead = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(11), 0);
        while (!beforeRead.done) yield return null;
        Assert.IsFalse(beforeRead.hasError);
        var before = beforeRead.GetData<byte>().ToArray();
        c = new Commands();
        c.Add(Op.Target, 50, 10, 64, 64, 51, 0, 0, 0, 52);
        c.LinearCopy(60);
        c.Add(Op.Begin, 50, 0);
        c.Add(Op.Scissor, 0, 0, 64, 64);
        c.Add(Op.Use, 60);
        c.Add(Op.Uniforms, 16, 0, 0, 0, 0);
        c.Add(Op.Image, 0, 11);
        c.Add(Op.Sampler, 0, 0, 0);
        c.Add(Op.Draw, 0, 3, 1);
        c.Add(Op.End);
        c.Add(Op.Mipmaps, 51);
        Replay(c);
        Assert.Greater(ImageTexture(51).mipmapCount, 1, "private sRGB target retains its mip chain");
        var source = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(11), 0);
        var sampled = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(51), 0);
        while (!source.done || !sampled.done) yield return null;
        Assert.IsFalse(source.hasError || sampled.hasError, "target copy readback");
        var original = source.GetData<byte>();
        var converted = sampled.GetData<byte>();
        CollectionAssert.AreEqual(before, original.ToArray(), "sampling conversion never changes the original target");
        for (var i = 0; i < original.Length; i += 4)
        {
            Assert.That((int)original[i], Is.InRange(63, 65), "original red remains display-premultiplied");
            Assert.That((int)original[i + 1], Is.InRange(31, 33), "original green remains display-premultiplied");
            Assert.That((int)original[i + 2], Is.InRange(127, 129), "original blue remains display-premultiplied");
            Assert.That((int)converted[i], Is.InRange(91, 94), "linear-premultiplied red encoded for sampling");
            Assert.That((int)converted[i + 1], Is.InRange(43, 46), "linear-premultiplied green encoded for sampling");
            Assert.That((int)converted[i + 2], Is.InRange(187, 190), "linear-premultiplied blue encoded for sampling");
            Assert.AreEqual(original[i + 3], converted[i + 3], "alpha is unchanged");
        }
    }

    [UnityTest]
    public IEnumerator ImagesRespectDrawAndRunOrder()
    {
        var camera = new GameObject("Tide image order camera").AddComponent<Camera>();
        var target = new RenderTexture(64, 64, 24);
        Texture2D pixels = null;
        try
        {
            camera.targetTexture = target;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            var c = ImageObjects();
            ImageQuad(c, 10, 0, 0, 64, 64, 0, 0, 0, -1, Color.red);
            ImageQuad(c, 0, 0, 0, 20, 64, 0.5f, 0.5f, 0, 0, Color.black);
            FillImages(c);
            ImageQuad(c, 0, 20, 0, 20, 64, 0.5f, 0.5f, 0, 0, Color.black);
            ImageQuad(c, 10, 0, 0, 64, 64, 0, 0, 0, -1, Color.blue);
            ImageQuad(c, 0, 40, 0, 24, 64, 0.5f, 0.5f, 0, 0, Color.black);
            Replay(c, camera);
            pixels = Pixels(camera, target);
            Assert.AreEqual(new Color32(255, 0, 0, 255), (Color32)pixels.GetPixel(10, 32), "draw before the kernel sees the earlier target draw");
            Assert.AreEqual(new Color32(0, 255, 0, 255), (Color32)pixels.GetPixel(30, 32), "draw after the kernel retains its image");
            Assert.AreEqual(new Color32(0, 0, 255, 255), (Color32)pixels.GetPixel(52, 32), "later target draw overwrites the kernel");
            yield return null;
        }
        finally
        {
            if (pixels != null) UnityEngine.Object.Destroy(pixels);
            camera.targetTexture = null;
            UnityEngine.Object.Destroy(camera.gameObject);
            target.Release();
            UnityEngine.Object.Destroy(target);
        }
    }

    [UnityTest]
    public IEnumerator VolumesBlendEveryAxisAndUploadOnlyTheBox()
    {
        var camera = new GameObject("Tide volume camera").AddComponent<Camera>();
        var target = new RenderTexture(64, 64, 24);
        Texture2D pixels = null;
        try
        {
            camera.targetTexture = target;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            var c = ImageObjects();
            FillImages(c);
            ImageQuad(c, 0, 0, 0, 32, 32, 0.5f, 0.25f, 0.25f, 1, Color.black);
            ImageQuad(c, 0, 32, 0, 32, 32, 0.25f, 0.5f, 0.25f, 1, Color.black);
            ImageQuad(c, 0, 0, 32, 32, 32, 0.25f, 0.25f, 0.5f, 1, Color.black);
            ImageQuad(c, 0, 32, 32, 32, 32, 0.5f, 0.5f, 0.5f, 1, Color.black);
            Replay(c, camera);
            pixels = Pixels(camera, target);
            Filtered(new Color32(128, 0, 0, 255), (Color32)pixels.GetPixel(16, 48), "x blends between cells");
            Filtered(new Color32(0, 128, 0, 255), (Color32)pixels.GetPixel(48, 48), "y blends between cells");
            Filtered(new Color32(0, 0, 128, 255), (Color32)pixels.GetPixel(16, 16), "z blends between cells");
            Filtered(new Color32(128, 128, 128, 255), (Color32)pixels.GetPixel(48, 16), "all axes blend together");
            c = new Commands();
            c.Add(Op.VolumeWrite, 20, 1, 1, 1, 1, 1, 1, 0xffff00ff);
            Replay(c);
            var read = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(20));
            while (!read.done) yield return null;
            Assert.IsFalse(read.hasError, "volume readback");
            Assert.AreEqual(2, read.layerCount, "volume slices read back");
            for (var z = 0; z < 2; z++)
            {
                var data = read.GetData<byte>(z);
                Assert.AreEqual(16, data.Length);
                for (var y = 0; y < 2; y++)
                    for (var x = 0; x < 2; x++)
                    {
                        var at = (y * 2 + x) * 4;
                        var color = new Color32(data[at], data[at + 1], data[at + 2], data[at + 3]);
                        Assert.AreEqual(x == 1 && y == 1 && z == 1 ? new Color32(255, 0, 255, 255)
                            : new Color32((byte)(x * 255), (byte)(y * 255), (byte)(z * 255), 255), color, $"cell {x},{y},{z}");
                    }
            }
        }
        finally
        {
            if (pixels != null) UnityEngine.Object.Destroy(pixels);
            camera.targetTexture = null;
            UnityEngine.Object.Destroy(camera.gameObject);
            target.Release();
            UnityEngine.Object.Destroy(target);
        }
    }

    // Grid3<float>'s volume is R16F, sampled without a writable storage binding. A single occupied far corner
    // distinguishes a 3D filter (one eighth in the middle) from an array of independently filtered 2D layers.
    [UnityTest]
    public IEnumerator HalfFloatGridVolumesBlendAcrossLayers()
    {
        var camera = new GameObject("Tide grid volume camera").AddComponent<Camera>();
        var target = new RenderTexture(64, 64, 24);
        Texture2D pixels = null;
        try
        {
            camera.targetTexture = target;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            var c = ImageObjects(2);
            c.Add(Op.VolumeWrite, 20, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0x3c000000);
            ImageQuad(c, 0, 0, 0, 32, 32, 0.5f, 0.75f, 0.75f, 1, Color.black);
            ImageQuad(c, 0, 32, 0, 32, 32, 0.75f, 0.5f, 0.75f, 1, Color.black);
            ImageQuad(c, 0, 0, 32, 32, 32, 0.75f, 0.75f, 0.5f, 1, Color.black);
            ImageQuad(c, 0, 32, 32, 32, 32, 0.5f, 0.5f, 0.5f, 1, Color.black);
            ImageQuad(c, 0, 0, 56, 8, 8, 1.25f, 0.75f, 0.75f, 1, Color.black, 1, 1);
            ImageQuad(c, 0, 8, 56, 8, 8, 1.25f, 0.75f, 0.75f, 1, Color.black, 1, 2);
            ImageQuad(c, 0, 16, 56, 8, 8, 0.75f, 1.25f, 0.75f, 1, Color.black, 1, 1);
            ImageQuad(c, 0, 24, 56, 8, 8, 0.75f, 1.25f, 0.75f, 1, Color.black, 1, 2);
            ImageQuad(c, 0, 32, 56, 8, 8, 0.75f, 0.75f, 1.25f, 1, Color.black, 1, 1);
            ImageQuad(c, 0, 40, 56, 8, 8, 0.75f, 0.75f, 1.25f, 1, Color.black, 1, 2);
            ImageQuad(c, 0, 56, 56, 8, 8, 0.5f, 0.5f, 0.5f, 1, Color.black, 0);
            Replay(c, camera);
            pixels = Pixels(camera, target);
            Filtered(new Color32(128, 0, 0, 255), (Color32)pixels.GetPixel(16, 48), "half-float x");
            Filtered(new Color32(128, 0, 0, 255), (Color32)pixels.GetPixel(48, 48), "half-float y");
            Filtered(new Color32(128, 0, 0, 255), (Color32)pixels.GetPixel(16, 16), "half-float z");
            Filtered(new Color32(32, 0, 0, 255), (Color32)pixels.GetPixel(48, 16), "half-float three axes");
            for (var axis = 0; axis < 3; axis++)
            {
                Assert.AreEqual(new Color32(0, 0, 0, 255), (Color32)pixels.GetPixel(axis * 16 + 4, 4), $"repeat axis {axis}");
                Assert.AreEqual(new Color32(255, 0, 0, 255), (Color32)pixels.GetPixel(axis * 16 + 12, 4), $"mirror axis {axis}");
            }
            Assert.AreEqual(new Color32(255, 0, 0, 255), (Color32)pixels.GetPixel(60, 4), "point alongside linear");
            yield return null;
        }
        finally
        {
            if (pixels != null) UnityEngine.Object.Destroy(pixels);
            camera.targetTexture = null;
            UnityEngine.Object.Destroy(camera.gameObject);
            target.Release();
            UnityEngine.Object.Destroy(target);
        }
    }

    [UnityTest]
    public IEnumerator WritableHalfFloatImagesKeepValuesWithoutACamera()
    {
        // HLSL's typed float4 UAV works for either portable RGBA format. Metadata still checks the actual image.
        var c = ImageObjects(4, 2, 4);
        c.Add(Op.ComputeBegin);
        c.Add(Op.Use, 30);
        ImageValues(c, 2.5f, 0.25f, 4, 0.5f, Color.clear);
        c.Add(Op.Image, 0, 11);
        c.Add(Op.Image, 1, 20);
        c.Add(Op.Dispatch, 16, 16, 1);
        c.Add(Op.ComputeEnd);
        Replay(c);
        foreach (var id in new uint[] { 11, 20 })
        {
            var read = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(id), 0, TextureFormat.RGBAFloat);
            while (!read.done) yield return null;
            Assert.IsFalse(read.hasError);
            var values = read.GetData<float>();
            Assert.AreEqual(2.5f, values[0], $"image {id} red");
            Assert.AreEqual(0.25f, values[1], $"image {id} green");
            Assert.AreEqual(4, values[2], $"image {id} blue");
            Assert.AreEqual(0.5f, values[3], $"image {id} alpha");
        }
    }

    [UnityTest]
    public IEnumerator PendingCameraDrawsKeepEarlierTargetDepth()
    {
        var camera = new GameObject("Tide depth order camera").AddComponent<Camera>();
        var target = new RenderTexture(64, 64, 24);
        Texture2D pixels = null;
        try
        {
            camera.targetTexture = target;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            var c = ImageObjects();
            c.DepthImage(32, 1);
            c.DepthImage(33, 0);
            foreach (var half in new uint[] { 0, 32 })
            {
                c.Add(Op.Begin, 10, 1);
                c.Add(Op.Use, 32);
                ImageValues(c, 0, 0, 0, -1, new Color(0, 0, 0, half == 0 ? 0.25f : 0.75f));
                c.Add(Op.Image, 0, 0);
                c.Add(Op.Draw, 0, 3, 1);
                c.Add(Op.End);
                c.Add(Op.Begin, 0, 0);
                c.Add(Op.Scissor, half, 0, 32, 64);
                c.Add(Op.Use, 33);
                ImageValues(c, 0.5f, 0.5f, 0, 0, Color.black);
                c.Add(Op.Image, 0, 12);
                c.Add(Op.Sampler, 0, 0, 0);
                c.Add(Op.Draw, 0, 3, 1);
                c.Add(Op.End);
            }
            Replay(c, camera);
            pixels = Pixels(camera, target);
            Filtered(new Color32(64, 64, 64, 255), (Color32)pixels.GetPixel(16, 32), "earlier target depth");
            Filtered(new Color32(191, 191, 191, 255), (Color32)pixels.GetPixel(48, 32), "later target depth");
            yield return null;
        }
        finally
        {
            if (pixels != null) UnityEngine.Object.Destroy(pixels);
            camera.targetTexture = null;
            UnityEngine.Object.Destroy(camera.gameObject);
            target.Release();
            UnityEngine.Object.Destroy(target);
        }
    }

    [UnityTest]
    public IEnumerator PendingCameraDrawsKeepEarlierCubeFacesAndArrayLayers()
    {
        var camera = new GameObject("Tide layered order camera").AddComponent<Camera>();
        var target = new RenderTexture(64, 64, 24);
        Texture2D pixels = null;
        try
        {
            camera.targetTexture = target;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            var c = new Commands();
            c.Add(Op.Cube, 20, 1, 64, 0);
            c.Add(Op.CubeFace, 21, 20, 0, 22);
            c.Add(Op.Array, 30, 1, 64, 64, 2, 0, 31);
            c.Add(Op.ArrayLayer, 32, 30, 1);
            c.Add(Op.Cube, 40, 1, 64, 0); // Unused compatible bindings while painting, so no attachment is sampled
            c.Add(Op.Array, 50, 1, 64, 64, 2, 0, 51);
            c.LayeredImage(60);
            void Quad(uint into, uint x, uint y, uint width, uint height, float a, float b, float d, float kind, Color color)
            {
                c.Add(Op.Begin, into, 0);
                c.Add(Op.Scissor, x, y, width, height);
                c.Add(Op.Use, 60);
                ImageValues(c, a, b, d, kind, color);
                c.Add(Op.Image, 0, into == 21 ? 40u : 20u);
                c.Add(Op.Image, 1, into == 32 ? 50u : 30u);
                c.Add(Op.Sampler, 0, 1, 0);
                c.Add(Op.Draw, 0, 3, 1);
                c.Add(Op.End);
            }
            Quad(21, 0, 0, 64, 64, 0, 0, 0, -1, Color.red);
            Quad(0, 0, 0, 32, 32, 1, 0, 0, 0, Color.black);
            Quad(21, 0, 0, 64, 64, 0, 0, 0, -1, Color.blue);
            Quad(0, 32, 0, 32, 32, 1, 0, 0, 0, Color.black);
            Quad(32, 0, 0, 64, 64, 0, 0, 0, -1, Color.green);
            Quad(0, 0, 32, 32, 32, 0.5f, 0.5f, 1, 1, Color.black);
            Quad(32, 0, 0, 64, 64, 0, 0, 0, -1, Color.cyan);
            Quad(0, 32, 32, 32, 32, 0.5f, 0.5f, 1, 1, Color.black);
            Replay(c, camera);
            pixels = Pixels(camera, target);
            Assert.AreEqual(new Color32(255, 0, 0, 255), (Color32)pixels.GetPixel(16, 48), "earlier cube face");
            Assert.AreEqual(new Color32(0, 0, 255, 255), (Color32)pixels.GetPixel(48, 48), "later cube face");
            Assert.AreEqual(new Color32(0, 255, 0, 255), (Color32)pixels.GetPixel(16, 16), "earlier array layer");
            Assert.AreEqual(new Color32(0, 255, 255, 255), (Color32)pixels.GetPixel(48, 16), "later array layer");
            var arrayDepth = ImageTexture(31);
            render.enabled = false;
            Assert.IsTrue(arrayDepth == null, "an array's owned depth is released with the renderer");
            yield return null;
        }
        finally
        {
            if (pixels != null) UnityEngine.Object.Destroy(pixels);
            camera.targetTexture = null;
            UnityEngine.Object.Destroy(camera.gameObject);
            target.Release();
            UnityEngine.Object.Destroy(target);
        }
    }
    [UnityTest]
    public IEnumerator PixelReadKernelReturnsStraightHdrThroughBufferReads()
    {
        var c = ImageObjects(0, 2);
        ImageQuad(c, 10, 0, 0, 64, 64, 0, 0, 0, -1, new Color(1, 0.25f, 0.125f, 0.5f));
        // Two corners of other colors, so rows read from the wrong end fail. Green over the top left by its corners
        // alone (clip space, y up: the triangle (0, 0), (32, 0), (0, 32) in pixels from the top left), and blue in
        // the bottom right 16 by 16 by a clip rect, whose rows count from the top as the image's do
        c.Buffer(5, 24, new[] { Bits(-1), Bits(1), Bits(0), Bits(1), Bits(-1), Bits(0) });
        c.Buffer(6, 16, new uint[] { 0xff00ff00, 0, 0, 0 });
        c.Draws(33, Shape, 16, 2);
        c.Add(Op.Begin, 10, 0);
        c.Add(Op.Use, 33);
        c.Add(Op.Uniforms, 16, 0, 0, 0, 0);
        c.Add(Op.Storage, 0, 5);
        c.Add(Op.Storage, 1, 6);
        c.Add(Op.Draw, 0, 3, 1);
        c.Add(Op.End);
        ImageQuad(c, 10, 48, 48, 16, 16, 0, 0, 0, -1, new Color(0, 0, 0.5f, 0.5f));
        const int count = 64 * 64;
        var words = new uint[4 + count * 4]; words[0] = words[1] = count;
        c.Buffer(1, (uint)words.Length * 4, words);
        c.Buffer(2, 16, new uint[4]);
        // One band of the whole image: a run a pixel, x along a row and y the row (cull.tide's ReadTargetPixels: the
        // part's width, the band's rows and its first, the part's first pixel, the image's size, then the list's table)
        c.Buffer(3, 32, new uint[] { 1, 64, 1, count, 64, 64, 1, 0 });
        c.PixelRead(32);
        c.Add(Op.ComputeBegin); c.Add(Op.Use, 32);
        c.Add(Op.Uniforms, 48, 64, 64, 0, 0, 0, 64, 64, 0, count, 0, 0, 0);
        c.Add(Op.Image, 0, 11); c.Add(Op.Sampler, 0, 0, 0);
        c.Add(Op.Storage, 0, 1); c.Add(Op.Storage, 1, 2); c.Add(Op.Storage, 2, 3);
        c.Add(Op.Dispatch, 1, 64, 1); c.Add(Op.ComputeEnd);
        c.Add(Op.BufferRead, 1, 1, 16, count * 16);
        Replay(c);
        yield return Answers(1);
        Assert.AreEqual(count * 4, answers[1].Length);
        var wanted = new uint[] { Bits(2), Bits(0.5f), Bits(0.25f), Bits(0.5f) };
        var green = new uint[] { Bits(0), Bits(1), Bits(0), Bits(1) };
        var blue = new uint[] { Bits(0), Bits(0), Bits(1), Bits(0.5f) };
        var greens = 0;
        for (var i = 0; i < count; i++)
        {
            // Pixel i is column i % 64 of row i / 64, rows from the top
            int x = i % 64, y = i / 64;
            if (Math.Abs(x + y - 31) <= 1) continue; // The triangle's slanted edge, either side by the rasterizer
            var color = x + y < 31 ? green : x >= 48 && y >= 48 ? blue : wanted;
            if (color == green) greens++;
            for (var k = 0; k < 4; k++) Assert.AreEqual(color[k], answers[1][i * 4 + k], $"pixel ({x}, {y}) from the top left, word {k}");
        }
        Assert.Greater(greens, 400, "the top left corner's pixels");
    }

    // The renderer's other read kernels (platform/src/cull.tide's ReadTarget..., `await target.Read<T>(image)`,
    // ReadDepth and a face's or a layer's Read), as HLSL from platform/src/read_pixels_wgsl.h's WGSL through
    // tide_wgsl_translate --hlsl: 00000000c0de0061 ReadTargetFloat4, 62 Int, 63 Depth, 64 Face, 65 Layer,
    // 66 LayerNumbers, 67 Float, 68 LayerDepth. Each run as the recorder records it: the pools (floats' and ints', a list at their
    // start: its count, its room, then a pixel after another), the run's record, a read of the list's pixels.
    const ulong ReadFloat4 = 0xc0de0061, ReadInt = 0xc0de0062, ReadDepth = 0xc0de0063, ReadFace = 0xc0de0064,
                ReadLayer = 0xc0de0065, ReadLayerNumbers = 0xc0de0066, ReadFloat = 0xc0de0067, ReadLayerDepth = 0xc0de0068, ReadPixels = 0xc0de0014;

    // `image`'s pixels, `words` each, rows from the top, through the kernel `key`; `part` a face or a layer
    IEnumerator ReadKernel(ulong key, uint read, bool sampled, uint image, int width, int height, int words, int part = -1)
    {
        answers.Clear();
        var count = width * height;
        var groups = (uint)((width + 63) / 64); // Along a row; a group a row, rows from the top (one band of them all)
        var pool = new uint[4 + count * words];
        pool[0] = pool[1] = (uint)count;
        var ints = key == ReadInt;
        var id = 900 + (uint)(key & 0xff);
        var c = new Commands();
        c.Buffer(990, (uint)pool.Length * 4, pool);
        c.Buffer(991, (uint)pool.Length * 4, pool);
        c.Buffer(992, 32, new uint[] { groups, (uint)height, 1, (uint)count, (uint)width, (uint)height, 1, 0 });
        c.ReadKernel(id, key, 48u, read, sampled, ints);
        c.Add(Op.ComputeBegin);
        c.Add(Op.Use, id);
        // The part's width, the band's rows and its first, the part's first pixel, the image's size, [a face or a
        // layer,] then the list's table: its first word and its room
        if (part < 0) c.Add(Op.Uniforms, 48, (uint)width, (uint)height, 0, 0, 0, (uint)width, (uint)height, 0, (uint)count, 0, 0, 0);
        else c.Add(Op.Uniforms, 48, (uint)width, (uint)height, 0, 0, 0, (uint)width, (uint)height, (uint)part, 0, (uint)count, 0, 0);
        c.Add(Op.Image, 0, image);
        if (sampled) c.Add(Op.Sampler, 0, 0, 0);
        c.Add(Op.Storage, 0, 990);
        c.Add(Op.Storage, 1, 991);
        c.Add(Op.Storage, 2, 992);
        c.Add(Op.Dispatch, groups, (uint)height, 1);
        c.Add(Op.ComputeEnd);
        c.Add(Op.BufferRead, 1, ints ? 991u : 990u, 16, (uint)(count * words * 4));
        Replay(c);
        yield return Answers(1);
        Assert.IsNotNull(answers[1], "the kernel's pixels read back");
        Assert.AreEqual(count * words, answers[1].Length);
    }

    static float Float(uint bits) => BitConverter.Int32BitsToSingle(unchecked((int)bits));

    // Every image of one target: its color through the Color kernel, its second and third as the numbers they
    // hold, exactly, and its depth in forward terms (0 near, 1 far) whichever way Unity's device stores it.
    [UnityTest]
    public IEnumerator ReadKernelsReturnEveryImageOfATarget()
    {
        var c = new Commands();
        c.Add(Op.Target, 300, 0x982, 4, 4, 301, 302, 303, 0, 304);
        c.NumericMixed(310);
        c.Add(Op.Begin, 300, 1);
        c.Add(Op.Use, 310);
        c.Add(Op.Uniforms, 16, 0, 0, 0, 0);
        c.Add(Op.Draw, 0, 3, 1);
        c.Add(Op.Draw, 0, 3, 1);
        c.Add(Op.End);
        // Then a draw whose frag has a color alone: the target's other images stay as they are (Unity's OpenGL Core
        // wrote the color into each, until the shader said ColorMask 0 for them)
        c.Images(31, ImageDraw, false);
        ImageQuad(c, 300, 0, 0, 4, 4, 0, 0, 0, -1, new Color(0.25f, 0, 0, 1));
        // And a depth nothing but a draw gives: a quarter of the way in over the top left alone, the far limit elsewhere
        c.Add(Op.Target, 10, 1, 8, 8, 11, 0, 0, 0, 12);
        c.DepthImage(32, 1);
        c.Add(Op.Begin, 10, 1);
        c.Add(Op.Scissor, 0, 0, 4, 2);
        c.Add(Op.Use, 32);
        ImageValues(c, 0, 0, 0, -1, new Color(0, 0, 0, 0.25f));
        c.Add(Op.Image, 0, 0);
        c.Add(Op.Draw, 0, 3, 1);
        c.Add(Op.End);
        Replay(c);

        yield return ReadKernel(ReadPixels, 0, true, 301, 4, 4, 4);
        var color = new[] { 0.25f, 0f, 0f, 1f };
        for (var i = 0; i < 64; i++) Assert.AreEqual(color[i % 4], Float(answers[1][i]), "the first image's color, word " + i);
        yield return ReadKernel(ReadFloat4, 9, false, 302, 4, 4, 4);
        var numbers = new[] { 1.5f, -0.5f, 2f, 0.125f };
        for (var i = 0; i < 64; i++) Assert.AreEqual(numbers[i % 4], Float(answers[1][i]), "the second image's numbers, word " + i);
        yield return ReadKernel(ReadInt, 10, false, 303, 4, 4, 1);
        foreach (var value in answers[1]) Assert.AreEqual(-16777217, unchecked((int)value), "the third image's whole numbers");
        yield return ReadKernel(ReadDepth, 1, true, 304, 4, 4, 1);
        foreach (var value in answers[1]) Assert.AreEqual(1f, Float(value), "a cleared depth is the far limit, 1");
        yield return ReadKernel(ReadDepth, 1, true, 12, 8, 8, 1);
        for (var i = 0; i < 64; i++)
        {
            var drawn = i % 8 < 4 && i / 8 < 2;
            Assert.AreEqual(drawn ? 0.25f : 1f, Float(answers[1][i]), 1e-6f, $"depth at ({i % 8}, {i / 8}) from the top left");
        }
    }

    // A number image blends between its pixels and has smaller images, and is an array's layer: R16F with 1.5 in
    // its left half (its top left, in the layer) and nothing elsewhere.
    [UnityTest]
    public IEnumerator NumberImagesBlendHaveSmallerImagesAndAreLayers()
    {
        var c = ImageObjects();
        c.Add(Op.Target, 140, 2, 64, 64, 141, 0, 0, 0, 142); // RGBA16F, where the blended read is drawn
        c.Add(Op.Target, 100, 5, 4, 4, 101, 0, 0, 0, 102);
        c.Add(Op.Array, 130, 5, 4, 4, 2, 0, 131);
        c.Add(Op.ArrayLayer, 132, 130, 1);
        c.Numeric(110, 0xc0de0021);
        foreach (var into in new uint[] { 100, 132 })
        {
            c.Add(Op.Begin, into, 1);
            c.Add(Op.Scissor, 0, 0, 2, into == 100 ? 4u : 1u);
            c.Add(Op.Use, 110);
            c.Add(Op.Uniforms, 16, 0, 0, 0, 0);
            c.Add(Op.Draw, 0, 3, 1);
            c.Add(Op.End);
        }
        c.Add(Op.Mipmaps, 101);
        // Between the second column's middle and the third's: half of each where it blends, one of them where it doesn't
        ImageQuad(c, 140, 0, 0, 32, 64, 0.5f, 0.5f, 0, 0, Color.black, 1, 0, 101);
        ImageQuad(c, 140, 32, 0, 32, 64, 0.49f, 0.5f, 0, 0, Color.black, 0, 0, 101);
        Replay(c);
        var read = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(141), 0, TextureFormat.RGBAFloat);
        while (!read.done) yield return null;
        Assert.IsFalse(read.hasError);
        var values = read.GetData<float>();
        Assert.AreEqual(0.75f, values[(32 * 64 + 16) * 4], "a number image blended between two pixels");
        Assert.AreEqual(1.5f, values[(32 * 64 + 48) * 4], "and the nearest of them");
        Assert.AreEqual(3, ImageTexture(101).mipmapCount, "a number image's smaller images");
        read = UnityEngine.Rendering.AsyncGPUReadback.Request(ImageTexture(101), 2, TextureFormat.RGBAFloat);
        while (!read.done) yield return null;
        Assert.IsFalse(read.hasError);
        Assert.AreEqual(0.75f, read.GetData<float>()[0], "its smallest image is the mean of its pixels");

        yield return ReadKernel(ReadFloat, 9, false, 101, 4, 4, 1);
        for (var i = 0; i < 16; i++) Assert.AreEqual(i % 4 < 2 ? 1.5f : 0f, Float(answers[1][i]), $"the number at ({i % 4}, {i / 4})");
        yield return ReadKernel(ReadLayerNumbers, 6, true, 130, 4, 4, 4, 1);
        for (var i = 0; i < 16; i++)
            Assert.AreEqual(i < 2 ? 1.5f : 0f, Float(answers[1][i * 4]), $"the layer's number at ({i % 4}, {i / 4}) from the top left");
    }

    // A cube's face and an array's layer, read with their rows from the top as they were drawn: green clipped into
    // the top left 4 by 2 of red.
    [UnityTest]
    public IEnumerator ReadKernelsReturnFacesAndLayersFromTheTopLeft()
    {
        var c = new Commands();
        c.Add(Op.Cube, 20, 1, 8, 0);
        c.Add(Op.CubeFace, 21, 20, 0, 22);
        c.Add(Op.CubeFace, 23, 20, 2, 24);
        c.Add(Op.CubeFace, 25, 20, 5, 26);
        c.Add(Op.Array, 30, 1, 8, 8, 2, 0, 31);
        c.Add(Op.ArrayLayer, 32, 30, 1);
        c.Add(Op.Cube, 40, 1, 8, 0); // Unused compatible bindings while painting, so no attachment is sampled
        c.Add(Op.Array, 50, 1, 8, 8, 2, 0, 51);
        c.LayeredImage(60);
        foreach (var into in new uint[] { 21, 23, 25, 32 })
        {
            foreach (var corner in new[] { false, true })
            {
                c.Add(Op.Begin, into, corner ? 0u : 1u);
                if (corner) c.Add(Op.Scissor, 0, 0, 4, 2);
                c.Add(Op.Use, 60);
                ImageValues(c, 0, 0, 0, -1, corner ? Color.green : Color.red);
                c.Add(Op.Image, 0, 40);
                c.Add(Op.Image, 1, 50);
                c.Add(Op.Sampler, 0, 0, 0);
                c.Add(Op.Draw, 0, 3, 1);
                c.Add(Op.End);
            }
        }
        // Their depths: a quarter of the way in over the top left 4 by 2 of a face, of a layer, and of a layer of an
        // array that's depth alone (read by its depth array's id, as the recorder gives it one of its own)
        c.Add(Op.Array, 70, 0, 8, 8, 2, 0, 71);
        c.Add(Op.ArrayLayer, 72, 70, 1);
        c.DepthImage(61, 1);
        foreach (var into in new uint[] { 23, 32, 72 })
        {
            c.Add(Op.Begin, into, 1);
            c.Add(Op.Scissor, 0, 0, 4, 2);
            c.Add(Op.Use, 61);
            ImageValues(c, 0, 0, 0, -1, new Color(0, 0, 0, 0.25f));
            c.Add(Op.Image, 0, 0);
            c.Add(Op.Draw, 0, 3, 1);
            c.Add(Op.End);
        }
        Replay(c);
        foreach (var depth in new uint[] { 24, 31, 71 })
        {
            if (depth == 24) yield return ReadKernel(ReadDepth, 1, true, depth, 8, 8, 1);
            else yield return ReadKernel(ReadLayerDepth, 7, true, depth, 8, 8, 1, 1);
            var what = depth == 24 ? "a face's depth" : depth == 31 ? "a layer's depth" : "a depth array's layer";
            for (var i = 0; i < 64; i++)
                Assert.AreEqual(i % 8 < 4 && i / 8 < 2 ? 0.25f : 1f, Float(answers[1][i]), 1e-6f, $"{what} at ({i % 8}, {i / 8}) from the top left");
        }
        // The colors are drawn again after: the depth passes above cleared the face and the layer they share
        c = new Commands();
        foreach (var into in new uint[] { 23, 32 })
        {
            foreach (var corner in new[] { false, true })
            {
                c.Add(Op.Begin, into, corner ? 0u : 1u);
                if (corner) c.Add(Op.Scissor, 0, 0, 4, 2);
                c.Add(Op.Use, 60);
                ImageValues(c, 0, 0, 0, -1, corner ? Color.green : Color.red);
                c.Add(Op.Image, 0, 40);
                c.Add(Op.Image, 1, 50);
                c.Add(Op.Sampler, 0, 0, 0);
                c.Add(Op.Draw, 0, 3, 1);
                c.Add(Op.End);
            }
        }
        Replay(c);
        foreach (var part in new[] { 0, 2, 5, -1 })
        {
            if (part >= 0) yield return ReadKernel(ReadFace, 3, true, 20, 8, 8, 4, part);
            else yield return ReadKernel(ReadLayer, 6, true, 30, 8, 8, 4, 1);
            var what = part >= 0 ? "face " + part : "the layer";
            for (var i = 0; i < 64; i++)
            {
                var corner = i % 8 < 4 && i / 8 < 2;
                Assert.AreEqual(corner ? 0f : 1f, Float(answers[1][i * 4]), $"{what}'s red at ({i % 8}, {i / 8}) from the top left");
                Assert.AreEqual(corner ? 1f : 0f, Float(answers[1][i * 4 + 1]), $"{what}'s green at ({i % 8}, {i / 8}) from the top left");
                Assert.AreEqual(1f, Float(answers[1][i * 4 + 3]), $"{what}'s alpha");
            }
        }
    }

    // A clip rect's rows count from the top, in the camera's own pass as in a target's (the pixel read above): red
    // in the camera's top quarter alone, and the target's top quarter (green) shown over the camera's lower half.
    [UnityTest]
    public IEnumerator ClipRectsCountRowsFromTheTop()
    {
        var camera = new GameObject("Tide clip camera").AddComponent<Camera>();
        var target = new RenderTexture(64, 64, 24);
        Texture2D pixels = null;
        try
        {
            camera.targetTexture = target;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            var c = ImageObjects();
            ImageQuad(c, 10, 0, 0, 64, 64, 0, 0, 0, -1, Color.blue);
            ImageQuad(c, 10, 0, 0, 64, 16, 0, 0, 0, -1, Color.green);
            ImageQuad(c, 0, 0, 0, 64, 16, 0, 0, 0, -1, Color.red);
            ImageQuad(c, 0, 0, 32, 32, 32, 0.5f, 0.125f, 0, 0, Color.black, 0); // The target's top quarter...
            ImageQuad(c, 0, 32, 32, 32, 32, 0.5f, 0.875f, 0, 0, Color.black, 0); // ...and its lowest
            Replay(c, camera);
            pixels = Pixels(camera, target);
            // Unity's pixels count rows from the bottom
            Assert.AreEqual(new Color32(255, 0, 0, 255), (Color32)pixels.GetPixel(32, 56), "the camera's top rows are clipped to");
            Assert.AreEqual(new Color32(0, 0, 0, 255), (Color32)pixels.GetPixel(32, 40), "and no others");
            Assert.AreEqual(new Color32(0, 255, 0, 255), (Color32)pixels.GetPixel(16, 16), "a target's top rows are clipped to");
            Assert.AreEqual(new Color32(0, 0, 255, 255), (Color32)pixels.GetPixel(48, 16), "and no others");
            yield return null;
        }
        finally
        {
            if (pixels != null) UnityEngine.Object.Destroy(pixels);
            camera.targetTexture = null;
            UnityEngine.Object.Destroy(camera.gameObject);
            target.Release();
            UnityEngine.Object.Destroy(target);
        }
    }

    // A depth bias in Unity's own depth convention (Draw.DepthBias: positive pushes away from the camera, negative
    // pulls nearer, whichever way the device stores depth). Four columns of two draws each on one plane, depth tested
    // and written, the first red and the second green; with no bias the second passes, its depth being equal.
    [UnityTest]
    public IEnumerator DepthBiasPushesAwayWherePositive()
    {
        var camera = new GameObject("Tide depth bias camera").AddComponent<Camera>();
        var target = new RenderTexture(64, 64, 24);
        Texture2D pixels = null;
        try
        {
            camera.targetTexture = target;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            const uint red = 0xff0000ff, green = 0xff00ff00;
            // The first draw's bias and the second's, a column each
            var biases = new[] { (0f, 8f), (0f, -8f), (-8f, 0f), (0f, 0f) };
            foreach (var own in new[] { false, true }) // In a target's pass, which runs as it's read, and in the camera's own
            {
                var c = own ? new Commands() : ImageObjects(); // The second frame goes on with the first's objects
                if (!own)
                {
                    c.Buffer(1, 24, new[] { Bits(-1), Bits(-1), Bits(3), Bits(-1), Bits(-1), Bits(3) }); // A triangle over everything
                    c.Buffer(2, 16, new[] { red, green, 0u, 0u });
                    c.DepthDraws(5, Shape, 16, 2);
                }
                c.Add(Op.Begin, own ? 0u : 10u, own ? 0u : 1u);
                for (var column = 0u; column < 4; column++)
                {
                    c.Add(Op.Scissor, column * 16, 0, 16, 64);
                    c.Add(Op.Use, 5);
                    c.Add(Op.Storage, 0, 1);
                    c.Add(Op.Storage, 1, 2);
                    for (var draw = 0; draw < 2; draw++)
                    {
                        c.Add(Op.Uniforms, 16, Bits(draw), 0, 0, 0);
                        c.Add(Op.DepthBias, Bits(draw == 0 ? biases[column].Item1 : biases[column].Item2), Bits(0));
                        c.Add(Op.Draw, 0, 3, 1);
                    }
                }
                c.Add(Op.End);
                if (!own)
                {
                    for (var column = 0u; column < 4; column++)
                        ImageQuad(c, 0, column * 16, 0, 16, 64, (column * 16 + 8) / 64f, 0.5f, 0, 0, Color.black, 0);
                }
                Replay(c, camera);
                if (pixels != null) UnityEngine.Object.Destroy(pixels);
                pixels = Pixels(camera, target);
                var where = own ? " in the camera's own pass" : " in a target's pass";
                Assert.AreEqual(new Color32(255, 0, 0, 255), (Color32)pixels.GetPixel(8, 32), "a draw pushed away loses to the one there" + where);
                Assert.AreEqual(new Color32(0, 255, 0, 255), (Color32)pixels.GetPixel(24, 32), "a draw pulled nearer wins" + where);
                Assert.AreEqual(new Color32(255, 0, 0, 255), (Color32)pixels.GetPixel(40, 32), "a draw pulled nearer stays under a later one with none" + where);
                Assert.AreEqual(new Color32(0, 255, 0, 255), (Color32)pixels.GetPixel(56, 32), "with no bias the later draw of one depth passes" + where);
                yield return null;
            }
        }
        finally
        {
            if (pixels != null) UnityEngine.Object.Destroy(pixels);
            camera.targetTexture = null;
            UnityEngine.Object.Destroy(camera.gameObject);
            target.Release();
            UnityEngine.Object.Destroy(target);
        }
    }

}
