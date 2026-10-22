using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Experimental.Rendering;
using UnityEngine.Rendering;

namespace Tide
{
    // Draws what Tide's views drew with Tide's own renderer, inside Unity's
    // frame: the GPU calls it made (tide/embed.h's tide_embed_render) replayed
    // into a CommandBuffer on the camera, after its transparent objects and
    // with its depth, so Tide's 3D and Unity's scene hide each other. Shapes,
    // text, meshes, 3D, render targets, stencil, blits and the game's own frags
    // and verts are Tide's pixels, through the shaders tide unity wrote into
    // com.tide.game (Resources/TideShaders, by each one's key). Vertices are
    // read from buffers by the shaders (compiler/src/hlsl.h), so it needs a GPU
    // with compute-class buffers; TideRunner uses TideDraw where there's none.
    [DisallowMultipleComponent]
    public sealed unsafe class TideRender : MonoBehaviour
    {
        enum Op : uint
        {
            Buffer = 1, BufferWrite, BufferFree, Texture, TextureWrite, Mipmaps, TextureFree, Cube, CubeFace, Pipeline,
            PipelineFree, Target, TargetFree, ResolveDepth, Begin, End, Scissor, Use, Stencil, Uniforms, Sample, Vertices,
            Indices, Draw, DrawIndexed,
        }

        // A texture a draw samples: a texture's, a cube's, or a target's color or depth.
        sealed class Image
        {
            public Texture texture;
            public RenderTextureSubElement element = RenderTextureSubElement.Default;
            public byte[] pixels; // A Texture2D's, which a part is written into
            public int pixelBytes, width, height;
            public bool mips;  // Its smaller images are made
            public bool owned; // Destroyed when it's freed: a target's go with the target
        }

        sealed class Target
        {
            public RenderTexture[] colors;
            public RenderTexture depth;
            public RenderTexture cube; // A cube face's: the cube, and which face
            public CubemapFace face;
            public int width, height;
            public uint[] imageIds;
        }

        sealed class Pipeline
        {
            public Shader shader;
            public string name;
            public ulong key;
            public Vector4[] attributes = new Vector4[16];
            public Vector4[] slots = new Vector4[3];
            public int uniformSize, textureCount, blend, depth, cull, stencilTest, stencilOp;
            public int looked, made; // The frame its shader was last looked for, while it isn't there, and the pipeline's
            public readonly Dictionary<int, Material> materials = new Dictionary<int, Material>();
        }

        public TideRunner Runner { get; set; }

        readonly Dictionary<uint, GraphicsBuffer> buffers = new Dictionary<uint, GraphicsBuffer>();
        readonly Dictionary<uint, Image> images = new Dictionary<uint, Image>();
        readonly Dictionary<uint, Target> targets = new Dictionary<uint, Target>();
        readonly Dictionary<uint, Pipeline> pipelines = new Dictionary<uint, Pipeline>();
        readonly List<UnityEngine.Object> doomed = new List<UnityEngine.Object>(); // Freed once the frame that drew with them drew
        readonly List<GraphicsBuffer> doomedBuffers = new List<GraphicsBuffer>();
        readonly HashSet<ulong> missing = new HashSet<ulong>(); // Shaders said once not to be there

        CommandBuffer commands;
        Camera attached;
        GraphicsBuffer uniformBuffer;
        byte[] uniformBytes = new byte[0];
        uint[] words = new uint[0];
        GraphicsBuffer none; // What an unused vertex slot reads
        Texture2D white;
        bool fresh = true; // Nothing made yet: Tide's renderer makes everything again first
        bool linear;

        // The draw being put together
        Pipeline pipeline;
        readonly uint[] vertexBuffer = new uint[3];
        readonly uint[] vertexOffset = new uint[3];
        uint indexBuffer;
        bool indexWide;
        readonly uint[] sampled = new uint[8];
        readonly uint[] sampling = new uint[8];
        int uniformAt, uniformSize;
        int stencil;
        bool inPass, flipped;
        int passHeight;
        uint passTarget;

