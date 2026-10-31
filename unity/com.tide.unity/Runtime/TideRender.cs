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
    // into the camera after its transparent objects and with its depth, so
    // Tide's 3D and Unity's scene hide each other. Shapes, text, meshes, 3D,
    // render targets, stencil, blits and the game's own frags and verts are
    // Tide's pixels, through the shaders tide unity wrote into com.tide.game
    // (Resources/TideShaders, by each one's key). In the built-in render
    // pipeline it's a CommandBuffer on the camera; a scriptable pipeline's pass
    // calls Record (Tide.Runtime.URP's does, for URP). The editor's scene views
    // show the frame's 3D through their own cameras (tide_embed_render_view).
    //
    // A frame's commands are read once (Prepare): objects are made and written
    // then, and what draws is kept, each draw with the objects it reads, to be
    // recorded into a camera's commands when it renders. An object written after
    // a draw of this frame read it is a new one from there (as a driver renames a
    // buffer), so the draws already kept read it as it was.
    [DisallowMultipleComponent]
    public sealed unsafe class TideRender : MonoBehaviour
    {
        enum Op : uint
        {
            Buffer = 1, BufferWrite, BufferFree, Texture, TextureWrite, Mipmaps, TextureFree, Cube, CubeFace, Pipeline,
            PipelineFree, Target, TargetFree, ResolveDepth, Begin, End, Scissor, Use, Stencil, Uniforms, Sample, Vertices,
            Indices, Draw, DrawIndexed,
        }

        // A buffer: vertices, read by the shaders as bytes, or indices, which Unity draws through as an index buffer
        // of 16 or 32 bits each (a copy of each kind it's drawn as, made from the bytes kept here).
        sealed class BufferObject
        {
            public GraphicsBuffer raw;
            public Texture2D words; // ...or OpenGL ES 3.0's and WebGL 2's: its words in a texture, each a pixel's bytes, 4,096 a row
            public uint[] shadow;
            public bool indices;
            public byte[] bytes; // An index buffer's
            public int version;
            public readonly GraphicsBuffer[] views = new GraphicsBuffer[2]; // 16 and 32 bits
            public readonly int[] viewVersions = new int[2];
            public readonly int[] viewUsed = new int[2];
            public int used = -1; // The frame a draw last read it
        }

        // A texture a draw samples: a texture's, a cube's, or a target's color or depth.
        sealed class Image
        {
            public Texture texture;
            public RenderTextureSubElement element = RenderTextureSubElement.Default;
            public byte[] pixels; // A Texture2D's, which a part is written into
            public int format, pixelBytes, width, height;
            public bool mips;  // Its smaller images are made
            public bool owned; // Destroyed when it's freed: a target's go with the target
            public int used = -1;
        }

        sealed class Target
        {
            public RenderTexture[] colors;
            public RenderTexture depth;
            public RenderTexture resolved; // A multisampled one's depth, one sample a pixel, once asked for
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
            public bool alphaToCoverage; // A cut-out's into a target with samples: its alpha is the share it covers
            public int looked, made; // The frame its shader was last looked for, while it isn't there, and the pipeline's
            public readonly Dictionary<int, Material> materials = new Dictionary<int, Material>();
            // Its attributes and slots, which each draw takes with it: a material keeps no array of its shader's that
            // isn't a property it declares, and an asset refresh in the editor (as a new build's C# comes) empties them
            public MaterialPropertyBlock properties;
        }

        enum Kind : byte { Begin, End, Scissor, Draw, Mipmaps, ResolveDepth }

        // What a camera's commands get: a pass, a rect, a draw with everything it reads, smaller images made...
        struct Command
        {
            public Kind kind;
            public Target target; // Begin (null for the camera's own), ResolveDepth
            public bool clear, flipped;
            public int x, y, width, height; // Scissor
            public Material material; // Draw
            public MaterialPropertyBlock properties; // ...and its pipeline's attributes and slots
            public GraphicsBuffer v0, v1, v2, index;
            public Texture w0, w1, w2; // The vertices of OpenGL ES 3.0 and WebGL 2, in textures
            public int o0, o1, o2, count, instances, textures, textureCount, uniformAt, uniformSize;
            public uint first, sampling, samplingHigh;
            public RenderTexture mipmapped; // Mipmaps
        }

        // A frame's commands for one camera, kept: its draws, the textures they read, and their uniforms.
        sealed class Recording
        {
            public readonly List<Command> commands = new List<Command>();
            public readonly List<Texture> textures = new List<Texture>();
            public readonly List<RenderTextureSubElement> elements = new List<RenderTextureSubElement>();
            public GraphicsBuffer uniformBuffer;
            public byte[] uniformBytes = new byte[0];
            public int uniformUsed;
            public bool linear;
        }

        public TideRunner Runner { get; set; }

        // The one drawing, while one is (a scriptable pipeline's pass finds it here).
        public static TideRender Current { get; private set; }

        readonly Dictionary<uint, BufferObject> buffers = new Dictionary<uint, BufferObject>();
        readonly Dictionary<uint, Image> images = new Dictionary<uint, Image>();
        readonly Dictionary<uint, Target> targets = new Dictionary<uint, Target>();
        readonly Dictionary<uint, Pipeline> pipelines = new Dictionary<uint, Pipeline>();
        readonly List<(UnityEngine.Object thing, int frame)> doomed = new List<(UnityEngine.Object, int)>();
        readonly List<(GraphicsBuffer buffer, int frame)> doomedBuffers = new List<(GraphicsBuffer, int)>();
        readonly HashSet<ulong> missing = new HashSet<ulong>(); // Shaders said once not to be there
        Dictionary<ulong, ulong> webKeys; // A WGSL's key to its shader's, for a program with no SPIR-V (web-keys.txt)
        readonly Recording frame = new Recording();
        readonly Dictionary<Camera, Recording> views = new Dictionary<Camera, Recording>();
        readonly Dictionary<Camera, CommandBuffer> viewCommands = new Dictionary<Camera, CommandBuffer>();

        CommandBuffer commands;
        Camera attached;
        uint[] words = new uint[0];
        ushort[] halves = new ushort[0];
        GraphicsBuffer none; // What an unused vertex slot reads
        Texture2D noneWords;
        Texture2D white;
        Material resolveDepth;
        bool fresh = true; // Nothing made yet: Tide's renderer makes everything again first
        bool ownSamplers;  // OpenGL's: each texture's own sampler, set as each draw asks (see hlsl.h)
        bool textureBuffers; // OpenGL ES 3.0's and WebGL 2's: buffers as textures of words (hlsl.h)
        bool pipelineSaid; // A scriptable pipeline Tide doesn't draw in was said to be one

        // The frame's commands being read
        Recording into;
        Pipeline pipeline;
        readonly uint[] vertexBuffer = new uint[3];
        readonly uint[] vertexOffset = new uint[3];
        uint indexBuffer;
        bool indexWide;
        const int MaxTextures = 12;
        readonly uint[] sampled = new uint[MaxTextures];
        readonly uint[] sampling = new uint[MaxTextures];
        int uniformAt, uniformSize;
        int stencil;
        bool inPass, flipped;

        static readonly int SrcColor = Shader.PropertyToID("_TideSrcColor"), DstColor = Shader.PropertyToID("_TideDstColor");
        static readonly int SrcAlpha = Shader.PropertyToID("_TideSrcAlpha"), DstAlpha = Shader.PropertyToID("_TideDstAlpha");
        static readonly int ZTest = Shader.PropertyToID("_TideZTest"), ZWrite = Shader.PropertyToID("_TideZWrite");
        static readonly int Cull = Shader.PropertyToID("_TideCull"), StencilRef = Shader.PropertyToID("_TideStencilRef");
        static readonly int StencilComp = Shader.PropertyToID("_TideStencilComp"), StencilPass = Shader.PropertyToID("_TideStencilPass");
        static readonly int AlphaToMask = Shader.PropertyToID("_TideAlphaToMask");
        static readonly int Attributes = Shader.PropertyToID("_TideAttributes"), Slots = Shader.PropertyToID("_TideSlots");
        static readonly int Flip = Shader.PropertyToID("_TideFlip"), Height = Shader.PropertyToID("_TideHeight");
        static readonly int Linear = Shader.PropertyToID("_TideLinear");
        static readonly int First = Shader.PropertyToID("_TideFirst"), Sampling = Shader.PropertyToID("_TideSampling");
        static readonly int SamplingHigh = Shader.PropertyToID("_TideSamplingHigh");
        static readonly int Uniforms = Shader.PropertyToID("TideUniforms"), DepthMS = Shader.PropertyToID("_TideDepthMS");
        static readonly int[] Offsets = { Shader.PropertyToID("_TideOffset0"), Shader.PropertyToID("_TideOffset1"), Shader.PropertyToID("_TideOffset2") };
        static readonly int[] Vertices = { Shader.PropertyToID("_TideVertices0"), Shader.PropertyToID("_TideVertices1"), Shader.PropertyToID("_TideVertices2") };
        static readonly int[] Textures =
        {
            Shader.PropertyToID("_TideTexture0"), Shader.PropertyToID("_TideTexture1"), Shader.PropertyToID("_TideTexture2"),
            Shader.PropertyToID("_TideTexture3"), Shader.PropertyToID("_TideTexture4"), Shader.PropertyToID("_TideTexture5"),
            Shader.PropertyToID("_TideTexture6"), Shader.PropertyToID("_TideTexture7"),
            Shader.PropertyToID("_TideTexture8"), Shader.PropertyToID("_TideTexture9"),
            Shader.PropertyToID("_TideTexture10"), Shader.PropertyToID("_TideTexture11"),
        };

        // Whether this GPU replays Tide's drawing: its shaders read vertices from buffers, or on OpenGL ES 3.0 and
        // WebGL 2 from textures.
        public static bool Supported =>
            SystemInfo.supportsSetConstantBuffer
            && (SystemInfo.graphicsDeviceType == GraphicsDeviceType.OpenGLES3
                || SystemInfo.supportsComputeShaders && SystemInfo.graphicsShaderLevel >= 45);

        // Whether a scriptable render pipeline draws the frame, whose pass records Tide's drawing (Record), rather
        // than the built-in pipeline's camera events.
        public static bool Scriptable => GraphicsSettings.currentRenderPipeline != null;

        // The camera the frame is drawn for: the one the views start with.
        public Camera Camera => attached;

        // Whether Tide's renderer has to make everything it draws with again (the objects here are gone), which
        // TideRunner tells it before the next render.
        public bool Fresh => fresh;

        void OnEnable()
        {
            commands = new CommandBuffer { name = "Tide" };
            var api = SystemInfo.graphicsDeviceType;
            textureBuffers = api == GraphicsDeviceType.OpenGLES3;
            ownSamplers = api == GraphicsDeviceType.OpenGLCore || api == GraphicsDeviceType.OpenGLES3;
            if (textureBuffers)
            {
                noneWords = new Texture2D(1, 1, GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.None) { hideFlags = HideFlags.HideAndDontSave };
                noneWords.SetPixelData(new uint[1], 0);
                noneWords.Apply(false, false);
            }
            else
            {
                none = new GraphicsBuffer(GraphicsBuffer.Target.Raw, 4, 4);
                none.SetData(new uint[4]);
            }
            white = new Texture2D(1, 1, GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.None) { hideFlags = HideFlags.HideAndDontSave };
            white.SetPixelData(new byte[] { 255, 255, 255, 255 }, 0);
            white.Apply(false, false);
            var resolve = Resources.Load<Shader>("TideResolveDepth");
            if (resolve != null && resolve.isSupported) resolveDepth = new Material(resolve) { hideFlags = HideFlags.HideAndDontSave };
            fresh = true;
            Camera.onPreCull += BeforeCamera;
            Current = this;
        }

        void OnDisable()
        {
            if (Current == this) Current = null;
            Camera.onPreCull -= BeforeCamera;
            Attach(null);
            foreach (var pair in viewCommands)
            {
                if (pair.Key != null) pair.Key.RemoveCommandBuffer(CameraEvent.AfterForwardAlpha, pair.Value);
                pair.Value.Release();
            }
            viewCommands.Clear();
            commands?.Release();
            commands = null;
            ForgetAll();
            none?.Release();
            none = null;
            if (noneWords != null) DestroyImmediate(noneWords);
            if (white != null) DestroyImmediate(white);
            if (resolveDepth != null) DestroyImmediate(resolveDepth);
            Release(frame);
            foreach (var view in views.Values) Release(view);
            views.Clear();
        }

        static void Release(Recording r)
        {
            r.uniformBuffer?.Release();
            r.uniformBuffer = null;
            r.commands.Clear();
            r.textures.Clear();
            r.elements.Clear();
        }

        // Everything made, gone: the next frame's commands make it again.
        void ForgetAll()
        {
            foreach (var b in buffers.Values) ReleaseBuffer(b);
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
            Collect(int.MaxValue);
            fresh = true;
        }

        static void ReleaseBuffer(BufferObject b)
        {
            b.raw?.Release();
            if (b.words != null) DestroyImmediate(b.words);
            foreach (var v in b.views) v?.Release();
        }

        // What was let go of up to `frame`: no camera's commands read it any more.
        void Collect(int frame)
        {
            doomed.RemoveAll(d =>
            {
                if (d.frame > frame) return false;
                if (d.thing is RenderTexture rt) rt.Release();
                if (d.thing != null) DestroyImmediate(d.thing);
                return true;
            });
            doomedBuffers.RemoveAll(d =>
            {
                if (d.frame > frame) return false;
                d.buffer.Release();
                return true;
            });
        }

        void Doom(UnityEngine.Object thing)
        {
            if (thing != null) doomed.Add((thing, Time.frameCount));
        }

        void Doom(GraphicsBuffer buffer)
        {
            if (buffer != null) doomedBuffers.Add((buffer, Time.frameCount));
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
            foreach (var rt in new[] { t.depth, t.resolved })
            {
                if (rt == null) continue;
                rt.Release();
                DestroyImmediate(rt);
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
            if (attached != null && commands != null && !Scriptable) attached.AddCommandBuffer(CameraEvent.AfterForwardAlpha, commands);
        }

        // This frame's drawing, `size` bytes of tide_embed_render's commands, for `camera`: read now, and in the
        // built-in pipeline recorded into the camera's commands.
        public void Replay(byte* stream, uint size, Camera camera)
        {
            Collect(Time.frameCount - 3);
            Attach(camera);
            fresh = false;
            commands.Clear();
            if (stream == null || camera == null) return;
            Prepare(frame, stream, size);
            if (!Scriptable) Record(frame, commands, camera, BuiltinRenderTextureType.CameraTarget, BuiltinRenderTextureType.CameraTarget);
            else if (!pipelineSaid && GraphicsSettings.currentRenderPipeline.GetType().Name != "UniversalRenderPipelineAsset")
            {
                pipelineSaid = true;
                Debug.LogWarning($"Tide: Tide's views draw in Unity's built-in render pipeline and in URP, not in {GraphicsSettings.currentRenderPipeline.GetType().Name}.");
            }
        }

        // For a scriptable render pipeline (Tide.Runtime.URP's pass), as `camera` begins rendering: whether Tide draws
        // into it. The frame's camera gets the frame; an editor scene view the frame's 3D through it, read now.
        public bool Prepare(Camera camera)
        {
            if (camera == null) return false;
            if (camera == attached) return !fresh;
            return SceneView(camera) != null;
        }

        // ...and as its pass runs: what Tide draws into it, recorded into `into` onto `color` and `depth`, the
        // camera's own.
        public void Record(Camera camera, CommandBuffer into, RenderTargetIdentifier color, RenderTargetIdentifier depth)
        {
            if (camera == null || into == null) return;
            if (camera == attached) Record(frame, into, camera, color, depth);
            else if (views.TryGetValue(camera, out var view)) Record(view, into, camera, color, depth);
        }

        // The built-in pipeline's scene views: the frame's 3D through each, into commands of its own.
        void BeforeCamera(Camera camera)
        {
            if (Scriptable || camera == null || camera.cameraType != CameraType.SceneView) return;
            if (!viewCommands.TryGetValue(camera, out var cb))
            {
                cb = new CommandBuffer { name = "Tide (scene view)" };
                viewCommands[camera] = cb;
                camera.AddCommandBuffer(CameraEvent.AfterForwardAlpha, cb);
            }
            cb.Clear();
            var view = SceneView(camera);
            if (view != null) Record(view, cb, camera, BuiltinRenderTextureType.CameraTarget, BuiltinRenderTextureType.CameraTarget);
        }

        // A scene view camera's picture of the frame's 3D, read now, or null.
        Recording SceneView(Camera camera)
        {
            if (camera.cameraType != CameraType.SceneView || Runner == null || fresh || attached == null) return null;
            var t = camera.transform;
            var transform = Matrix4x4.TRS(t.position, t.rotation, Vector3.one);
            var projection = camera.projectionMatrix;
            var stream = Runner.RenderView(&transform, &projection, camera.pixelWidth, camera.pixelHeight, out var size);
            if (stream == null) return null;
            if (!views.TryGetValue(camera, out var view)) views[camera] = view = new Recording();
            Prepare(view, stream, size);
            return view;
        }

        static uint Word(byte* p, uint at) => *(uint*)(p + at);

        // Reads commands: objects made and written now, and what draws kept in `r`.
        void Prepare(Recording r, byte* stream, uint size)
        {
            r.commands.Clear();
            r.textures.Clear();
            r.elements.Clear();
            r.linear = QualitySettings.activeColorSpace == ColorSpace.Linear;
            PrepareUniforms(r, stream, size);
            into = r;
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
            if (inPass) r.commands.Add(new Command { kind = Kind.End });
            if (r.uniformBuffer != null && r.uniformBytes.Length > 0) r.uniformBuffer.SetData(r.uniformBytes);
            into = null;
        }

        // Every UNIFORMS command's bytes in one buffer, each where Unity can bind it from.
        static void PrepareUniforms(Recording r, byte* stream, uint size)
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
            if (r.uniformBytes.Length != bytes) r.uniformBytes = new byte[bytes];
            var had = r.uniformBuffer != null ? r.uniformBuffer.count : 0;
            if (had * 16 < bytes)
            {
                r.uniformBuffer?.Release(); // Last frame's draws are drawn
                r.uniformBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Constant, Math.Max(bytes / 16, Math.Max(256, had * 2)), 16);
            }
            r.uniformUsed = 0;
        }

        void Run(Op op, byte* p, uint length)
        {
            uint W(int i) => *(uint*)(p + 4 * i);
            switch (op)
            {
                case Op.Buffer: MakeBuffer(W(0), W(1) == 1, (int)W(2), W(3) != 0 ? p + 16 : null); break;
                case Op.BufferWrite:
                    if (buffers.TryGetValue(W(0), out var written)) Write(written, (int)W(1), p + 12, (int)W(2));
                    break;
                case Op.BufferFree:
                    if (buffers.TryGetValue(W(0), out var freed))
                    {
                        Doom(freed.raw);
                        Doom(freed.words);
                        foreach (var v in freed.views) Doom(v);
                        buffers.Remove(W(0));
                    }
                    break;
                case Op.Texture: MakeTexture(W(0), (int)W(1), (int)W(2), (int)W(3), W(4) != 0 ? p + 20 : null); break;
                case Op.TextureWrite: WriteTexture(W(0), (int)W(1), (int)W(2), (int)W(3), (int)W(4), p + 20); break;
                case Op.Mipmaps: Mipmaps(W(0)); break;
                case Op.TextureFree:
                    if (images.TryGetValue(W(0), out var image))
                    {
                        if (image.owned) Doom(image.texture);
                        images.Remove(W(0));
                    }
                    break;
                case Op.Cube: MakeCube(W(0), (int)W(1), (int)W(2), W(3) != 0 ? p + 16 : null); break;
                case Op.CubeFace: MakeCubeFace(W(0), W(1), (int)W(2), W(3)); break;
                case Op.Pipeline: MakePipeline(p); break;
                case Op.PipelineFree:
                    if (pipelines.TryGetValue(W(0), out var gone))
                    {
                        foreach (var m in gone.materials.Values) Doom(m);
                        pipelines.Remove(W(0));
                    }
                    break;
                case Op.Target: MakeTarget(p); break;
                case Op.TargetFree:
                    if (targets.TryGetValue(W(0), out var target))
                    {
                        foreach (var id in target.imageIds) images.Remove(id);
                        if (target.colors != null) foreach (var c in target.colors) Doom(c);
                        Doom(target.depth);
                        Doom(target.resolved);
                        targets.Remove(W(0));
                    }
                    break;
                case Op.ResolveDepth: ResolveDepth(W(0), W(1)); break;
                case Op.Begin: Begin(W(0), W(1) != 0); break;
                case Op.End:
                    into.commands.Add(new Command { kind = Kind.End });
                    inPass = false;
                    break;
                case Op.Scissor:
                    into.commands.Add(new Command { kind = Kind.Scissor, x = (int)W(0), y = (int)W(1), width = (int)W(2), height = (int)W(3) });
                    break;
                case Op.Use:
                    pipelines.TryGetValue(W(0), out pipeline);
                    break;
                case Op.Stencil: stencil = (int)W(0); break;
                case Op.Uniforms:
                {
                    var size = (int)W(0);
                    var align = Math.Max(16, SystemInfo.constantBufferOffsetAlignment);
                    uniformAt = into.uniformUsed;
                    uniformSize = size;
                    if (uniformAt + size <= into.uniformBytes.Length) Marshal.Copy((IntPtr)(p + 4), into.uniformBytes, uniformAt, size);
                    into.uniformUsed += (size + align - 1) / align * align;
                    break;
                }
                case Op.Sample:
                    if (W(0) < MaxTextures)
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

        void MakeBuffer(uint id, bool indices, int size, byte* data)
        {
            if (buffers.TryGetValue(id, out var had))
            {
                Doom(had.raw);
                Doom(had.words);
                foreach (var v in had.views) Doom(v);
            }
            var b = new BufferObject { indices = indices };
            if (textureBuffers)
            {
                var rows = Math.Max(1, ((size + 3) / 4 + 4095) / 4096);
                b.shadow = new uint[4096 * rows];
                b.words = WordTexture(rows);
            }
            else
            {
                b.raw = new GraphicsBuffer(GraphicsBuffer.Target.Raw | GraphicsBuffer.Target.CopySource | GraphicsBuffer.Target.CopyDestination,
                    Math.Max(1, (size + 3) / 4), 4);
            }
            if (indices) b.bytes = new byte[Math.Max(4, (size + 3) / 4 * 4)];
            buffers[id] = b;
            if (data != null) Write(b, 0, data, size);
        }

        // Bytes into a buffer, now: the frame's writes come before the draws after them, and one a draw of this
        // frame already read is written in a new buffer, its bytes copied first, which the draws after read.
        static Texture2D WordTexture(int rows) =>
            new Texture2D(4096, rows, GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.None)
            {
                hideFlags = HideFlags.HideAndDontSave, filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp,
            };

        void Write(BufferObject b, int offset, byte* data, int size)
        {
            if (b.words != null)
            {
                if (b.used == Time.frameCount)
                {
                    Doom(b.words);
                    b.words = WordTexture(b.shadow.Length / 4096);
                    b.used = -1;
                }
                var room = Math.Max(0, Math.Min(size, b.shadow.Length * 4 - offset));
                fixed (uint* to = b.shadow) Buffer.MemoryCopy(data, (byte*)to + offset, room, room);
                b.words.SetPixelData(b.shadow, 0);
                b.words.Apply(false, false);
                if (b.indices)
                {
                    var kept = Math.Max(0, Math.Min(size, b.bytes.Length - offset));
                    if (kept > 0) Marshal.Copy((IntPtr)data, b.bytes, offset, kept);
                    b.version++;
                }
                return;
            }
            if (b.used == Time.frameCount)
            {
                var renamed = new GraphicsBuffer(b.raw.target, b.raw.count, 4);
                Graphics.CopyBuffer(b.raw, renamed);
                Doom(b.raw);
                b.raw = renamed;
                b.used = -1;
            }
            var count = (size + 3) / 4;
            if (words.Length < count) words = new uint[Math.Max(count, words.Length * 2)];
            fixed (uint* to = words) Buffer.MemoryCopy(data, to, (long)words.Length * 4, size);
            if (offset / 4 + count > b.raw.count) count = b.raw.count - offset / 4;
            if (count > 0) b.raw.SetData(words, 0, offset / 4, count);
            if (b.indices)
            {
                var room = Math.Max(0, Math.Min(size, b.bytes.Length - offset));
                if (room > 0) Marshal.Copy((IntPtr)data, b.bytes, offset, room);
                b.version++;
            }
        }

        // An index buffer as Unity draws through it, of 16 or 32 bits each, as its bytes are now.
        GraphicsBuffer IndexView(BufferObject b, bool wide)
        {
            var k = wide ? 1 : 0;
            var stride = wide ? 4 : 2;
            var view = b.views[k];
            if (view != null && b.viewVersions[k] == b.version)
            {
                b.viewUsed[k] = Time.frameCount;
                return view;
            }
            if (view == null || b.viewUsed[k] == Time.frameCount)
            {
                Doom(view);
                view = b.views[k] = new GraphicsBuffer(GraphicsBuffer.Target.Index, Math.Max(1, b.bytes.Length / stride), stride);
            }
            if (wide)
            {
                if (words.Length < b.bytes.Length / 4) words = new uint[b.bytes.Length / 4];
                Buffer.BlockCopy(b.bytes, 0, words, 0, b.bytes.Length / 4 * 4);
                view.SetData(words, 0, 0, b.bytes.Length / 4);
            }
            else
            {
                if (halves.Length < b.bytes.Length / 2) halves = new ushort[b.bytes.Length / 2];
                Buffer.BlockCopy(b.bytes, 0, halves, 0, b.bytes.Length / 2 * 2);
                view.SetData(halves, 0, 0, b.bytes.Length / 2);
            }
            b.viewVersions[k] = b.version;
            b.viewUsed[k] = Time.frameCount;
            return view;
        }

        static GraphicsFormat TextureFormat(int format) => format switch
        {
            1 => GraphicsFormat.R8_UNorm,
            2 => GraphicsFormat.R16_SFloat,
            3 => GraphicsFormat.R32G32B32A32_SInt,
            _ => GraphicsFormat.R8G8B8A8_UNorm,
        };

        static int PixelBytes(int format) => format == 1 ? 1 : format == 2 ? 2 : format == 3 ? 16 : 4;

        static Texture2D NewTexture(int format, int width, int height)
        {
            var flags = format == 3 ? TextureCreationFlags.None : TextureCreationFlags.MipChain;
            return new Texture2D(Math.Max(1, width), Math.Max(1, height), TextureFormat(format), flags) { hideFlags = HideFlags.HideAndDontSave };
        }

        void MakeTexture(uint id, int format, int width, int height, byte* pixels)
        {
            var texture = NewTexture(format, width, height);
            var bytes = PixelBytes(format);
            var image = new Image { texture = texture, format = format, pixelBytes = bytes, width = width, height = height, owned = true };
            image.pixels = new byte[Math.Max(1, width * height * bytes)];
            if (pixels != null) Marshal.Copy((IntPtr)pixels, image.pixels, 0, width * height * bytes);
            texture.SetPixelData(image.pixels, 0);
            texture.Apply(false, false);
            if (images.TryGetValue(id, out var had) && had.owned) Doom(had.texture);
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
            if (image.used == Time.frameCount)
            {
                // A draw of this frame reads it as it was: a new texture from here
                Doom(texture);
                texture = NewTexture(image.format, image.width, image.height);
                image.texture = texture;
                image.used = -1;
                texture.SetPixelData(image.pixels, 0);
                texture.Apply(image.mips, false);
                return;
            }
            texture.SetPixelData(image.pixels, 0);
            texture.Apply(false, false); // Its smaller images stay as they were made
        }

        void Mipmaps(uint id)
        {
            if (!images.TryGetValue(id, out var image)) return;
            if (image.texture is Texture2D texture && texture.mipmapCount > 1) texture.Apply(true, false);
            else if (image.texture is Cubemap cube) cube.Apply(true, false);
            else if (image.texture is RenderTexture rt && rt.useMipMap) into.commands.Add(new Command { kind = Kind.Mipmaps, mipmapped = rt });
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
                antiAliasing = samples, bindTextureMS = samples > 1, hideFlags = HideFlags.HideAndDontSave,
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

        // A multisampled target's depth, a sample of each pixel's, into a depth texture of one sample a pixel: the
        // texture `depthId` from here, made again where the frame's commands are at.
        void ResolveDepth(uint id, uint depthId)
        {
            if (!targets.TryGetValue(id, out var t) || t.depth == null || t.depth.antiAliasing <= 1) return;
            if (t.resolved == null)
            {
                t.resolved = DepthTexture(t.width, t.height, 1);
                var ids = new List<uint>(t.imageIds) { depthId };
                t.imageIds = ids.ToArray();
            }
            images[depthId] = new Image { texture = t.resolved, element = RenderTextureSubElement.Depth, width = t.width, height = t.height };
            into.commands.Add(new Command { kind = Kind.ResolveDepth, target = t });
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
            at += made.textureCount; // ABI 6: one read kind per actual texture.
            made.blend = (int)W(at++);
            made.depth = (int)W(at++);
            made.cull = (int)W(at++);
            made.stencilTest = (int)W(at++);
            made.stencilOp = (int)W(at++);
            at += 2; // into, outputs: the target says what it holds
            made.alphaToCoverage = W(at++) != 0;
            var nameLength = (int)W(at++);
            made.name = Marshal.PtrToStringAnsi((IntPtr)(p + 4 * at), nameLength);
            made.properties = new MaterialPropertyBlock();
            made.properties.SetVectorArray(Attributes, made.attributes);
            made.properties.SetVectorArray(Slots, made.slots);
            made.shader = ShaderOf(made.key);
            made.looked = made.made = Time.frameCount;
            pipelines[W(0)] = made;
        }

        // The shader tide unity wrote for a pipeline's key: its SPIR-V's, or on the web its WGSL's.
        Shader ShaderOf(ulong key)
        {
            var shader = Resources.Load<Shader>("TideShaders/" + key.ToString("x16"));
            if (shader != null) return shader;
            if (webKeys == null)
            {
                webKeys = new Dictionary<ulong, ulong>();
                var keys = Resources.Load<TextAsset>("TideShaders/web-keys");
                foreach (var line in keys != null ? keys.text.Split('\n') : Array.Empty<string>())
                {
                    var pair = line.Trim().Split(' ');
                    if (pair.Length == 2 && ulong.TryParse(pair[0], System.Globalization.NumberStyles.HexNumber, null, out var web)
                        && ulong.TryParse(pair[1], System.Globalization.NumberStyles.HexNumber, null, out var file)) webKeys[web] = file;
                }
            }
            return webKeys.TryGetValue(key, out var named) ? Resources.Load<Shader>("TideShaders/" + named.ToString("x16")) : null;
        }

        void Begin(uint target, bool clear)
        {
            inPass = true;
            stencil = 0;
            if (target == 0 || !targets.TryGetValue(target, out var t))
            {
                // The camera's own: never cleared, Unity's scene is under what Tide draws
                flipped = false;
                into.commands.Add(new Command { kind = Kind.Begin });
                return;
            }
            // A target's rows from the top, as Tide samples them: where Unity's go from the bottom (OpenGL), upside down
            flipped = !SystemInfo.graphicsUVStartsAtTop;
            into.commands.Add(new Command { kind = Kind.Begin, target = t, clear = clear, flipped = flipped, height = t.height });
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
            m.SetFloat(AlphaToMask, p.alphaToCoverage ? 1f : 0f);
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

        GraphicsBuffer VertexBuffer(int slot)
        {
            if (textureBuffers || !buffers.TryGetValue(vertexBuffer[slot], out var b)) return none;
            b.used = Time.frameCount;
            return b.raw;
        }

        Texture VertexWords(int slot)
        {
            if (!textureBuffers) return null;
            if (!buffers.TryGetValue(vertexBuffer[slot], out var b) || b.words == null) return noneWords;
            b.used = Time.frameCount;
            return b.words;
        }

        void Draw(uint first, int count, int instances, bool indexed)
        {
            if (!inPass || pipeline == null || count <= 0 || instances <= 0) return;
            if (pipeline.shader == null && pipeline.looked != Time.frameCount)
            {
                // Unity imports a shader tide wrote for a new build as it refreshes: looked for again each frame till then
                pipeline.looked = Time.frameCount;
                var file = pipeline.key.ToString("x16");
                webKeys = null; // Read again: tide may have written new ones
                pipeline.shader = ShaderOf(pipeline.key);
                // Some frames on, it isn't coming: said once for each
                if (pipeline.shader == null && Time.frameCount - pipeline.made > 120 && missing.Add(pipeline.key))
                {
                    Debug.LogError($"Tide: the shader of Tide's {pipeline.name} pipeline isn't in com.tide.game (Resources/TideShaders/{file}.shader): tide writes it as it builds the game for Unity, so build the game with this tide.");
                }
            }
            if (pipeline.shader == null) return;
            var c = new Command
            {
                kind = Kind.Draw, material = MaterialFor(pipeline), properties = pipeline.properties, count = count,
                instances = instances, first = indexed ? 0 : first,
                v0 = VertexBuffer(0), v1 = VertexBuffer(1), v2 = VertexBuffer(2),
                w0 = VertexWords(0), w1 = VertexWords(1), w2 = VertexWords(2),
                o0 = (int)vertexOffset[0], o1 = (int)vertexOffset[1], o2 = (int)vertexOffset[2],
                textures = into.textures.Count,
                uniformAt = uniformAt, uniformSize = pipeline.uniformSize > 0 ? Math.Max(16, (pipeline.uniformSize + 15) / 16 * 16) : 0,
            };
            if (indexed)
            {
                if (!buffers.TryGetValue(indexBuffer, out var ib) || !ib.indices) return;
                c.index = IndexView(ib, indexWide);
            }
            uint modes = 0, modesHigh = 0;
            for (var i = 0; i < pipeline.textureCount && i < MaxTextures; i++)
            {
                var mode = sampling[i];
                Texture texture = white;
                var element = RenderTextureSubElement.Default;
                if (images.TryGetValue(sampled[i], out var image) && image.texture != null)
                {
                    if ((mode & 3) == 2 && !image.mips) mode = mode & ~3u | 1; // No smaller images yet: blended, as gpu.h has it
                    texture = image.texture;
                    element = image.element;
                    image.used = Time.frameCount;
                    if (ownSamplers && element == RenderTextureSubElement.Default) Sample(texture, mode);
                }
                into.textures.Add(texture);
                into.elements.Add(element);
                if (i < 8) modes |= (mode & 15) << (4 * i);
                else modesHigh |= (mode & 15) << (4 * (i - 8));
            }
            c.textureCount = into.textures.Count - c.textures;
            c.sampling = modes;
            c.samplingHigh = modesHigh;
            into.commands.Add(c);
        }

        // OpenGL's: the texture's own sampler, as the draw asks for it (the frame's last draw of it says, for all of
        // them: OpenGL in Unity has no samplers apart from textures).
        static void Sample(Texture texture, uint mode)
        {
            var filter = mode & 3;
            texture.filterMode = filter == 0 ? FilterMode.Point : filter == 1 ? FilterMode.Bilinear : FilterMode.Trilinear;
            var wrap = mode >> 2 & 3;
            texture.wrapMode = wrap == 1 ? TextureWrapMode.Repeat : wrap == 2 ? TextureWrapMode.Mirror : TextureWrapMode.Clamp;
        }

        // A recording into a camera's commands, onto `color` and `depth`, the camera's own.
        void Record(Recording r, CommandBuffer cb, Camera camera, RenderTargetIdentifier color, RenderTargetIdentifier depth)
        {
            var passHeight = camera.pixelHeight;
            foreach (var c in r.commands)
            {
                switch (c.kind)
                {
                    case Kind.Begin:
                        if (c.target == null)
                        {
                            // Its rows go as Unity's projection says (_ProjectionParams.x), its colors as Unity's color space has them
                            cb.SetRenderTarget(color, depth);
                            passHeight = camera.pixelHeight;
                            cb.SetGlobalFloat(Flip, 0f);
                            cb.SetGlobalFloat(Linear, r.linear ? 1f : 0f);
                        }
                        else
                        {
                            var t = c.target;
                            if (t.cube != null) cb.SetRenderTarget(new RenderTargetIdentifier(t.cube, 0, t.face), new RenderTargetIdentifier(t.depth));
                            else if (t.colors.Length == 0) cb.SetRenderTarget(new RenderTargetIdentifier(t.depth));
                            else if (t.colors.Length == 1) cb.SetRenderTarget(new RenderTargetIdentifier(t.colors[0]), new RenderTargetIdentifier(t.depth));
                            else
                            {
                                var ids = new RenderTargetIdentifier[t.colors.Length];
                                for (var i = 0; i < ids.Length; i++) ids[i] = new RenderTargetIdentifier(t.colors[i]);
                                cb.SetRenderTarget(ids, new RenderTargetIdentifier(t.depth));
                            }
                            if (c.clear) cb.ClearRenderTarget(RTClearFlags.All, Color.black, 1f, 0);
                            passHeight = c.height;
                            cb.SetGlobalFloat(Flip, c.flipped ? -1f : 1f);
                            cb.SetGlobalFloat(Linear, 0f);
                        }
                        cb.SetGlobalFloat(Height, passHeight);
                        break;
                    case Kind.End:
                        cb.DisableScissorRect();
                        break;
                    case Kind.Scissor:
                        cb.EnableScissorRect(new Rect(c.x, passHeight - c.y - c.height, c.width, c.height)); // Unity counts rows from the bottom
                        break;
                    case Kind.Mipmaps:
                        cb.GenerateMips(c.mipmapped);
                        break;
                    case Kind.ResolveDepth:
                        if (resolveDepth == null) break;
                        cb.SetRenderTarget(new RenderTargetIdentifier(c.target.resolved));
                        cb.SetGlobalTexture(DepthMS, new RenderTargetIdentifier(c.target.depth), RenderTextureSubElement.Depth);
                        cb.DrawProcedural(Matrix4x4.identity, resolveDepth, 0, MeshTopology.Triangles, 3, 1);
                        break;
                    case Kind.Draw:
                        if (c.w0 != null)
                        {
                            cb.SetGlobalTexture(Vertices[0], c.w0);
                            cb.SetGlobalTexture(Vertices[1], c.w1);
                            cb.SetGlobalTexture(Vertices[2], c.w2);
                        }
                        else
                        {
                            cb.SetGlobalBuffer(Vertices[0], c.v0);
                            cb.SetGlobalBuffer(Vertices[1], c.v1);
                            cb.SetGlobalBuffer(Vertices[2], c.v2);
                        }
                        cb.SetGlobalInteger(Offsets[0], c.o0);
                        cb.SetGlobalInteger(Offsets[1], c.o1);
                        cb.SetGlobalInteger(Offsets[2], c.o2);
                        cb.SetGlobalInteger(First, (int)c.first);
                        for (var i = 0; i < c.textureCount; i++)
                        {
                            var texture = r.textures[c.textures + i];
                            if (r.elements[c.textures + i] == RenderTextureSubElement.Depth)
                                cb.SetGlobalTexture(Textures[i], new RenderTargetIdentifier(texture), RenderTextureSubElement.Depth);
                            else cb.SetGlobalTexture(Textures[i], texture);
                        }
                        cb.SetGlobalInteger(Sampling, (int)c.sampling);
                        cb.SetGlobalInteger(SamplingHigh, (int)c.samplingHigh);
                        if (c.uniformSize > 0) cb.SetGlobalConstantBuffer(r.uniformBuffer, Uniforms, c.uniformAt, c.uniformSize);
                        if (c.index != null)
                        {
                            cb.DrawProcedural(c.index, Matrix4x4.identity, c.material, 0, MeshTopology.Triangles, c.count, c.instances,
                                c.properties);
                        }
                        else cb.DrawProcedural(Matrix4x4.identity, c.material, 0, MeshTopology.Triangles, c.count, c.instances, c.properties);
                        break;
                }
            }
        }
    }
}
