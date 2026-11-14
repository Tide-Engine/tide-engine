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
        Cube = 8, CubeFace = 9, Array = 38, ArrayLayer = 39,
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

        public void Images(uint id, ulong key, bool compute, uint format = 0)
        {
            Pipeline(id, key, 32, 3, new uint[] { 0, 8 }, !compute);
            if (compute)
            {
                Add(Op.PipelineStorage, id, 1, 0, 0);
                Add(Op.PipelineImages, id, 3, 2, format, format);
            }
        }

        public void PixelRead(uint id) {
            Pipeline(id, 0xc0de0014, 16, 3, new uint[] { 0 }, true);
            Add(Op.PipelineStorage, id, 1, 3, 1);
        }

        public void DepthImage(uint id, uint depth) => Pipeline(id, 0xc0de0012, 32, 3, new uint[] { 1 }, true, depth);
        public void LayeredImage(uint id) => Pipeline(id, 0xc0de0013, 32, 3, new uint[] { 3, 6 }, true);

        void Pipeline(uint id, ulong key, uint uniforms, uint blend, uint[] reads = null, bool sampled = false, uint depth = 0)
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
            w.AddRange(new uint[] { blend, depth, 0, 0, 0, 0, 1, 0 }); // Blend, depth, cull, stencil, into, outputs, alpha_to_coverage
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
                          float u, float v, float w, float kind, Color color, uint filter = 1, uint wrap = 0)
    {
        c.Add(Op.Begin, target, 0);
        c.Add(Op.Scissor, x, y, width, height);
        c.Add(Op.Use, 31);
        ImageValues(c, u, v, w, kind, color);
        c.Add(Op.Image, 0, target == 10 ? 0u : 11u);
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
        const int count = 64 * 64;
        var words = new uint[4 + count * 4]; words[0] = words[1] = count;
        c.Buffer(1, (uint)words.Length * 4, words);
        c.Buffer(2, 16, new uint[4]);
        c.Buffer(3, 32, new uint[] { 64, 1, 1, count, count, 1, 1, 0 });
        c.PixelRead(32);
        c.Add(Op.ComputeBegin); c.Add(Op.Use, 32);
        c.Add(Op.Uniforms, 16, 64, 64, 0, count);
        c.Add(Op.Image, 0, 11); c.Add(Op.Sampler, 0, 0, 0);
        c.Add(Op.Storage, 0, 1); c.Add(Op.Storage, 1, 2); c.Add(Op.Storage, 2, 3);
        c.Add(Op.Dispatch, 64, 1, 1); c.Add(Op.ComputeEnd);
        c.Add(Op.BufferRead, 1, 1, 16, count * 16);
        Replay(c);
        yield return Answers(1);
        Assert.AreEqual(count * 4, answers[1].Length);
        var wanted = new uint[] { Bits(2), Bits(0.5f), Bits(0.25f), Bits(0.5f) };
        for (var i = 0; i < count * 4; i++) Assert.AreEqual(wanted[i % 4], answers[1][i], "pixel word " + i);
    }

}