        static readonly int SrcColor = Shader.PropertyToID("_TideSrcColor"), DstColor = Shader.PropertyToID("_TideDstColor");
        static readonly int SrcAlpha = Shader.PropertyToID("_TideSrcAlpha"), DstAlpha = Shader.PropertyToID("_TideDstAlpha");
        static readonly int ZTest = Shader.PropertyToID("_TideZTest"), ZWrite = Shader.PropertyToID("_TideZWrite");
        static readonly int Cull = Shader.PropertyToID("_TideCull"), StencilRef = Shader.PropertyToID("_TideStencilRef");
        static readonly int StencilComp = Shader.PropertyToID("_TideStencilComp"), StencilPass = Shader.PropertyToID("_TideStencilPass");
        static readonly int Attributes = Shader.PropertyToID("_TideAttributes"), Slots = Shader.PropertyToID("_TideSlots");
        static readonly int Flip = Shader.PropertyToID("_TideFlip"), Height = Shader.PropertyToID("_TideHeight");
        static readonly int Linear = Shader.PropertyToID("_TideLinear");
        static readonly int First = Shader.PropertyToID("_TideFirst"), Indexed = Shader.PropertyToID("_TideIndexed");
        static readonly int Sampling = Shader.PropertyToID("_TideSampling"), Indices = Shader.PropertyToID("_TideIndices");
        static readonly int Uniforms = Shader.PropertyToID("TideUniforms");
        static readonly int[] Offsets = { Shader.PropertyToID("_TideOffset0"), Shader.PropertyToID("_TideOffset1"), Shader.PropertyToID("_TideOffset2") };
        static readonly int[] Vertices = { Shader.PropertyToID("_TideVertices0"), Shader.PropertyToID("_TideVertices1"), Shader.PropertyToID("_TideVertices2") };
        static readonly int[] Textures =
        {
            Shader.PropertyToID("_TideTexture0"), Shader.PropertyToID("_TideTexture1"), Shader.PropertyToID("_TideTexture2"),
            Shader.PropertyToID("_TideTexture3"), Shader.PropertyToID("_TideTexture4"), Shader.PropertyToID("_TideTexture5"),
            Shader.PropertyToID("_TideTexture6"), Shader.PropertyToID("_TideTexture7"),
        };

        // Whether this GPU replays Tide's drawing: its shaders read vertices from buffers.
        public static bool Supported =>
            SystemInfo.supportsComputeShaders && SystemInfo.graphicsShaderLevel >= 45 && SystemInfo.supportsSetConstantBuffer;

        // The camera it draws into: the one the views start with.
        public Camera Camera => attached;

        // Whether Tide's renderer has to make everything it draws with again (the objects here are gone), which
        // TideRunner tells it before the next render.
        public bool Fresh => fresh;

        void OnEnable()
        {
            commands = new CommandBuffer { name = "Tide" };
            none = new GraphicsBuffer(GraphicsBuffer.Target.Raw, 4, 4);
            none.SetData(new uint[4]);
            white = new Texture2D(1, 1, GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.None) { hideFlags = HideFlags.HideAndDontSave };
            white.SetPixelData(new byte[] { 255, 255, 255, 255 }, 0);
            white.Apply(false, false);
            fresh = true;
        }

        void OnDisable()
        {
            Attach(null);
            commands?.Release();
            commands = null;
            ForgetAll();
            none?.Release();
            none = null;
            if (white != null) DestroyImmediate(white);
            uniformBuffer?.Release();
            uniformBuffer = null;
        }

        // Everything made, gone: the next frame's commands make it again.
        void ForgetAll()
        {
            foreach (var b in buffers.Values) b.Release();
            buffers.Clear();
            foreach (var i in images.Values)
            {
                if (i.owned && i.texture != null) DestroyImmediate(i.texture);
            }
            images.Clear();
            foreach (var t in targets.Values) Release(t);
            targets.Clear();
            foreach (var p in pipelines.Values) ForgetMaterials(p);
            pipelines.Clear();
            Collect();
            fresh = true;
        }

        void Collect()
        {
            foreach (var o in doomed)
            {
                if (o is RenderTexture rt) rt.Release();
                if (o != null) DestroyImmediate(o);
            }
            doomed.Clear();
            foreach (var b in doomedBuffers) b.Release();
            doomedBuffers.Clear();
        }

        static void Release(Target t)
        {
            if (t.colors != null)
            {
                foreach (var c in t.colors)
                {
                    if (c == null) continue;
                    c.Release();
                    DestroyImmediate(c);
                }
            }
            if (t.depth != null)
            {
                t.depth.Release();
                DestroyImmediate(t.depth);
            }
        }

        static void ForgetMaterials(Pipeline p)
        {
            foreach (var m in p.materials.Values)
            {
                if (m != null) DestroyImmediate(m);
            }
            p.materials.Clear();
        }

        void Attach(Camera camera)
        {
            if (attached == camera) return;
            if (attached != null && commands != null) attached.RemoveCommandBuffer(CameraEvent.AfterForwardAlpha, commands);
            attached = camera;
            if (attached != null && commands != null) attached.AddCommandBuffer(CameraEvent.AfterForwardAlpha, commands);
        }

        // This frame's drawing, `size` bytes of tide_embed_render's commands, into `camera`.
        public void Replay(byte* stream, uint size, Camera camera)
        {
            Attach(camera);
            Collect();
            fresh = false;
            commands.Clear();
            if (stream == null || camera == null) return;
            linear = QualitySettings.activeColorSpace == ColorSpace.Linear;
            PrepareUniforms(stream, size);
            pipeline = null;
            inPass = false;
            for (uint at = 0; at + 8 <= size;)
            {
                var op = (Op)Word(stream, at);
                var length = Word(stream, at + 4);
                at += 8;
                if (length > size - at) break;
                Run(op, stream + at, length);
                at += length;
            }
            if (inPass) commands.DisableScissorRect();
            if (uniformBuffer != null && uniformBytes.Length > 0) uniformBuffer.SetData(uniformBytes);
        }

        static uint Word(byte* p, uint at) => *(uint*)(p + at);

        // Every UNIFORMS command's bytes in one buffer, each where Unity can bind it from.
        void PrepareUniforms(byte* stream, uint size)
        {
            var align = Math.Max(16, SystemInfo.constantBufferOffsetAlignment);
            long total = 0;
            for (uint at = 0; at + 8 <= size;)
            {
                var op = (Op)Word(stream, at);
                var length = Word(stream, at + 4);
                if (length > size - at - 8) break;
                if (op == Op.Uniforms) total += (Word(stream, at + 8) + align - 1) / align * align + align;
                at += 8 + length;
            }
            var bytes = (int)Math.Max(align, total);
            bytes = (bytes + 15) / 16 * 16;
            if (uniformBytes.Length != bytes) uniformBytes = new byte[bytes];
            if (uniformBuffer == null || uniformBuffer.count * 16 < bytes)
            {
                if (uniformBuffer != null) doomedBuffers.Add(uniformBuffer);
                uniformBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Constant, Math.Max(bytes / 16, uniformBuffer == null ? 256 : uniformBuffer.count * 2), 16);
            }
            uniformUsed = 0;
        }

        int uniformUsed;

        void Run(Op op, byte* p, uint length)
        {
            uint W(int i) => *(uint*)(p + 4 * i);
            switch (op)
            {
                case Op.Buffer:
                {
                    var size = (int)W(2);
                    var buffer = new GraphicsBuffer(GraphicsBuffer.Target.Raw, Math.Max(1, (size + 3) / 4), 4);
                    Forget(W(0));
                    buffers[W(0)] = buffer;
                    if (W(3) != 0) Write(buffer, 0, p + 16, size);
                    break;
                }
                case Op.BufferWrite:
                    if (buffers.TryGetValue(W(0), out var written)) Write(written, (int)W(1), p + 12, (int)W(2));
                    break;
                case Op.BufferFree:
                    if (buffers.TryGetValue(W(0), out var freed))
                    {
                        doomedBuffers.Add(freed);
                        buffers.Remove(W(0));
                    }
                    break;
                case Op.Texture: MakeTexture(W(0), (int)W(1), (int)W(2), (int)W(3), W(4) != 0 ? p + 20 : null); break;
                case Op.TextureWrite: WriteTexture(W(0), (int)W(1), (int)W(2), (int)W(3), (int)W(4), p + 20); break;
                case Op.Mipmaps: Mipmaps(W(0)); break;
                case Op.TextureFree:
                    if (images.TryGetValue(W(0), out var image))
                    {
                        if (image.owned && image.texture != null) doomed.Add(image.texture);
                        images.Remove(W(0));
                    }
                    break;
                case Op.Cube: MakeCube(W(0), (int)W(1), (int)W(2), W(3) != 0 ? p + 16 : null); break;
                case Op.CubeFace: MakeCubeFace(W(0), W(1), (int)W(2), W(3)); break;
                case Op.Pipeline: MakePipeline(p); break;
                case Op.PipelineFree:
                    if (pipelines.TryGetValue(W(0), out var gone))
                    {
                        foreach (var m in gone.materials.Values) doomed.Add(m);
                        pipelines.Remove(W(0));
                    }
                    break;
                case Op.Target: MakeTarget(p); break;
                case Op.TargetFree:
                    if (targets.TryGetValue(W(0), out var target))
                    {
                        foreach (var id in target.imageIds) images.Remove(id);
                        if (target.colors != null) doomed.AddRange(target.colors);
                        if (target.depth != null) doomed.Add(target.depth);
                        targets.Remove(W(0));
                    }
                    break;
                case Op.ResolveDepth:
                    // A multisampled target's depth isn't a texture Unity resolves: it reads as far away
                    if (!images.ContainsKey(W(1))) images[W(1)] = new Image { texture = Texture2D.blackTexture, width = 1, height = 1 };
                    break;
                case Op.Begin: Begin(W(0), W(1) != 0); break;
                case Op.End:
                    commands.DisableScissorRect();
                    inPass = false;
                    break;
                case Op.Scissor:
                {
                    int x = (int)W(0), y = (int)W(1), w = (int)W(2), h = (int)W(3);
                    // Unity counts a rect's rows from the bottom
                    commands.EnableScissorRect(new Rect(x, passHeight - y - h, w, h));
                    break;
                }
                case Op.Use:
                    pipelines.TryGetValue(W(0), out pipeline);
                    break;
                case Op.Stencil: stencil = (int)W(0); break;
                case Op.Uniforms:
                {
                    var size = (int)W(0);
                    var align = Math.Max(16, SystemInfo.constantBufferOffsetAlignment);
                    uniformAt = uniformUsed;
                    uniformSize = size;
                    if (uniformAt + size <= uniformBytes.Length) Marshal.Copy((IntPtr)(p + 4), uniformBytes, uniformAt, size);
                    uniformUsed += (size + align - 1) / align * align;
                    break;
                }
                case Op.Sample:
                    if (W(0) < 8)
                    {
                        sampled[W(0)] = W(1);
                        sampling[W(0)] = W(2) | W(3) << 2;
                    }
                    break;
                case Op.Vertices:
                    if (W(0) < 3)
                    {
                        vertexBuffer[W(0)] = W(1);
                        vertexOffset[W(0)] = W(2);
                    }
                    break;
                case Op.Indices:
                    indexBuffer = W(0);
                    indexWide = W(1) != 0;
                    break;
                case Op.Draw: Draw(W(0), (int)W(1), (int)W(2), false); break;
                case Op.DrawIndexed: Draw(0, (int)W(0), (int)W(1), true); break;
            }
        }

        void Forget(uint id)
        {
            if (buffers.TryGetValue(id, out var b))
            {
                doomedBuffers.Add(b);
                buffers.Remove(id);
            }
        }

        // Bytes into a buffer, now: the frame's writes are all before its draws, as the renderer makes them.
        void Write(GraphicsBuffer buffer, int offset, byte* data, int size)
        {
            var count = (size + 3) / 4;
            if (words.Length < count) words = new uint[Math.Max(count, words.Length * 2)];
            fixed (uint* to = words) Buffer.MemoryCopy(data, to, (long)words.Length * 4, size);
            if (offset / 4 + count > buffer.count) count = buffer.count - offset / 4;
            if (count > 0) buffer.SetData(words, 0, offset / 4, count);
        }

        static GraphicsFormat TextureFormat(int format) => format switch
        {
            1 => GraphicsFormat.R8_UNorm,
            2 => GraphicsFormat.R16_SFloat,
            3 => GraphicsFormat.R32G32B32A32_SInt,
            _ => GraphicsFormat.R8G8B8A8_UNorm,
        };

        static int PixelBytes(int format) => format == 1 ? 1 : format == 2 ? 2 : format == 3 ? 16 : 4;

        void MakeTexture(uint id, int format, int width, int height, byte* pixels)
        {
            var flags = format == 3 ? TextureCreationFlags.None : TextureCreationFlags.MipChain;
            var texture = new Texture2D(Math.Max(1, width), Math.Max(1, height), TextureFormat(format), flags)
            {
                hideFlags = HideFlags.HideAndDontSave,
            };
            var bytes = PixelBytes(format);
            var image = new Image { texture = texture, pixelBytes = bytes, width = width, height = height, owned = true };
            image.pixels = new byte[Math.Max(1, width * height * bytes)];
            if (pixels != null) Marshal.Copy((IntPtr)pixels, image.pixels, 0, width * height * bytes);
            texture.SetPixelData(image.pixels, 0);
            texture.Apply(false, false);
            images[id] = image;
        }

        void WriteTexture(uint id, int x, int y, int width, int height, byte* pixels)
        {
            if (!images.TryGetValue(id, out var image) || image.pixels == null || !(image.texture is Texture2D texture)) return;
            var row = width * image.pixelBytes;
            for (var r = 0; r < height; r++)
            {
                Marshal.Copy((IntPtr)(pixels + r * row), image.pixels, ((y + r) * image.width + x) * image.pixelBytes, row);
            }
            texture.SetPixelData(image.pixels, 0);
            texture.Apply(false, false); // Its smaller images stay as they were made
        }

        void Mipmaps(uint id)
        {
            if (!images.TryGetValue(id, out var image)) return;
            if (image.texture is Texture2D texture && texture.mipmapCount > 1) texture.Apply(true, false);
            else if (image.texture is Cubemap cube) cube.Apply(true, false);
            else if (image.texture is RenderTexture rt && rt.useMipMap) commands.GenerateMips(rt);
            else return;
            image.mips = true;
        }

        void MakeCube(uint id, int color, int size, byte* faces)
        {
            size = Math.Max(1, size);
            if (faces != null)
            {
                var cube = new Cubemap(size, GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.MipChain) { hideFlags = HideFlags.HideAndDontSave };
                var face = size * size * 4;
                var bytes = new byte[face];
                for (var i = 0; i < 6; i++)
                {
                    Marshal.Copy((IntPtr)(faces + i * face), bytes, 0, face);
                    cube.SetPixelData(bytes, 0, (CubemapFace)i);
                }
                cube.Apply(false, false);
                images[id] = new Image { texture = cube, width = size, height = size, owned = true };
                return;
            }
            var rt = new RenderTexture(size, size, 0, color == 2 ? RenderTextureFormat.ARGBHalf : RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear)
            {
                dimension = TextureDimension.Cube, useMipMap = true, autoGenerateMips = false, hideFlags = HideFlags.HideAndDontSave,
            };
            rt.Create();
            images[id] = new Image { texture = rt, width = size, height = size, owned = true };
        }

        void MakeCubeFace(uint id, uint cube, int face, uint depthId)
        {
            if (!images.TryGetValue(cube, out var image) || !(image.texture is RenderTexture rt)) return;
            var depth = DepthTexture(rt.width, rt.height, 1);
            targets[id] = new Target { cube = rt, face = (CubemapFace)face, depth = depth, width = rt.width, height = rt.height, imageIds = new[] { depthId } };
            images[depthId] = new Image { texture = depth, element = RenderTextureSubElement.Depth, width = rt.width, height = rt.height };
        }

        static RenderTexture DepthTexture(int width, int height, int samples)
        {
            var depth = new RenderTexture(width, height, GraphicsFormat.None, GraphicsFormat.D24_UNorm_S8_UInt)
            {
                antiAliasing = samples, hideFlags = HideFlags.HideAndDontSave,
            };
            depth.Create();
            return depth;
        }

        void MakeTarget(byte* p)
        {
            uint W(int i) => *(uint*)(p + 4 * i);
            var id = W(0);
            var format = W(1);
            int width = Math.Max(1, (int)W(2)), height = Math.Max(1, (int)W(3));
            var samples = (int)(format >> 16 & 15);
            samples = samples > 1 ? samples : 1;
            var colors = new List<RenderTexture>();
            var ids = new List<uint>();
            for (var i = 0; i < 4; i++)
            {
                var held = format >> (4 * i) & 15;
                if (held == 0) break;
                var rt = new RenderTexture(width, height, 0, held == 2 ? RenderTextureFormat.ARGBHalf : RenderTextureFormat.ARGB32,
                    RenderTextureReadWrite.Linear)
                {
                    antiAliasing = samples, useMipMap = samples == 1, autoGenerateMips = false, hideFlags = HideFlags.HideAndDontSave,
                };
                rt.Create();
                colors.Add(rt);
                images[W(4 + i)] = new Image { texture = rt, width = width, height = height };
                ids.Add(W(4 + i));
            }
            var depth = DepthTexture(width, height, samples);
            if (W(8) != 0)
            {
                images[W(8)] = new Image { texture = depth, element = RenderTextureSubElement.Depth, width = width, height = height };
                ids.Add(W(8));
            }
            targets[id] = new Target { colors = colors.ToArray(), depth = depth, width = width, height = height, imageIds = ids.ToArray() };
        }

        void MakePipeline(byte* p)
        {
            uint W(int i) => *(uint*)(p + 4 * i);
            var made = new Pipeline { key = W(1) | (ulong)W(2) << 32 };
            var at = 3;
            at++; // slot_count: the shader reads the slots its attributes name
            for (var i = 0; i < 3; i++, at += 2) made.slots[i] = new Vector4(W(at), W(at + 1), 0, 0);
            at++; // attribute_count
            for (var i = 0; i < 16; i++, at += 3) made.attributes[i] = new Vector4(W(at), W(at + 2), W(at + 1), 0); // slot, offset, format
            made.uniformSize = (int)W(at++);
            made.textureCount = (int)W(at++);
            at += 8; // reads: the shader says how it reads each
            made.blend = (int)W(at++);
            made.depth = (int)W(at++);
            made.cull = (int)W(at++);
            made.stencilTest = (int)W(at++);
            made.stencilOp = (int)W(at++);
            at += 2; // into, outputs: the target says what it holds
            var nameLength = (int)W(at++);
            made.name = Marshal.PtrToStringAnsi((IntPtr)(p + 4 * at), nameLength);
            var file = made.key.ToString("x16");
            made.shader = Resources.Load<Shader>("TideShaders/" + file);
            made.looked = made.made = Time.frameCount;
            pipelines[W(0)] = made;
        }

        void Begin(uint target, bool clear)
        {
            inPass = true;
            passTarget = target;
            if (target == 0 || !targets.TryGetValue(target, out var t))
            {
                // The camera's own: never cleared, Unity's scene is under what Tide draws. Its rows go as Unity's
                // projection says (_ProjectionParams.x), its colors are written as Unity's color space has them
                commands.SetRenderTarget(BuiltinRenderTextureType.CameraTarget);
                passHeight = attached.pixelHeight;
                flipped = false;
                commands.SetGlobalFloat(Flip, 0f);
                commands.SetGlobalFloat(Linear, linear ? 1f : 0f);
            }
            else
            {
                if (t.cube != null) commands.SetRenderTarget(new RenderTargetIdentifier(t.cube, 0, t.face), new RenderTargetIdentifier(t.depth));
                else if (t.colors.Length == 0) commands.SetRenderTarget(new RenderTargetIdentifier(t.depth));
                else if (t.colors.Length == 1) commands.SetRenderTarget(new RenderTargetIdentifier(t.colors[0]), new RenderTargetIdentifier(t.depth));
                else
                {
                    var ids = new RenderTargetIdentifier[t.colors.Length];
                    for (var i = 0; i < ids.Length; i++) ids[i] = new RenderTargetIdentifier(t.colors[i]);
                    commands.SetRenderTarget(ids, new RenderTargetIdentifier(t.depth));
                }
                if (clear) commands.ClearRenderTarget(RTClearFlags.All, Color.black, 1f, 0);
                passHeight = t.height;
                // A target's rows from the top, as Tide samples them: where Unity's go from the bottom (OpenGL), upside down
                flipped = !SystemInfo.graphicsUVStartsAtTop;
                commands.SetGlobalFloat(Flip, flipped ? -1f : 1f);
                commands.SetGlobalFloat(Linear, 0f);
            }
            commands.SetGlobalFloat(Height, passHeight);
            stencil = 0;
        }

        Material MaterialFor(Pipeline p)
        {
            var stores = p.stencilTest != 0 || p.stencilOp != 0;
            var key = (flipped ? 1 : 0) | (stores ? stencil << 1 : 0);
            if (p.materials.TryGetValue(key, out var m) && m != null) return m;
            m = new Material(p.shader) { hideFlags = HideFlags.HideAndDontSave, name = "Tide " + p.name };
            // How it blends: what tide_gpu_blend says, every color premultiplied
            switch (p.blend)
            {
                case 1: Blend(m, BlendMode.One, BlendMode.One, BlendMode.OneMinusDstAlpha, BlendMode.One); break;
                case 2: Blend(m, BlendMode.DstColor, BlendMode.Zero, BlendMode.Zero, BlendMode.One); break;
                case 3: Blend(m, BlendMode.One, BlendMode.Zero, BlendMode.One, BlendMode.Zero); break;
                default: Blend(m, BlendMode.One, BlendMode.OneMinusSrcAlpha, BlendMode.One, BlendMode.OneMinusSrcAlpha); break;
            }
            // Depth: Unity turns the test around where it keeps depth from 1 near
            m.SetFloat(ZTest, (float)(p.depth == 0 || p.depth == 3 ? CompareFunction.Always : CompareFunction.LessEqual));
            m.SetFloat(ZWrite, p.depth == 1 || p.depth == 3 ? 1f : 0f);
            var cull = p.cull == 1 ? CullMode.Back : p.cull == 2 ? CullMode.Front : CullMode.Off;
            if (flipped && cull != CullMode.Off) cull = cull == CullMode.Back ? CullMode.Front : CullMode.Back;
            m.SetFloat(Cull, (float)cull);
            CompareFunction[] tests =
            {
                CompareFunction.Always, CompareFunction.Never, CompareFunction.Less, CompareFunction.Equal, CompareFunction.LessEqual,
                CompareFunction.Greater, CompareFunction.NotEqual, CompareFunction.GreaterEqual,
            };
            StencilOp[] ops = { StencilOp.Keep, StencilOp.Replace, StencilOp.IncrementSaturate, StencilOp.DecrementSaturate, StencilOp.Zero, StencilOp.Invert };
            m.SetFloat(StencilComp, (float)tests[Math.Min(p.stencilTest, 7)]);
            m.SetFloat(StencilPass, (float)ops[Math.Min(p.stencilOp, 5)]);
            m.SetFloat(StencilRef, stencil);
            m.SetVectorArray(Attributes, p.attributes);
            m.SetVectorArray(Slots, p.slots);
            p.materials[key] = m;
            return m;
        }

        static void Blend(Material m, BlendMode sc, BlendMode dc, BlendMode sa, BlendMode da)
        {
            m.SetFloat(SrcColor, (float)sc);
            m.SetFloat(DstColor, (float)dc);
            m.SetFloat(SrcAlpha, (float)sa);
            m.SetFloat(DstAlpha, (float)da);
        }

        void Draw(uint first, int count, int instances, bool indexed)
        {
            if (!inPass || pipeline == null || count <= 0 || instances <= 0) return;
            if (pipeline.shader == null && pipeline.looked != Time.frameCount)
            {
                // Unity imports a shader tide wrote for a new build as it refreshes: looked for again each frame till then
                pipeline.looked = Time.frameCount;
                var file = pipeline.key.ToString("x16");
                pipeline.shader = Resources.Load<Shader>("TideShaders/" + file);
                // Some frames on, it isn't coming: said once for each
                if (pipeline.shader == null && Time.frameCount - pipeline.made > 120 && missing.Add(pipeline.key))
                {
                    Debug.LogError($"Tide: the shader of Tide's {pipeline.name} pipeline isn't in com.tide.game (Resources/TideShaders/{file}.shader): tide writes it as it builds the game for Unity, so build the game with this tide.");
                }
            }
            if (pipeline.shader == null) return;
            var material = MaterialFor(pipeline);
            for (var i = 0; i < 3; i++)
            {
                commands.SetGlobalBuffer(Vertices[i], buffers.TryGetValue(vertexBuffer[i], out var b) ? b : none);
                commands.SetGlobalInteger(Offsets[i], (int)vertexOffset[i]);
            }
            commands.SetGlobalInteger(First, (int)first);
            if (indexed)
            {
                commands.SetGlobalInteger(Indexed, indexWide ? 2 : 1);
                commands.SetGlobalBuffer(Indices, buffers.TryGetValue(indexBuffer, out var ib) ? ib : none);
            }
            else
            {
                commands.SetGlobalInteger(Indexed, 0);
                commands.SetGlobalBuffer(Indices, none);
            }
            uint modes = 0;
            for (var i = 0; i < pipeline.textureCount && i < 8; i++)
            {
                var mode = sampling[i];
                if (images.TryGetValue(sampled[i], out var image) && image.texture != null)
                {
                    if ((mode & 3) == 2 && !image.mips) mode = mode & ~3u | 1; // No smaller images yet: blended, as gpu.h has it
                    if (image.element == RenderTextureSubElement.Depth) commands.SetGlobalTexture(Textures[i], new RenderTargetIdentifier(image.texture), RenderTextureSubElement.Depth);
                    else commands.SetGlobalTexture(Textures[i], image.texture);
                }
                else
                {
                    commands.SetGlobalTexture(Textures[i], white);
                }
                modes |= (mode & 15) << (4 * i);
            }
            commands.SetGlobalInteger(Sampling, (int)modes);
            if (pipeline.uniformSize > 0)
            {
                var size = Math.Max(16, (pipeline.uniformSize + 15) / 16 * 16);
                commands.SetGlobalConstantBuffer(uniformBuffer, Uniforms, uniformAt, size);
            }
            commands.DrawProcedural(Matrix4x4.identity, material, 0, MeshTopology.Triangles, count, instances);
        }
    }
}
