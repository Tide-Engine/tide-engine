using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
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
    // then, and camera-own drawing is kept, each draw with the objects it reads, to be
    // recorded into a camera's commands when it renders. An object written after
    // a draw of this frame read it is a new one from there (as a driver renames a
    // buffer), so the draws already kept read it as it was.
    //
    // Compute (gpu.h's storage buffers, kernels, copies and reads back) runs
    // once a frame, however many cameras draw it: as the commands are read, in
    // their order, through commands of its own that Unity runs then (Flush). A
    // storage buffer a kernel or a copy writes after a kept draw read it is
    // renamed as any written buffer is, so each draw reads the buffers as they
    // were where it is in the frame, and an indirect call reads its numbers
    // from a copy made where it is. Offscreen passes run in that same stream,
    // once, so kernels see earlier draws and later draws see their writes.
    [DisallowMultipleComponent]
    public sealed unsafe class TideRender : MonoBehaviour
    {
        enum Op : uint
        {
            Buffer = 1, BufferWrite, BufferFree, Texture, TextureWrite, Mipmaps, TextureFree, Cube, CubeFace, Pipeline,
            PipelineFree, Target, TargetFree, ResolveDepth, Begin, End, Scissor, Use, Stencil, Uniforms, Image, Vertices,
            Indices, Draw, DrawIndexed, PipelineStorage, Storage, ComputeBegin, ComputeEnd, Dispatch, DispatchIndirect,
            DrawIndirect, DrawIndexedIndirect, BufferCopy, BufferRead, DrawIndexedIndirectCount, Sampler, Array, ArrayLayer,
            ArrayOf, ArrayWrite, TextureBlocks, ArrayBlocks, Volume, VolumeWrite, PipelineImages, DepthBias,
        }

        // A buffer: vertices, read by the shaders as bytes, or indices, which Unity draws through as an index buffer
        // of 16 or 32 bits each (a copy of each kind it's drawn as, made from the bytes kept here).
        sealed class BufferObject
        {
            public GraphicsBuffer raw;
            public bool indices;
            public bool storage; // A storage buffer: kernels read and write it, indirect calls read their numbers there
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
            public int format, pixelBytes, width, height, layers; // An array written a part at a time: its layers
            public int depth; // A volume's cells along z; zero for a 2D image or an array
            public bool mips;  // Its smaller images are made
            public bool owned; // Destroyed when it's freed: a target's go with the target
            public RenderTexture arrayDepth; // An array drawn into: its depth array, freed with it...
            public uint arrayDepthId;        // ...which is read as this image
            public int used = -1;
            public uint own = 15; // OpenGL Core's: how it samples itself this frame (Own), 15 for not yet
            // A Gamma project's [Linear] image (Unity has no sRGB formats there): its bytes as they're stored, a UNorm
            // Texture2D or Texture2DArray (a cube's six faces as layers), which `texture` holds decoded to linear light
            // in half floats, so it's filtered and its smaller images made in linear light as an sRGB format's are
            public Texture encoded;
            public int levels = 1;  // ...and how many of its levels are given (a pack's blocks), decoded each
            public bool authored;   // Its smaller images came with it (blocks): never made again
            // A multisampled target's color: what's drawn into, which `texture` is until its smaller images are made,
            // and its one-sample copy that has them (Mipmaps resolves into it), which `texture` is from then until the
            // target is drawn into again; and the frame a camera's draw read each, while it isn't `texture`
            public RenderTexture drawn, mipped;
            public int drawnUsed = -1, mippedUsed = -1;
            public RenderTexture faceColor; // A cube drawn into: the one 2D image its faces' passes draw into (MakeCubeFace)
        }

        sealed class Target
        {
            public RenderTexture[] colors;
            public RenderTargetIdentifier[] colorIds; // What a pass on several colors hands Unity, filled as it begins
            public RenderTexture depth;
            public RenderTexture resolved; // A multisampled one's depth, one sample a pixel, once asked for
            public RenderTexture cube; // A cube face's: the cube, and which face
            public CubemapFace face;
            // ...and the image its passes draw into, copied into the face as each ends: one for all six faces, the
            // cube's (Image.faceColor), as one face's pass runs at a time, with the face copied into it first where the
            // pass doesn't clear. Unity binds no depth beside a cube's face unless the depth is a cube's too, so a face
            // drawn into directly had no depth test and its depth, which is read as an image of its own, was never
            // written
            public RenderTexture faceColor;
            public RenderTexture layers, layersDepth; // An array's layer: the array and its depth array (either may be null)...
            public int layer = -1;                    // ...and which layer
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
            public int samplerCount; // Its samplers, apart from its textures (_TideSampling is by sampler)...
            public uint[] textureSamplers = new uint[16]; // ...and which read each texture, bit j for sampler j
            public bool alphaToCoverage; // A cut-out's into a target with samples: its alpha is the share it covers
            public int storageCount;     // The storage buffers it binds (_TideStorage<i>), and which of them it writes
            public uint storageWrites;
            public uint imageWrites; // Written image slots (the existing _TideTexture<i> namespace)
            public readonly int[] imageFormats = new int[16];
            public readonly uint[] imageReads = new uint[16];
            public bool compute;         // A kernel's: its .compute's tidehl_compute, not a shader's pass
            public ComputeShader kernelShader;
            public int kernel = -1;      // ...found once it's there; -2 for one this GPU can't run
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
            public float bias, slopeBias;
            public Kind kind;
            public Target target; // Begin (null for the camera's own), ResolveDepth
            public bool clear, flipped;
            public int x, y, width, height; // Scissor
            public Material material; // Draw
            public MaterialPropertyBlock properties; // ...and its pipeline's attributes and slots
            public GraphicsBuffer v0, v1, v2, index;
            public int o0, o1, o2, count, instances, textures, textureCount, uniformAt, uniformSize, shaderPass;
            public uint first, sampling, samplingHigh, unmipped, own, ownHigh;
            public RenderTexture mipmapped; // Mipmaps
            public int storage, storageCount; // Draw: the storage buffers it reads, in its recording's `storage`
            public GraphicsBuffer arguments; // ...and an indirect one's numbers, copied where it is in the frame
        }

        // A frame's commands for one camera, kept: its draws, the textures they read, and their uniforms.
        sealed class Recording
        {
            public readonly List<Command> commands = new List<Command>();
            public readonly List<Texture> textures = new List<Texture>();
            public readonly List<RenderTextureSubElement> elements = new List<RenderTextureSubElement>();
            public readonly List<GraphicsBuffer> storage = new List<GraphicsBuffer>();
            public GraphicsBuffer uniformBuffer;
            // The uniform buffer's bytes, of its whole size. Native, as is everything written to the GPU each frame:
            // Unity's SetData of a managed array makes garbage at every call
            public NativeArray<byte> uniformBytes;
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
        readonly List<(UnityEngine.Object thing, int frame, bool again)> doomed = new List<(UnityEngine.Object, int, bool)>();
        readonly List<(GraphicsBuffer buffer, int frame, bool again)> doomedBuffers = new List<(GraphicsBuffer, int, bool)>();
        // Buffers a rename let go of, once no camera's commands read them: the next rename of a buffer that size takes
        // one, so a buffer written after a draw read it, each frame, goes round a few buffers rather than making one a
        // frame. One nothing took for a while is released.
        readonly List<(GraphicsBuffer buffer, int since)> spares = new List<(GraphicsBuffer, int)>();
        const int MaxSpares = 64;
        // Images likewise: a target drawn into, or an image a kernel writes, after a draw read it, each frame
        readonly List<(RenderTexture texture, int since)> spareImages = new List<(RenderTexture, int)>();
        const int MaxSpareImages = 32;
        readonly HashSet<ulong> missing = new HashSet<ulong>(); // Shaders said once not to be there
        Dictionary<ulong, ulong> webKeys; // A WGSL's key to its shader's, for a program with no SPIR-V (web-keys.txt)
        readonly Recording frame = new Recording();
        readonly Dictionary<Camera, Recording> views = new Dictionary<Camera, Recording>();
        readonly Dictionary<Camera, CommandBuffer> viewCommands = new Dictionary<Camera, CommandBuffer>();

        CommandBuffer commands;
        Camera attached;
        NativeArray<uint> words;  // What a buffer's write hands to Unity (Words)
        NativeArray<uint> groupWords; // A direct dispatch's TideGroups
        GraphicsBuffer none; // What an unused vertex slot reads
        Texture2D white;
        Texture3D emptyVolume;
        Texture2D emptyNumber;
        RenderTexture emptyInteger, emptyIntVolume;
        // What a read of a cube, an array, a depth or the numbers of layers gets where it's given no image that's there:
        // a dark cube, a white array, the far depth (a compare: lit), zeros of floats or of ints, of the read's own
        // dimension (gpu.h's fallbacks, as the engine's are on the other backends)
        Cubemap emptyCube;
        Texture2DArray emptyArray, emptyNumberArray;
        RenderTexture emptyIntArray, emptyDepth, emptyDepthArray;
        Material resolveDepth;
        bool fresh = true; // Nothing made yet: Tide's renderer makes everything again first
        bool ownSamplers;  // OpenGL Core's: each texture's own sampler, set as each draw asks (see hlsl.h)
        bool pipelineSaid; // A scriptable pipeline Tide doesn't draw in was said to be one

        // Offscreen targets and compute: work runs once, as commands are read (Flush); the copy kernel
        // (Resources/TideCompute.compute); and indirect calls' numbers, each copied where it's asked for into buffers
        // of pools taken in turn each frame
        CommandBuffer work;
        bool working;
        ComputeShader copier;
        int copyKernel = -1;
        readonly List<GraphicsBuffer> staged = new List<GraphicsBuffer>();    // Raw: the numbers, copied from storage...
        readonly List<GraphicsBuffer> arguments = new List<GraphicsBuffer>(); // ...into IndirectArguments
        readonly List<GraphicsBuffer> groups = new List<GraphicsBuffer>();    // ...or a dispatch's TideGroups
        int stagedUsed, groupsUsed;
        int readEpoch; // Reads begun before everything was let go of aren't answered: the embed forgot them
        bool copySaid; // That the copy kernel isn't there, said once

        // The frame's commands being read
        Recording into;
        Pipeline pipeline;
        readonly uint[] vertexBuffer = new uint[3];
        readonly uint[] vertexOffset = new uint[3];
        uint indexBuffer;
        bool indexWide;
        const int MaxTextures = 16; // gpu.h's TIDE_GPU_MAX_TEXTURES
        readonly uint[] sampled = new uint[MaxTextures];  // By texture slot (IMAGE)
        readonly uint[] sampling = new uint[MaxTextures]; // By sampler slot (SAMPLER)
        int uniformAt, uniformSize;
        int stencil;
        float bias, slopeBias;
        bool inPass, flipped, inCompute;
        bool offscreenPass;
        int passStart;
        bool volumeSaid;
        Material srgbMipMaterial, srgbDecodeMaterial;
        RenderTexture srgbMipScratch;
        static readonly int SrgbMipInfo = Shader.PropertyToID("_TideSrgbMipInfo");
        static readonly int SrgbSource2D = Shader.PropertyToID("_TideSrgbSource2D");
        static readonly int SrgbSourceArray = Shader.PropertyToID("_TideSrgbSourceArray");
        static readonly int SrgbSourceCube = Shader.PropertyToID("_TideSrgbSourceCube");
        const int MaxStorage = 4; // gpu.h's TIDE_GPU_MAX_STORAGE
        readonly uint[] storageBound = new uint[MaxStorage];

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
        static readonly int Unmipped = Shader.PropertyToID("_TideUnmipped");
        static readonly int OwnLow = Shader.PropertyToID("_TideOwn"), OwnHigh = Shader.PropertyToID("_TideOwnHigh");
        static readonly int Uniforms = Shader.PropertyToID("TideUniforms"), DepthMS = Shader.PropertyToID("_TideDepthMS");
        static readonly int[] Offsets = { Shader.PropertyToID("_TideOffset0"), Shader.PropertyToID("_TideOffset1"), Shader.PropertyToID("_TideOffset2") };
        static readonly int[] Vertices = { Shader.PropertyToID("_TideVertices0"), Shader.PropertyToID("_TideVertices1"), Shader.PropertyToID("_TideVertices2") };
        static readonly int[] Storage =
        {
            Shader.PropertyToID("_TideStorage0"), Shader.PropertyToID("_TideStorage1"), Shader.PropertyToID("_TideStorage2"),
            Shader.PropertyToID("_TideStorage3"),
        };
        static readonly int Groups = Shader.PropertyToID("TideGroups");
        static readonly int CopyFrom = Shader.PropertyToID("_TideFrom"), CopyTo = Shader.PropertyToID("_TideTo");
        static readonly int CopyWords = Shader.PropertyToID("_TideCopy");
        static readonly int[] Textures =
        {
            Shader.PropertyToID("_TideTexture0"), Shader.PropertyToID("_TideTexture1"), Shader.PropertyToID("_TideTexture2"),
            Shader.PropertyToID("_TideTexture3"), Shader.PropertyToID("_TideTexture4"), Shader.PropertyToID("_TideTexture5"),
            Shader.PropertyToID("_TideTexture6"), Shader.PropertyToID("_TideTexture7"),
            Shader.PropertyToID("_TideTexture8"), Shader.PropertyToID("_TideTexture9"),
            Shader.PropertyToID("_TideTexture10"), Shader.PropertyToID("_TideTexture11"),
            Shader.PropertyToID("_TideTexture12"), Shader.PropertyToID("_TideTexture13"),
            Shader.PropertyToID("_TideTexture14"), Shader.PropertyToID("_TideTexture15"),
        };

        // Whether this GPU replays Tide's drawing: it computes, as each of Tide's renderers does, and its shaders read
        // raw buffers, three of vertices and four of storage a stage. Unity's OpenGL Core (4.5) does; OpenGL ES 3.0 and
        // WebGL 2 don't, as Tide has no backend that can't compute.
        // The block formats this GPU samples, as tide_embed_render_formats takes them: 1 << 1 for BC1, 1 << 3 for BC3
        public static uint BlockFormats =>
            (SystemInfo.SupportsTextureFormat(UnityEngine.TextureFormat.DXT1) ? 1u << 1 : 0u)
            | (SystemInfo.SupportsTextureFormat(UnityEngine.TextureFormat.DXT5) ? 1u << 3 : 0u)
            | (DepthFormat() == GraphicsFormat.D32_SFloat_S8_UInt ? 1u << 31 : 0u);

        public static bool Supported =>
            SystemInfo.supportsSetConstantBuffer && SystemInfo.supportsComputeShaders && SystemInfo.supportsIndirectArgumentsBuffer
            && SystemInfo.graphicsShaderLevel >= 45 && SystemInfo.graphicsDeviceType != GraphicsDeviceType.OpenGLES3
            && SystemInfo.maxComputeBufferInputsCompute >= MaxStorage && SystemInfo.maxComputeBufferInputsFragment >= MaxStorage
            && SystemInfo.maxComputeBufferInputsVertex >= 3 + MaxStorage;

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
            ownSamplers = SystemInfo.graphicsDeviceType == GraphicsDeviceType.OpenGLCore;
            none = new GraphicsBuffer(GraphicsBuffer.Target.Raw, 4, 4);
            none.SetData(new uint[4]);
            white = new Texture2D(1, 1, GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.None) { hideFlags = HideFlags.HideAndDontSave };
            white.SetPixelData(new byte[] { 255, 255, 255, 255 }, 0);
            white.Apply(false, false);
            emptyVolume = new Texture3D(1, 1, 1, GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.None)
            { hideFlags = HideFlags.HideAndDontSave };
            emptyVolume.SetPixelData(new byte[4], 0);
            emptyVolume.Apply(false, true);
            var resolve = Resources.Load<Shader>("TideResolveDepth");
            if (resolve != null && resolve.isSupported) resolveDepth = new Material(resolve) { hideFlags = HideFlags.HideAndDontSave };
            work = new CommandBuffer { name = "Tide targets and compute" };
            copier = Resources.Load<ComputeShader>("TideCompute");
            copyKernel = copier != null && copier.HasKernel("TideCopy") ? copier.FindKernel("TideCopy") : -1;
            if (copyKernel >= 0 && !copier.IsSupported(copyKernel)) copyKernel = -1;
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
            work?.Release();
            work = null;
            working = false;
            foreach (var pool in new[] { staged, arguments, groups })
            {
                foreach (var b in pool) b.Release();
                pool.Clear();
            }
            none?.Release();
            none = null;
            if (white != null) DestroyImmediate(white);
            if (emptyVolume != null) DestroyImmediate(emptyVolume);
            if (emptyNumber != null) DestroyImmediate(emptyNumber);
            if (emptyInteger != null) DestroyImmediate(emptyInteger);
            foreach (var empty in new Texture[] { emptyCube, emptyArray, emptyNumberArray, emptyIntArray, emptyDepth, emptyDepthArray })
                if (empty != null) DestroyImmediate(empty);
            emptyCube = null;
            emptyArray = emptyNumberArray = null;
            emptyIntArray = emptyDepth = emptyDepthArray = null;
            if (emptyIntVolume != null) DestroyImmediate(emptyIntVolume);
            if (resolveDepth != null) DestroyImmediate(resolveDepth);
            Release(frame);
            foreach (var view in views.Values) Release(view);
            views.Clear();
            if (words.IsCreated) words.Dispose();
            words = default;
            if (groupWords.IsCreated) groupWords.Dispose();
            groupWords = default;
        }

        static void Release(Recording r)
        {
            r.uniformBuffer?.Release();
            r.uniformBuffer = null;
            if (r.uniformBytes.IsCreated) r.uniformBytes.Dispose();
            r.uniformBytes = default;
            r.commands.Clear();
            r.textures.Clear();
            r.elements.Clear();
            r.storage.Clear();
        }

        // Everything made, gone: the next frame's commands make it again.
        void ForgetAll()
        {
            Flush(); // What the work reads goes now
            foreach (var b in buffers.Values) ReleaseBuffer(b);
            buffers.Clear();
            foreach (var i in images.Values)
            {
                if (i.owned && i.texture != null) DestroyImmediate(i.texture);
                if (i.owned && i.arrayDepth != null && i.arrayDepth != i.texture) DestroyImmediate(i.arrayDepth);
                if (i.owned && i.encoded != null) DestroyImmediate(i.encoded);
                // A multisampled color's copy and a cube's faces' image are its own, whichever `texture` is now
                foreach (var own in new[] { i.mipped, i.faceColor })
                {
                    if (own == null || own == i.texture && i.owned) continue;
                    own.Release();
                    DestroyImmediate(own);
                }
            }
            images.Clear();
            if (srgbMipScratch != null) { srgbMipScratch.Release(); DestroyImmediate(srgbMipScratch); srgbMipScratch = null; }
            if (srgbMipMaterial != null) { DestroyImmediate(srgbMipMaterial); srgbMipMaterial = null; }
            if (srgbDecodeMaterial != null) { DestroyImmediate(srgbDecodeMaterial); srgbDecodeMaterial = null; }
            foreach (var t in targets.Values) Release(t);
            targets.Clear();
            foreach (var p in pipelines.Values) ForgetMaterials(p);
            pipelines.Clear();
            Collect(int.MaxValue);
            readEpoch++;
            fresh = true;
        }

        static void ReleaseBuffer(BufferObject b)
        {
            b.raw?.Release();
            foreach (var v in b.views) v?.Release();
        }

        // What was let go of up to `frame`: no camera's commands read it any more.
        void Collect(int frame)
        {
            // Each list is in the order things were let go of, so what's due is at its start
            var due = 0;
            var now = Time.frameCount;
            for (; due < doomed.Count && doomed[due].frame <= frame; due++)
            {
                var thing = doomed[due].thing;
                if (thing is RenderTexture rt)
                {
                    if (doomed[due].again && frame != int.MaxValue && rt != null && spareImages.Count < MaxSpareImages)
                    {
                        spareImages.Add((rt, now));
                        continue;
                    }
                    rt.Release();
                }
                if (thing != null) DestroyImmediate(thing);
            }
            if (due > 0) doomed.RemoveRange(0, due);
            due = 0;
            for (; due < doomedBuffers.Count && doomedBuffers[due].frame <= frame; due++)
            {
                var d = doomedBuffers[due];
                if (d.again && frame != int.MaxValue && spares.Count < MaxSpares) spares.Add((d.buffer, now));
                else d.buffer.Release();
            }
            if (due > 0) doomedBuffers.RemoveRange(0, due);
            if (frame == int.MaxValue || (now & 255) == 0)
            {
                for (var i = spares.Count - 1; i >= 0; i--)
                {
                    if (frame != int.MaxValue && now - spares[i].since < 600) continue;
                    spares[i].buffer.Release();
                    spares[i] = spares[spares.Count - 1];
                    spares.RemoveAt(spares.Count - 1);
                }
                for (var i = spareImages.Count - 1; i >= 0; i--)
                {
                    if (frame != int.MaxValue && now - spareImages[i].since < 600) continue;
                    var spare = spareImages[i].texture;
                    if (spare != null)
                    {
                        spare.Release();
                        DestroyImmediate(spare);
                    }
                    spareImages[i] = spareImages[spareImages.Count - 1];
                    spareImages.RemoveAt(spareImages.Count - 1);
                }
            }
        }

        // An image like `from` for a rename to copy it into: one a rename let go of, else a new one, or null where
        // none can be made.
        RenderTexture Renamed(RenderTexture from)
        {
            var like = from.descriptor;
            for (var i = spareImages.Count - 1; i >= 0; i--)
            {
                var spare = spareImages[i].texture;
                if (spare != null)
                {
                    // Compared a member at a time: a struct's own Equals boxes both
                    var its = spare.descriptor;
                    if (its.width != like.width || its.height != like.height || its.volumeDepth != like.volumeDepth
                        || its.msaaSamples != like.msaaSamples || its.dimension != like.dimension
                        || its.graphicsFormat != like.graphicsFormat || its.depthStencilFormat != like.depthStencilFormat
                        || its.mipCount != like.mipCount || its.flags != like.flags) continue;
                }
                spareImages[i] = spareImages[spareImages.Count - 1];
                spareImages.RemoveAt(spareImages.Count - 1);
                if (spare == null) continue; // Unity destroyed it
                if (spare.IsCreated() || spare.Create()) return spare;
                DestroyImmediate(spare);
            }
            RenderTexture next = null;
            try
            {
                next = new RenderTexture(like) { hideFlags = HideFlags.HideAndDontSave };
                if (next.Create()) return next;
                DestroyImmediate(next);
            }
            catch (OutOfMemoryException) { Doom(next); }
            catch (UnityException) { Doom(next); }
            return null;
        }

        // A buffer like `from` for a rename to copy it into: one a rename let go of, else a new one. `from` is let go.
        GraphicsBuffer Renamed(GraphicsBuffer from)
        {
            doomedBuffers.Add((from, Time.frameCount, true));
            for (var i = spares.Count - 1; i >= 0; i--)
            {
                var spare = spares[i].buffer;
                if (spare.count != from.count || spare.target != from.target) continue;
                spares[i] = spares[spares.Count - 1];
                spares.RemoveAt(spares.Count - 1);
                return spare;
            }
            return new GraphicsBuffer(from.target, from.count, 4);
        }

        void Doom(UnityEngine.Object thing)
        {
            if (thing != null) doomed.Add((thing, Time.frameCount, false));
        }

        void Doom(GraphicsBuffer buffer)
        {
            if (buffer != null) doomedBuffers.Add((buffer, Time.frameCount, false));
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
            foreach (var rt in new[] { t.depth, t.resolved }) // A face's color image is its cube's (Image.faceColor)
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
            stagedUsed = groupsUsed = 0; // Last frame's are drawn
            if (stream == null) return;
            Prepare(frame, stream, size); // With no camera too: its compute runs, and its reads begin
            if (camera == null) return;
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
            r.storage.Clear();
            r.linear = QualitySettings.activeColorSpace == ColorSpace.Linear;
            PrepareUniforms(r, stream, size);
            into = r;
            pipeline = null;
            inPass = inCompute = false;
            for (uint at = 0; at + 8 <= size;)
            {
                var op = (Op)Word(stream, at);
                var length = Word(stream, at + 4);
                at += 8;
                if (length > size - at) break;
                Run(op, stream + at, length);
                at += length;
            }
            if (inPass) End();
            inCompute = false;
            Flush();
            if (r.uniformBuffer != null && r.uniformBytes.IsCreated) r.uniformBuffer.SetData(r.uniformBytes);
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
            var had = r.uniformBuffer != null ? r.uniformBuffer.count : 0;
            if (had * 16 < bytes)
            {
                r.uniformBuffer?.Release(); // Last frame's draws are drawn
                r.uniformBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Constant, Math.Max(bytes / 16, Math.Max(256, had * 2)), 16);
            }
            // The buffer's whole size, which grows: a frame of more or fewer draws makes no new array
            if (!r.uniformBytes.IsCreated || r.uniformBytes.Length != r.uniformBuffer.count * 16)
            {
                if (r.uniformBytes.IsCreated) r.uniformBytes.Dispose();
                r.uniformBytes = new NativeArray<byte>(r.uniformBuffer.count * 16, Allocator.Persistent);
            }
            r.uniformUsed = 0;
        }

        void Run(Op op, byte* p, uint length)
        {
            uint W(int i) => *(uint*)(p + 4 * i);
            switch (op)
            {
                case Op.Buffer: MakeBuffer(W(0), (int)W(1), (int)W(2), W(3) != 0 ? p + 16 : null); break;
                case Op.BufferWrite:
                    if (buffers.TryGetValue(W(0), out var written)) Write(written, (int)W(1), p + 12, (int)W(2));
                    break;
                case Op.BufferFree:
                    if (buffers.TryGetValue(W(0), out var freed))
                    {
                        Doom(freed.raw);
                        foreach (var v in freed.views) Doom(v);
                        buffers.Remove(W(0));
                    }
                    break;
                case Op.Texture: MakeTexture(W(0), (int)W(1), (int)W(2), (int)W(3), W(4) != 0 ? p + 20 : null); break;
                case Op.TextureWrite:
                    Flush(); // After the work before it, which may sample it
                    WriteTexture(W(0), (int)W(1), (int)W(2), (int)W(3), (int)W(4), p + 20);
                    break;
                case Op.Mipmaps:
                    Flush();
                    Mipmaps(W(0));
                    break;
                case Op.TextureFree:
                    if (images.TryGetValue(W(0), out var image))
                    {
                        if (image.owned) Doom(image.texture);
                        if (image.owned) Doom(image.encoded);
                        Doom(image.faceColor);
                        if (image.arrayDepth != null && image.arrayDepth != image.texture) Doom(image.arrayDepth);
                        if (image.arrayDepthId != 0 && image.arrayDepthId != W(0)) images.Remove(image.arrayDepthId);
                        images.Remove(W(0));
                    }
                    break;
                case Op.Array: MakeArray(W(0), W(1), (int)W(2), (int)W(3), (int)W(4), W(5) != 0 ? p + 28 : null, W(6)); break;
                case Op.ArrayLayer: MakeArrayLayer(W(0), W(1), (int)W(2)); break;
                case Op.ArrayOf: MakeArrayOf(W(0), (int)W(1), (int)W(2), (int)W(3), (int)W(4)); break;
                case Op.TextureBlocks: MakeBlocks(W(0), (int)W(1), (int)W(2), 0, W(3), (int)W(4), (int)W(5), p + 24); break;
                case Op.ArrayBlocks: MakeBlocks(W(0), (int)W(1), (int)W(2), (int)W(3), W(4), (int)W(5), (int)W(6), p + 28); break;
                case Op.ArrayWrite:
                    Flush(); // After the work before it, which may sample it
                    WriteArray(W(0), (int)W(1), (int)W(2), (int)W(3), (int)W(4), (int)W(5), p + 24);
                    break;
                case Op.Volume: MakeVolume(W(0), (int)W(1), (int)W(2), (int)W(3), (int)W(4)); break;
                case Op.VolumeWrite:
                    WriteVolume(W(0), (int)W(1), (int)W(2), (int)W(3), (int)W(4), (int)W(5), (int)W(6), p + 28);
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
                        foreach (var id in target.imageIds)
                        {
                            if (images.TryGetValue(id, out var color)) Doom(color.mipped);
                            images.Remove(id);
                        }
                        if (target.colors != null) foreach (var c in target.colors) Doom(c);
                        Doom(target.depth);
                        Doom(target.resolved);
                        targets.Remove(W(0));
                    }
                    break;
                case Op.ResolveDepth: ResolveDepth(W(0), W(1)); break;
                case Op.Begin: Begin(W(0), W(1) != 0); break;
                case Op.End:
                    End();
                    break;
                case Op.Scissor:
                    into.commands.Add(new Command { kind = Kind.Scissor, x = (int)W(0), y = (int)W(1), width = (int)W(2), height = (int)W(3) });
                    break;
                case Op.Use:
                    pipelines.TryGetValue(W(0), out pipeline);
                    break;
                case Op.DepthBias: bias = BitConverter.Int32BitsToSingle((int)W(0)); slopeBias = BitConverter.Int32BitsToSingle((int)W(1)); break;
                case Op.Stencil: stencil = (int)W(0); break;
                case Op.Uniforms:
                {
                    var size = (int)W(0);
                    var align = Math.Max(16, SystemInfo.constantBufferOffsetAlignment);
                    uniformAt = into.uniformUsed;
                    uniformSize = size;
                    if (size > 0 && uniformAt + size <= into.uniformBytes.Length)
                    {
                        UnsafeUtility.MemCpy((byte*)into.uniformBytes.GetUnsafePtr() + uniformAt, p + 4, size);
                    }
                    into.uniformUsed += (size + align - 1) / align * align;
                    break;
                }
                case Op.Image:
                    if (W(0) < MaxTextures) sampled[W(0)] = W(1);
                    break;
                case Op.Sampler:
                    if (W(0) < MaxTextures) sampling[W(0)] = W(1) | W(2) << 2;
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
                case Op.Draw: Draw(W(0), (int)W(1), (int)W(2), false, null); break;
                case Op.DrawIndexed: Draw(0, (int)W(0), (int)W(1), true, null); break;
                case Op.PipelineStorage:
                    if (pipelines.TryGetValue(W(0), out var storing))
                    {
                        storing.compute = W(1) != 0;
                        storing.storageCount = (int)Math.Min(W(2), (uint)MaxStorage);
                        storing.storageWrites = W(3);
                        if (storing.compute)
                        {
                            storing.shader = null;
                            storing.kernelShader = Loaded<ComputeShader>(storing.key);
                        }
                    }
                    break;
                case Op.PipelineImages:
                    if (pipelines.TryGetValue(W(0), out var writing))
                    {
                        writing.imageWrites = W(1);
                        for (var i = 0; i < Math.Min((int)W(2), MaxTextures); i++) writing.imageFormats[i] = (int)W(3 + i);
                    }
                    break;
                case Op.Storage:
                    if (W(0) < MaxStorage) storageBound[W(0)] = W(1);
                    break;
                case Op.ComputeBegin: inCompute = true; break;
                case Op.ComputeEnd: inCompute = false; break;
                case Op.Dispatch: Dispatch(W(0), W(1), W(2), 0, 0, false); break;
                case Op.DispatchIndirect: Dispatch(0, 0, 0, W(0), W(1), true); break;
                case Op.DrawIndirect: DrawIndirect(W(0), W(1), false); break;
                case Op.DrawIndexedIndirect: DrawIndirect(W(0), W(1), true); break;
                case Op.BufferCopy:
                    if (buffers.TryGetValue(W(0), out var from) && buffers.TryGetValue(W(2), out var to) && from.raw != null && to.raw != null)
                    {
                        var source = from.raw; // Before `to` is renamed, should they be one
                        Copy(source, W(1), Written(to), W(3), W(4) / 4);
                    }
                    break;
                case Op.BufferRead: Read(W(0), W(1), W(2), W(3)); break;
                case Op.DrawIndexedIndirectCount: // Each of the records, a draw each: those past the count draw nothing
                    for (uint k = 0; k < W(2) && k < 65536u; k++) DrawIndirect(W(0), W(1) + k * 20u, true);
                    break;
            }
        }

        void MakeBuffer(uint id, int kind, int size, byte* data)
        {
            var indices = kind == 1;
            if (buffers.TryGetValue(id, out var had))
            {
                Doom(had.raw);
                foreach (var v in had.views) Doom(v);
            }
            var b = new BufferObject { indices = indices, storage = kind == 2 };
            b.raw = new GraphicsBuffer(GraphicsBuffer.Target.Raw | GraphicsBuffer.Target.CopySource | GraphicsBuffer.Target.CopyDestination,
                Math.Max(1, (size + 3) / 4), 4);
            if (indices) b.bytes = new byte[Math.Max(4, (size + 3) / 4 * 4)];
            buffers[id] = b;
            if (data != null) Write(b, 0, data, size);
            else if (b.storage && b.raw != null) b.raw.SetData(new uint[b.raw.count]); // A storage buffer starts as zeros
        }

        // Bytes into a buffer, now: the frame's writes come before the draws after them, and one a draw of this
        // frame already read is written in a new buffer, its bytes copied first, which the draws after read.
        void Write(BufferObject b, int offset, byte* data, int size)
        {
            Flush(); // After the work before it, which may read or write it
            if (b.used == Time.frameCount)
            {
                var renamed = Renamed(b.raw);
                Graphics.CopyBuffer(b.raw, renamed);
                b.raw = renamed;
                b.used = -1;
            }
            var count = (size + 3) / 4;
            var held = Words(count);
            if (size > 0) UnsafeUtility.MemCpy(held.GetUnsafePtr(), data, size);
            if (offset / 4 + count > b.raw.count) count = b.raw.count - offset / 4;
            if (count > 0) b.raw.SetData(held, 0, offset / 4, count);
            if (b.indices)
            {
                var room = Math.Max(0, Math.Min(size, b.bytes.Length - offset));
                if (room > 0) Marshal.Copy((IntPtr)data, b.bytes, offset, room);
                b.version++;
            }
        }

        // Room for `count` words that Unity takes as they are, kept from write to write.
        NativeArray<uint> Words(int count)
        {
            if (words.IsCreated && words.Length >= count) return words;
            var room = Math.Max(Math.Max(count, 1024), words.IsCreated ? words.Length * 2 : 0);
            if (words.IsCreated) words.Dispose();
            words = new NativeArray<uint>(room, Allocator.Persistent, NativeArrayOptions.UninitializedMemory);
            return words;
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
            var held = Words((b.bytes.Length + 3) / 4);
            fixed (byte* from = b.bytes) UnsafeUtility.MemCpy(held.GetUnsafePtr(), from, b.bytes.Length);
            if (wide) view.SetData(held, 0, 0, b.bytes.Length / 4);
            else view.SetData(held.Reinterpret<ushort>(4), 0, 0, b.bytes.Length / 2);
            b.viewVersions[k] = b.version;
            b.viewUsed[k] = Time.frameCount;
            return view;
        }

        // Unity replaces sRGB texture formats with UNorm in Gamma projects, even when a GraphicsFormat is asked for by
        // name. There a [Linear] image (`srgb`) keeps its bytes as they're stored, UNorm, and is read through a copy
        // decoded to linear light in half floats (Decoded, Decode), which is filtered, blended and has its smaller
        // images made in linear light, as an sRGB format's would be. The private sRGB attachments (color 10), which
        // the renderer's [Linear] copies write linear light into, are half floats there too (AttachmentFormat).
        static bool Gamma(bool srgb) => srgb && QualitySettings.activeColorSpace != ColorSpace.Linear;

        static readonly int DecodeSource2D = Shader.PropertyToID("_TideSrgbDecode2D");
        static readonly int DecodeSourceArray = Shader.PropertyToID("_TideSrgbDecodeArray");
        static readonly int DecodeInfo = Shader.PropertyToID("_TideSrgbDecodeInfo");

        // An image of linear light for `layers` of `width` by `height` (a cube's six faces), with `levels` levels
        // (0: a whole chain): what a Gamma project's [Linear] image is read as.
        static RenderTexture Decoded(int width, int height, int layers, TextureDimension dimension, int levels)
        {
            var whole = 1 + (int)Math.Floor(Math.Log(Math.Max(width, height), 2));
            var count = levels <= 0 ? whole : Math.Min(levels, whole);
            var rt = new RenderTexture(new RenderTextureDescriptor(width, height)
            {
                graphicsFormat = GraphicsFormat.R16G16B16A16_SFloat, depthStencilFormat = GraphicsFormat.None, dimension = dimension,
                volumeDepth = dimension == TextureDimension.Tex2DArray ? Math.Max(1, layers) : 1, msaaSamples = 1,
                useMipMap = count > 1, autoGenerateMips = false, mipCount = count,
            }) { hideFlags = HideFlags.HideAndDontSave };
            rt.Create();
            return rt;
        }

        // ...decoded from `encoded` (a Texture2D, or a Texture2DArray of its layers or faces) in the work's order: each
        // of `layers` (all when -1, else the one) at each of its first `levels` levels, each texel loaded as stored and
        // its color, not its alpha, turned from sRGB to linear light.
        void Decode(Texture encoded, RenderTexture into, int layer, int levels)
        {
            if (encoded == null || into == null) return;
            if (srgbDecodeMaterial == null)
            {
                var shader = Resources.Load<Shader>("TideSrgbDecode");
                if (shader == null) return;
                srgbDecodeMaterial = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
            }
            var cube = into.dimension == TextureDimension.Cube;
            var array = encoded.dimension == TextureDimension.Tex2DArray;
            var count = cube ? 6 : into.dimension == TextureDimension.Tex2DArray ? into.volumeDepth : 1;
            var cb = Work();
            cb.SetGlobalTexture(array ? DecodeSourceArray : DecodeSource2D, encoded);
            cb.DisableScissorRect();
            cb.SetGlobalDepthBias(0, 0);
            for (var level = 0; level < Math.Min(levels, into.mipmapCount); level++)
            {
                var width = Math.Max(1, into.width >> level);
                var height = Math.Max(1, into.height >> level);
                for (var i = layer < 0 ? 0 : layer; i < (layer < 0 ? count : layer + 1); i++)
                {
                    cb.SetRenderTarget(into, level, cube ? (CubemapFace)i : CubemapFace.Unknown, cube ? 0 : i);
                    cb.SetViewport(new Rect(0, 0, width, height));
                    cb.SetGlobalVector(DecodeInfo, new Vector4(level, i, 0, 0));
                    cb.DrawProcedural(Matrix4x4.identity, srgbDecodeMaterial, array ? 1 : 0, MeshTopology.Triangles, 3);
                }
            }
        }

        // A Gamma project's [Linear] image read now as a new one where a camera's draw of this frame read it as it was.
        RenderTexture DecodedAgain(Image image)
        {
            var rt = image.texture as RenderTexture;
            if (rt == null || image.used != Time.frameCount) return rt;
            var next = Renamed(rt);
            if (next == null) return null;
            doomed.Add((rt, Time.frameCount, true));
            image.texture = next;
            image.used = -1;
            image.own = 15;
            return next;
        }

        static GraphicsFormat TextureFormat(int format) => format switch
        {
            1 => GraphicsFormat.R8_UNorm,
            2 => GraphicsFormat.R16_SFloat,
            3 => GraphicsFormat.R32G32B32A32_SInt,
            4 => GraphicsFormat.R16G16B16A16_SFloat,
            5 => GraphicsFormat.R32_SFloat,
            6 => GraphicsFormat.R32G32B32A32_SFloat,
            7 => GraphicsFormat.R32_SInt,
            8 => GraphicsFormat.R8G8B8A8_SRGB,
            9 => GraphicsFormat.R16G16_SFloat, // A Grid2<float2>'s cells (tide_gpu_format's TIDE_GPU_RG16F)
            _ => GraphicsFormat.R8G8B8A8_UNorm,
        };

        static int PixelBytes(int format) => format == 1 ? 1 : format == 2 ? 2 : format == 4 ? 8 : format == 3 || format == 6 ? 16 : 4;

        static bool IntegerImage(int format) => format == 3 || format == 7;

        static bool WritableImage(int format) => format == 0 || format >= 4 && format <= 7;

        static int AttachmentPixelFormat(int color) => color switch
        {
            1 => 0, 2 => 4, 3 => 1, 5 => 2, 7 => 5, 8 => 6, 9 => 7, _ => -1,
        };

        void VolumeUnavailable()
        {
            if (volumeSaid) return;
            volumeSaid = true;
            Debug.LogWarning("Tide: a volume exceeds this GPU's limits or memory, or its format isn't supported: it reads zero and writes nothing. Use a smaller Texture3D or a supported format.");
        }

        // Volumes use GPU box uploads where the device can store their format. This includes half-float grids on
        // desktop Unity; a device without that internal store path keeps a CPU mirror. Public kernel bindings still
        // accept only WritableImage's portable formats. All are actual 3D images, without mipmaps.
        void MakeVolume(uint id, int format, int width, int height, int depth)
        {
            if (width <= 0 || height <= 0 || depth <= 0 || width > SystemInfo.maxTexture3DSize || height > SystemInfo.maxTexture3DSize
                || depth > SystemInfo.maxTexture3DSize || format < 0 || format > 7
                || (long)width * height * depth > 128L * 1024 * 1024 / PixelBytes(format) || !SystemInfo.supports3DTextures
                || (!IntegerImage(format) && !SystemInfo.IsFormatSupported(TextureFormat(format), GraphicsFormatUsage.Sample))
                || ((WritableImage(format) || IntegerImage(format)) // Unity has no integer Texture3D: a render texture's (NewIntegers)
                    && !SystemInfo.IsFormatSupported(TextureFormat(format), GraphicsFormatUsage.LoadStore)))
            {
                VolumeUnavailable();
                images[id] = new Image();
                return;
            }
            var image = new Image { format = format, pixelBytes = PixelBytes(format), width = width, height = height, depth = depth, owned = true };
            try
            {
                if (SystemInfo.IsFormatSupported(TextureFormat(format), GraphicsFormatUsage.LoadStore))
                {
                    var rt = new RenderTexture(new RenderTextureDescriptor(width, height)
                    {
                        graphicsFormat = TextureFormat(format), depthStencilFormat = GraphicsFormat.None,
                        dimension = TextureDimension.Tex3D, volumeDepth = depth, msaaSamples = 1,
                        enableRandomWrite = true, useMipMap = false, autoGenerateMips = false,
                    }) { hideFlags = HideFlags.HideAndDontSave };
                    image.texture = rt;
                    if (!rt.Create())
                    {
                        DestroyImmediate(rt);
                        VolumeUnavailable();
                        images[id] = new Image();
                        return;
                    }
                    var kernel = copier != null ? copier.FindKernel(IntegerImage(format) ? "TideVolumeClearInt" : "TideVolumeClear") : -1;
                    if (kernel < 0)
                    {
                        DestroyImmediate(rt);
                        VolumeUnavailable();
                        images[id] = new Image();
                        return;
                    }
                    var cb = Work();
                    cb.SetComputeTextureParam(copier, kernel, IntegerImage(format) ? "_TideVolumeToInt" : "_TideVolumeTo", rt);
                    cb.DispatchCompute(copier, kernel, (width + 7) / 8, (height + 7) / 8, (depth + 3) / 4);
                }
                else
                {
                    image.pixels = new byte[width * height * depth * image.pixelBytes];
                    var texture = new Texture3D(width, height, depth, TextureFormat(format), TextureCreationFlags.None)
                    { hideFlags = HideFlags.HideAndDontSave };
                    image.texture = texture;
                    texture.SetPixelData(image.pixels, 0);
                    texture.Apply(false, false);
                }
            }
            catch (OutOfMemoryException) { VolumeUnavailable(); Doom(image.texture); image = new Image(); }
            catch (UnityException) { VolumeUnavailable(); Doom(image.texture); image = new Image(); }
            if (images.TryGetValue(id, out var had) && had.owned) Doom(had.texture);
            images[id] = image;
        }

        void WriteVolume(uint id, int x, int y, int z, int width, int height, int depth, byte* pixels)
        {
            try { WriteVolumePixels(id, x, y, z, width, height, depth, pixels); }
            catch (OutOfMemoryException) { VolumeUnavailable(); }
            catch (UnityException) { VolumeUnavailable(); }
        }

        void WriteVolumePixels(uint id, int x, int y, int z, int width, int height, int depth, byte* pixels)
        {
            if (!images.TryGetValue(id, out var image) || image.texture == null || x < 0 || y < 0 || z < 0 || width <= 0 || height <= 0
                || depth <= 0 || x > image.width || y > image.height || z > image.depth || width > image.width - x
                || height > image.height - y || depth > image.depth - z) return;
            var bytes = width * height * depth * image.pixelBytes;
            if (image.texture is RenderTexture)
            {
                var destination = Written(image);
                if (destination == null || copier == null) return;
                if (IntegerImage(image.format))
                {
                    IntegersInto(destination, x, y, z, width, height, depth, image.pixelBytes, pixels);
                    return;
                }
                var uploaded = new byte[bytes];
                Marshal.Copy((IntPtr)pixels, uploaded, 0, bytes);
                var upload = new Texture3D(width, height, depth, TextureFormat(image.format), TextureCreationFlags.None)
                { hideFlags = HideFlags.HideAndDontSave };
                Doom(upload); // Also released if an upload or binding fails below
                upload.SetPixelData(uploaded, 0);
                upload.Apply(false, true);
                var integer = IntegerImage(image.format);
                var kernel = copier.FindKernel(integer ? "TideVolumeCopyInt" : "TideVolumeCopy");
                var cb = Work();
                cb.SetComputeTextureParam(copier, kernel, integer ? "_TideVolumeFromInt" : "_TideVolumeFrom", upload);
                cb.SetComputeTextureParam(copier, kernel, integer ? "_TideVolumeToInt" : "_TideVolumeTo", destination);
                cb.SetComputeIntParams(copier, "_TideVolumeOrigin", x, y, z, 0);
                cb.DispatchCompute(copier, kernel, (width + 7) / 8, (height + 7) / 8, (depth + 3) / 4);
                return;
            }
            Flush();
            var row = width * image.pixelBytes;
            for (var slice = 0; slice < depth; slice++)
                for (var r = 0; r < height; r++)
                    Marshal.Copy((IntPtr)(pixels + (slice * height + r) * row), image.pixels,
                        (((z + slice) * image.height + y + r) * image.width + x) * image.pixelBytes, row);
            var texture = image.texture as Texture3D;
            if (image.used == Time.frameCount)
            {
                Doom(texture);
                texture = new Texture3D(image.width, image.height, image.depth, TextureFormat(image.format), TextureCreationFlags.None)
                { hideFlags = HideFlags.HideAndDontSave };
                image.texture = texture;
                image.used = -1;
            }
            texture.SetPixelData(image.pixels, 0);
            texture.Apply(false, false);
        }

        static Texture2D NewTexture(int format, int width, int height)
        {
            // Integer images (a list's numbers, a Grid2<int>'s cells) are read at their nearest pixel: no smaller images
            var flags = IntegerImage(format) ? TextureCreationFlags.None : TextureCreationFlags.MipChain;
            return new Texture2D(Math.Max(1, width), Math.Max(1, height), TextureFormat(format), flags) { hideFlags = HideFlags.HideAndDontSave };
        }

        // Unity makes no Texture2D, Texture2DArray or Texture3D of an integer format (it refuses R32_SInt and
        // R32G32B32A32_SInt for Sample usage), so an integer image is a render texture a kernel copies its pixels into
        // (IntegersInto), loaded at its nearest pixel as an attachment of R32i is. Null where it can't be made.
        static RenderTexture NewIntegers(int format, int width, int height, int layers, TextureDimension dimension)
        {
            RenderTexture rt = null;
            try
            {
                rt = new RenderTexture(new RenderTextureDescriptor(Math.Max(1, width), Math.Max(1, height))
                {
                    graphicsFormat = TextureFormat(format), depthStencilFormat = GraphicsFormat.None, dimension = dimension,
                    volumeDepth = Math.Max(1, layers), msaaSamples = 1, enableRandomWrite = true, useMipMap = false,
                    autoGenerateMips = false,
                }) { hideFlags = HideFlags.HideAndDontSave };
                if (rt.Create()) return rt;
            }
            catch (OutOfMemoryException) { }
            catch (UnityException) { }
            if (rt != null) DestroyImmediate(rt);
            return null;
        }

        // A box of an integer image's pixels (`pixelBytes` 4 or 16 each, rows from the top, then layers or slices),
        // copied into it on the GPU, in order with the frame's other work
        void IntegersInto(RenderTexture to, int x, int y, int z, int width, int height, int depth, int pixelBytes, byte* pixels)
        {
            if (to == null || copier == null || width <= 0 || height <= 0 || depth <= 0) return;
            var kernel = copier.FindKernel(to.dimension == TextureDimension.Tex3D ? "TideIntsTo3D"
                : to.dimension == TextureDimension.Tex2DArray ? "TideIntsToArray" : "TideIntsTo2D");
            var count = width * height * depth * pixelBytes / 4;
            var from = new GraphicsBuffer(GraphicsBuffer.Target.Raw, Math.Max(1, count), 4);
            Doom(from); // Kept until the work reading it is done
            var held = Words(count);
            UnsafeUtility.MemCpy(held.GetUnsafePtr(), pixels, (long)count * 4);
            from.SetData(held, 0, 0, count);
            var cb = Work();
            cb.SetComputeBufferParam(copier, kernel, "_TideIntsFrom", from);
            cb.SetComputeTextureParam(copier, kernel, to.dimension == TextureDimension.Tex3D ? "_TideVolumeToInt"
                : to.dimension == TextureDimension.Tex2DArray ? "_TideIntsToArray" : "_TideIntsTo2D", to);
            cb.SetComputeIntParams(copier, "_TideIntsBox", x, y, z, pixelBytes / 4);
            cb.SetComputeIntParams(copier, "_TideIntsSize", width, height, depth, 0);
            cb.DispatchCompute(copier, kernel, (width + 7) / 8, (height + 7) / 8, (depth + 3) / 4);
        }

        void IntegersInto(RenderTexture to, int x, int y, int z, int width, int height, int depth, int pixelBytes, byte[] pixels)
        {
            fixed (byte* p = pixels) IntegersInto(to, x, y, z, width, height, depth, pixelBytes, p);
        }

        void MakeTexture(uint id, int format, int width, int height, byte* pixels)
        {
            if (IntegerImage(format))
            {
                var size = PixelBytes(format);
                var integers = new Image
                {
                    texture = NewIntegers(format, width, height, 1, TextureDimension.Tex2D), format = format, pixelBytes = size,
                    width = width, height = height, owned = true,
                };
                integers.pixels = new byte[Math.Max(1, width * height) * size];
                if (pixels != null) Marshal.Copy((IntPtr)pixels, integers.pixels, 0, width * height * size);
                IntegersInto((RenderTexture)integers.texture, 0, 0, 0, Math.Max(1, width), Math.Max(1, height), 1, size, integers.pixels);
                if (images.TryGetValue(id, out var was) && was.owned) { Doom(was.texture); Doom(was.encoded); }
                images[id] = integers;
                return;
            }
            var gamma = Gamma(format == 8);
            var texture = gamma ? new Texture2D(Math.Max(1, width), Math.Max(1, height), GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.None)
                { hideFlags = HideFlags.HideAndDontSave } : NewTexture(format, width, height);
            var bytes = PixelBytes(format);
            var image = new Image { texture = texture, format = format, pixelBytes = bytes, width = width, height = height, owned = true };
            image.pixels = new byte[Math.Max(1, width * height * bytes)];
            if (pixels != null) Marshal.Copy((IntPtr)pixels, image.pixels, 0, width * height * bytes);
            texture.SetPixelData(image.pixels, 0);
            texture.Apply(false, false);
            if (gamma)
            {
                image.encoded = texture;
                image.texture = Decoded(Math.Max(1, width), Math.Max(1, height), 1, TextureDimension.Tex2D, 0);
                Decode(texture, (RenderTexture)image.texture, -1, 1);
            }
            if (images.TryGetValue(id, out var had) && had.owned) { Doom(had.texture); Doom(had.encoded); }
            images[id] = image;
        }

        void WriteTexture(uint id, int x, int y, int width, int height, byte* pixels)
        {
            if (!images.TryGetValue(id, out var image) || image.pixels == null) return;
            var row = width * image.pixelBytes;
            if (image.encoded is Texture2D encoded)
            {
                for (var r = 0; r < height; r++)
                    Marshal.Copy((IntPtr)(pixels + r * row), image.pixels, ((y + r) * image.width + x) * image.pixelBytes, row);
                encoded.SetPixelData(image.pixels, 0);
                encoded.Apply(false, false);
                var into = DecodedAgain(image);
                Decode(encoded, into, -1, 1);
                if (image.mips && into != null) Work().GenerateMips(into);
                return;
            }
            if (IntegerImage(image.format))
            {
                if (x < 0 || y < 0 || width <= 0 || height <= 0 || width > image.width - x || height > image.height - y) return;
                for (var r = 0; r < height; r++)
                    Marshal.Copy((IntPtr)(pixels + r * row), image.pixels, ((y + r) * image.width + x) * image.pixelBytes, row);
                IntegersInto(Written(image), x, y, 0, width, height, 1, image.pixelBytes, pixels);
                return;
            }
            if (!(image.texture is Texture2D texture)) return;
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
                texture.Apply(image.mips && image.format != 8, false);
                if (image.mips && image.format == 8) SrgbMipmaps(image);
                return;
            }
            texture.SetPixelData(image.pixels, 0);
            texture.Apply(false, false); // Its smaller images stay as they were made
        }

        // Unity's Apply(true) and GenerateMips average encoded bytes. Draw each smaller level through the
        // image's sRGB sampling view into a separate sRGB attachment, then copy its encoded bytes back.
        void SrgbMipmaps(Image image)
        {
            var texture = image.texture;
            if (texture == null || texture.mipmapCount < 2) return;
            if (srgbMipMaterial == null)
                srgbMipMaterial = new Material(Resources.Load<Shader>("TideSrgbMip")) { hideFlags = HideFlags.HideAndDontSave };
            if (srgbMipScratch == null || srgbMipScratch.width != texture.width || srgbMipScratch.height != texture.height)
            {
                Doom(srgbMipScratch);
                srgbMipScratch = new RenderTexture(new RenderTextureDescriptor(texture.width, texture.height)
                {
                    graphicsFormat = GraphicsFormat.R8G8B8A8_SRGB, depthStencilFormat = GraphicsFormat.None,
                    useMipMap = true, autoGenerateMips = false, mipCount = texture.mipmapCount, msaaSamples = 1,
                }) { hideFlags = HideFlags.HideAndDontSave };
                srgbMipScratch.Create();
            }
            var cube = texture.dimension == TextureDimension.Cube;
            var array = texture.dimension == TextureDimension.Tex2DArray;
            var layers = cube ? 6 : texture is Texture2DArray pixels ? pixels.depth : array && texture is RenderTexture rt ? rt.volumeDepth : 1;
            var cb = Work();
            cb.SetGlobalTexture(cube ? SrgbSourceCube : array ? SrgbSourceArray : SrgbSource2D, texture);
            cb.DisableScissorRect();
            cb.SetGlobalDepthBias(0, 0);
            var width = texture.width;
            var height = texture.height;
            for (var level = 1; level < texture.mipmapCount; level++)
            {
                width = Math.Max(1, width / 2);
                height = Math.Max(1, height / 2);
                for (var layer = 0; layer < layers; layer++)
                {
                    cb.SetRenderTarget(srgbMipScratch, level);
                    cb.SetViewport(new Rect(0, 0, width, height));
                    cb.SetGlobalVector(SrgbMipInfo, new Vector4(width, height, level - 1, layer));
                    cb.DrawProcedural(Matrix4x4.identity, srgbMipMaterial, cube ? 2 : array ? 1 : 0, MeshTopology.Triangles, 3);
                    cb.CopyTexture(srgbMipScratch, 0, level, texture, layer, level);
                }
            }
        }

        void Mipmaps(uint id)
        {
            if (!images.TryGetValue(id, out var image) || image.texture == null || image.authored) return;
            if (image.drawn != null)
            {
                MultisampledMipmaps(image);
                return;
            }
            if (image.texture.graphicsFormat == GraphicsFormat.R8G8B8A8_SRGB)
                SrgbMipmaps(image);
            else if (image.texture is Texture2D texture && texture.mipmapCount > 1) texture.Apply(true, false);
            else if (image.texture is Texture2DArray array && array.mipmapCount > 1) array.Apply(true, false);
            else if (image.texture is Cubemap cube) cube.Apply(true, false);
            else if (image.texture is RenderTexture rt && rt.useMipMap) Work().GenerateMips(rt);
            else return;
            image.mips = true;
        }

        // A multisampled color's smaller images: what its samples resolve to, in a one-sample copy of its own with a
        // chain (an MSAA image has none), which draws and kernels read from here until the target is drawn into again
        // (Begin, Drawn). A copy a camera's draw of this frame read stays as it was: the next is one a rename let go of.
        void MultisampledMipmaps(Image image)
        {
            var drawn = image.drawn;
            var mipped = image.mipped;
            if (image.texture == mipped) Shown(image, drawn);
            if (mipped != null && image.mippedUsed == Time.frameCount)
            {
                var next = Renamed(mipped);
                if (next == null) return;
                doomed.Add((mipped, Time.frameCount, true));
                image.mipped = mipped = next;
                image.mippedUsed = -1;
            }
            if (mipped == null)
            {
                try
                {
                    mipped = new RenderTexture(new RenderTextureDescriptor(drawn.width, drawn.height)
                    {
                        graphicsFormat = drawn.graphicsFormat, depthStencilFormat = GraphicsFormat.None, msaaSamples = 1,
                        useMipMap = true, autoGenerateMips = false,
                    }) { hideFlags = HideFlags.HideAndDontSave };
                    if (!mipped.Create()) { DestroyImmediate(mipped); return; }
                }
                catch (UnityException) { Doom(mipped); return; }
                image.mipped = mipped;
            }
            var cb = Work();
            cb.SetRenderTarget(drawn); // OpenGL Core's Unity resolves only the target it has bound
            cb.ResolveAntiAliasedSurface(drawn, mipped);
            cb.GenerateMips(mipped);
            Shown(image, mipped);
            image.mips = true;
        }

        // Which of a multisampled color's two images draws read from here, each keeping when a camera's draw read it.
        static void Shown(Image image, RenderTexture texture)
        {
            if (image.texture == texture) return;
            if (image.texture == image.drawn)
            {
                image.drawnUsed = image.used;
                image.used = image.mippedUsed;
            }
            else
            {
                image.mippedUsed = image.used;
                image.used = image.drawnUsed;
            }
            image.texture = texture;
            image.own = 15;
        }

        // tide_gpu_color, recorded verbatim in TARGET/ARRAY/CUBE commands.
        static GraphicsFormat AttachmentFormat(int color)
        {
            switch (color)
            {
                case 2: return GraphicsFormat.R16G16B16A16_SFloat;
                case 3: return GraphicsFormat.R8_UNorm;
                case 4: return GraphicsFormat.R8G8_UNorm;
                case 5: return GraphicsFormat.R16_SFloat;
                case 6: return GraphicsFormat.R16G16_SFloat;
                case 7: return GraphicsFormat.R32_SFloat;
                case 8: return GraphicsFormat.R32G32B32A32_SFloat;
                case 9: return GraphicsFormat.R32_SInt;
                // The renderer's own [Linear] copies: linear light, stored sRGB, or as it is in a Gamma project (Gamma)
                case 10: return Gamma(true) ? GraphicsFormat.R16G16B16A16_SFloat : GraphicsFormat.R8G8B8A8_SRGB;
                default: return GraphicsFormat.R8G8B8A8_UNorm;
            }
        }

        // The colors an image of which is blended between its pixels, and so has smaller images made (gpu.h's
        // tide_gpu_color_single_sample are the others: 32-bit numbers, loaded as they are)
        static bool Blended(int color) => color >= 1 && color <= 6 || color == 10;

        void MakeCube(uint id, int color, int size, byte* faces)
        {
            size = Math.Max(1, size);
            if (faces != null && Gamma(color == 10))
            {
                // Its faces as stored, six layers of bytes, decoded to linear light into a cube of half floats
                var stored = new Texture2DArray(size, size, 6, GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.None)
                    { hideFlags = HideFlags.HideAndDontSave };
                var each = size * size * 4;
                var held = new byte[each];
                for (var i = 0; i < 6; i++)
                {
                    Marshal.Copy((IntPtr)(faces + i * each), held, 0, each);
                    stored.SetPixelData(held, 0, i);
                }
                stored.Apply(false, false);
                var decoded = Decoded(size, size, 6, TextureDimension.Cube, 0);
                Decode(stored, decoded, -1, 1);
                images[id] = new Image { texture = decoded, encoded = stored, width = size, height = size, owned = true };
                return;
            }
            if (faces != null)
            {
                var cube = new Cubemap(size, AttachmentFormat(color), TextureCreationFlags.MipChain) { hideFlags = HideFlags.HideAndDontSave };
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
            var rt = new RenderTexture(size, size, AttachmentFormat(color), GraphicsFormat.None)
            {
                dimension = TextureDimension.Cube, useMipMap = Blended(color), autoGenerateMips = false, hideFlags = HideFlags.HideAndDontSave,
            };
            rt.Create();
            images[id] = new Image { texture = rt, width = size, height = size, owned = true };
        }

        // An array: of pixels (a Texture2DArray, its smaller images made), or drawn into a layer at a time (a RenderTexture of
        // layers, with room for its smaller images where its color is blended, and a depth one beside it, read as
        // `depthId`; one of depth alone is that). A cube of depth alone or of 32-bit numbers is such an array of six.
        void MakeArray(uint id, uint format, int width, int height, int layers, byte* pixels, uint depthId)
        {
            width = Math.Max(1, width);
            height = Math.Max(1, height);
            layers = Math.Max(1, layers);
            if (pixels != null && Gamma((format & 15) == 10))
            {
                var stored = new Texture2DArray(width, height, layers, GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.None)
                    { hideFlags = HideFlags.HideAndDontSave };
                var each = width * height * 4;
                var held = new byte[each];
                for (var i = 0; i < layers; i++)
                {
                    Marshal.Copy((IntPtr)(pixels + (long)i * each), held, 0, each);
                    stored.SetPixelData(held, 0, i);
                }
                stored.Apply(false, false);
                var decoded = Decoded(width, height, layers, TextureDimension.Tex2DArray, 0);
                Decode(stored, decoded, -1, 1);
                if (decoded.useMipMap) Work().GenerateMips(decoded);
                images[id] = new Image { texture = decoded, encoded = stored, width = width, height = height, owned = true, mips = true };
                return;
            }
            if (pixels != null)
            {
                var array = new Texture2DArray(width, height, layers, AttachmentFormat((int)(format & 15)), TextureCreationFlags.MipChain)
                {
                    hideFlags = HideFlags.HideAndDontSave,
                };
                var layer = width * height * 4;
                var bytes = new byte[layer];
                for (var i = 0; i < layers; i++)
                {
                    Marshal.Copy((IntPtr)(pixels + (long)i * layer), bytes, 0, layer);
                    array.SetPixelData(bytes, 0, i);
                }
                array.Apply((format & 15) != 10, false);
                var image = new Image { texture = array, width = width, height = height, owned = true, mips = true };
                images[id] = image;
                if ((format & 15) == 10) SrgbMipmaps(image);
                return;
            }
            var depth = new RenderTexture(width, height, GraphicsFormat.None, DepthFormat())
            {
                dimension = TextureDimension.Tex2DArray, volumeDepth = layers, hideFlags = HideFlags.HideAndDontSave,
            };
            depth.Create();
            if ((format & 15) == 0) // Depth alone: the array is its depth
            {
                // ...read by the array's own id, and by its depth array's (gpu.h's array_depth), which is the id a
                // draw or a kernel that reads its depth gives: one of its own here too, as the recorder numbers it
                images[id] = new Image { texture = depth, element = RenderTextureSubElement.Depth, width = width, height = height,
                                         owned = true, arrayDepth = depth, arrayDepthId = depthId != 0 ? depthId : id };
                if (depthId != 0 && depthId != id)
                    images[depthId] = new Image { texture = depth, element = RenderTextureSubElement.Depth, width = width, height = height };
                return;
            }
            var color = new RenderTexture(width, height, AttachmentFormat((int)(format & 15)), GraphicsFormat.None)
            {
                dimension = TextureDimension.Tex2DArray, volumeDepth = layers, useMipMap = Blended((int)(format & 15)), autoGenerateMips = false,
                hideFlags = HideFlags.HideAndDontSave,
            };
            color.Create();
            images[id] = new Image { texture = color, width = width, height = height, owned = true, arrayDepth = depth, arrayDepthId = depthId };
            images[depthId] = new Image { texture = depth, element = RenderTextureSubElement.Depth, width = width, height = height };
        }

        // A pack's image of blocks, every level given (TEXTURE_BLOCKS), or an array of them, `layers` chains `bytes` apart
        // (ARRAY_BLOCKS, `layers` 0 for a texture): BC1, BC3 or RGBA8, as tide/texture_blocks.h lays them out. Never written
        // again, so Unity keeps no copy of them.
        void MakeBlocks(uint id, int width, int height, int layers, uint format, int levels, int bytes, byte* blocks)
        {
            var gamma = Gamma((format & 0x100u) != 0);
            var srgb = (format & 0x100u) != 0 && !gamma; // A Gamma project's are stored UNorm, and decoded below
            var baseFormat = format & ~0x100u;
            var graphics = baseFormat == 1 ? (srgb ? GraphicsFormat.RGBA_DXT1_SRGB : GraphicsFormat.RGBA_DXT1_UNorm)
                         : baseFormat == 3 ? (srgb ? GraphicsFormat.RGBA_DXT5_SRGB : GraphicsFormat.RGBA_DXT5_UNorm)
                         : srgb ? GraphicsFormat.R8G8B8A8_SRGB : GraphicsFormat.R8G8B8A8_UNorm;
            width = Math.Max(1, width);
            height = Math.Max(1, height);
            levels = Math.Max(1, levels);
            var flags = levels > 1 ? TextureCreationFlags.MipChain : TextureCreationFlags.None;
            var data = new byte[Math.Max(1, bytes)];
            Texture made;
            if (layers == 0)
            {
                var texture = new Texture2D(width, height, graphics, levels, flags) { hideFlags = HideFlags.HideAndDontSave };
                Marshal.Copy((IntPtr)blocks, data, 0, bytes);
                Levels(width, height, format, levels, (level, offset) => texture.SetPixelData(data, level, offset));
                texture.Apply(false, true);
                made = texture;
            }
            else
            {
                var array = new Texture2DArray(width, height, layers, graphics, flags, levels) { hideFlags = HideFlags.HideAndDontSave };
                for (var layer = 0; layer < layers; layer++)
                {
                    Marshal.Copy((IntPtr)(blocks + (long)layer * bytes), data, 0, bytes);
                    var each = layer;
                    Levels(width, height, format, levels, (level, offset) => array.SetPixelData(data, level, each, offset));
                }
                array.Apply(false, true);
                made = array;
            }
            if (images.TryGetValue(id, out var had) && had.owned) { Doom(had.texture); Doom(had.encoded); }
            var image = new Image { texture = made, width = width, height = height, owned = true, mips = levels > 1, authored = true };
            if (gamma)
            {
                // Each level given, decoded: its smaller images stay the pack's own
                var decoded = Decoded(width, height, Math.Max(1, layers), layers == 0 ? TextureDimension.Tex2D : TextureDimension.Tex2DArray, levels);
                Decode(made, decoded, -1, levels);
                image.encoded = made;
                image.texture = decoded;
                image.levels = levels;
            }
            images[id] = image;
        }

        // Each level's place in a chain of tide/texture_blocks.h's: its bytes from the chain's start
        static void Levels(int width, int height, uint format, int levels, Action<int, int> level)
        {
            format &= ~0x100u; // sRGB changes sampling, never the block or level byte layout.
            var offset = 0;
            for (var i = 0; i < levels; i++)
            {
                level(i, offset);
                offset += format == 4 ? width * height * 4 : (width + 3) / 4 * ((height + 3) / 4) * (format == 1 ? 8 : 16);
                width = Math.Max(1, width / 2);
                height = Math.Max(1, height / 2);
            }
        }

        // An array of a format, zeros until written a part at a time (a Grid3's): its pixels kept here, each write a
        // layer's. Its smaller images are made where it's blended: never for 32-bit numbers (R32F, RGBA32F, R32I),
        // which are loaded as they are (gpu.h's array_of; the renderer's zeros for 32-bit layers are such arrays). A
        // Gamma project's [Linear] array keeps its bytes UNorm here, read through `texture` decoded (Gamma).
        static Texture2DArray NewArray(int format, int width, int height, int layers) =>
            new Texture2DArray(Math.Max(1, width), Math.Max(1, height), Math.Max(1, layers), Gamma(format == 8) ? GraphicsFormat.R8G8B8A8_UNorm
                : TextureFormat(format), format >= 5 && format <= 7 || Gamma(format == 8) ? TextureCreationFlags.None : TextureCreationFlags.MipChain)
            {
                hideFlags = HideFlags.HideAndDontSave,
            };

        void MakeArrayOf(uint id, int format, int width, int height, int layers)
        {
            var bytes = PixelBytes(format);
            if (IntegerImage(format))
            {
                var integers = new Image
                {
                    texture = NewIntegers(format, width, height, layers, TextureDimension.Tex2DArray), format = format,
                    pixelBytes = bytes, width = width, height = height, layers = Math.Max(1, layers), owned = true,
                };
                integers.pixels = new byte[Math.Max(1, width * height) * bytes * integers.layers];
                IntegersInto((RenderTexture)integers.texture, 0, 0, 0, Math.Max(1, width), Math.Max(1, height), integers.layers,
                    bytes, integers.pixels); // Zeros until written, as gpu.h's array_of promises
                if (images.TryGetValue(id, out var was) && was.owned) { Doom(was.texture); Doom(was.encoded); }
                images[id] = integers;
                return;
            }
            var image = new Image
            {
                texture = NewArray(format, width, height, layers), format = format, pixelBytes = bytes, width = width, height = height,
                layers = Math.Max(1, layers), owned = true,
            };
            image.pixels = new byte[Math.Max(1, width * height * bytes * image.layers)];
            var array = (Texture2DArray)image.texture;
            for (var i = 0; i < image.layers; i++) array.SetPixelData(image.pixels, 0, i, i * width * height * bytes);
            array.Apply(false, false);
            if (Gamma(format == 8))
            {
                image.encoded = array;
                image.texture = Decoded(Math.Max(1, width), Math.Max(1, height), image.layers, TextureDimension.Tex2DArray, 0);
                Decode(array, (RenderTexture)image.texture, -1, 1);
            }
            if (images.TryGetValue(id, out var had) && had.owned) { Doom(had.texture); Doom(had.encoded); }
            images[id] = image;
        }

        void WriteArray(uint id, int layer, int x, int y, int width, int height, byte* pixels)
        {
            if (!images.TryGetValue(id, out var image) || image.pixels == null || layer < 0 || layer >= image.layers) return;
            var row = width * image.pixelBytes;
            var each = image.width * image.height * image.pixelBytes;
            if (image.encoded is Texture2DArray stored)
            {
                for (var r = 0; r < height; r++)
                    Marshal.Copy((IntPtr)(pixels + r * row), image.pixels, layer * each + ((y + r) * image.width + x) * image.pixelBytes, row);
                stored.SetPixelData(image.pixels, 0, layer, layer * each);
                stored.Apply(false, false);
                var was = image.texture;
                var into = DecodedAgain(image);
                Decode(stored, into, into == was ? layer : -1, 1); // A new one has none of its layers yet
                if (image.mips && into != null) Work().GenerateMips(into);
                return;
            }
            if (IntegerImage(image.format))
            {
                if (x < 0 || y < 0 || width <= 0 || height <= 0 || width > image.width - x || height > image.height - y) return;
                for (var r = 0; r < height; r++)
                    Marshal.Copy((IntPtr)(pixels + r * row), image.pixels, layer * each + ((y + r) * image.width + x) * image.pixelBytes, row);
                IntegersInto(Written(image), x, y, layer, width, height, 1, image.pixelBytes, pixels);
                return;
            }
            if (!(image.texture is Texture2DArray array)) return;
            for (var r = 0; r < height; r++)
            {
                Marshal.Copy((IntPtr)(pixels + r * row), image.pixels, layer * each + ((y + r) * image.width + x) * image.pixelBytes, row);
            }
            if (image.used == Time.frameCount)
            {
                // A draw of this frame reads it as it was: a new array from here
                Doom(array);
                array = NewArray(image.format, image.width, image.height, image.layers);
                image.texture = array;
                image.used = -1;
                for (var i = 0; i < image.layers; i++) array.SetPixelData(image.pixels, 0, i, i * each);
                array.Apply(image.mips && image.format != 8, false);
                if (image.mips && image.format == 8) SrgbMipmaps(image);
                return;
            }
            array.SetPixelData(image.pixels, 0, layer, layer * each);
            array.Apply(false, false); // Its smaller images stay as they were made
        }

        // An array's layer as a target: that layer of its colors and of its depth, neither its own to free
        void MakeArrayLayer(uint id, uint array, int layer)
        {
            if (!images.TryGetValue(array, out var image)) return;
            var depthAlone = image.arrayDepth == image.texture;
            targets[id] = new Target
            {
                layers = depthAlone ? null : image.texture as RenderTexture, layersDepth = image.arrayDepth, layer = layer,
                width = image.width, height = image.height, imageIds = Array.Empty<uint>(),
            };
        }

        void MakeCubeFace(uint id, uint cube, int face, uint depthId)
        {
            if (!images.TryGetValue(cube, out var image) || !(image.texture is RenderTexture rt)) return;
            var depth = DepthTexture(rt.width, rt.height, 1);
            // One for the cube's faces, made with the first
            if (image.faceColor == null)
            {
                image.faceColor = new RenderTexture(rt.width, rt.height, rt.graphicsFormat, GraphicsFormat.None) { hideFlags = HideFlags.HideAndDontSave };
                image.faceColor.Create();
            }
            targets[id] = new Target
            {
                cube = rt, face = (CubemapFace)face, faceColor = image.faceColor, depth = depth, width = rt.width, height = rt.height,
                imageIds = new[] { depthId },
            };
            images[depthId] = new Image { texture = depth, element = RenderTextureSubElement.Depth, width = rt.width, height = rt.height };
        }

        static GraphicsFormat DepthFormat() =>
            SystemInfo.IsFormatSupported(GraphicsFormat.D32_SFloat_S8_UInt, GraphicsFormatUsage.Render)
                ? GraphicsFormat.D32_SFloat_S8_UInt : GraphicsFormat.D24_UNorm_S8_UInt;

        static RenderTexture DepthTexture(int width, int height, int samples)
        {
            var depth = new RenderTexture(width, height, GraphicsFormat.None, DepthFormat())
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
                var pixelFormat = AttachmentPixelFormat((int)held);
                var rt = new RenderTexture(width, height, AttachmentFormat((int)held), GraphicsFormat.None)
                {
                    // Colors and the number formats every device blends have smaller images: a multisampled one's are its
                    // copy's, what its samples resolve to (MultisampledMipmaps)
                    antiAliasing = samples, useMipMap = samples == 1 && Blended((int)held), autoGenerateMips = false,
                    enableRandomWrite = samples == 1 && WritableImage(pixelFormat), hideFlags = HideFlags.HideAndDontSave,
                };
                rt.Create();
                colors.Add(rt);
                images[W(4 + i)] = new Image { texture = rt, width = width, height = height, format = pixelFormat,
                                               drawn = samples > 1 && Blended((int)held) ? rt : null };
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
            // The image it has for it stays, each frame it's resolved again, where it's still this one
            if (!images.TryGetValue(depthId, out var had) || had.texture != t.resolved)
            {
                images[depthId] = new Image { texture = t.resolved, element = RenderTextureSubElement.Depth, width = t.width, height = t.height };
            }
            if (resolveDepth != null)
            {
                var cb = Work();
                cb.SetRenderTarget(new RenderTargetIdentifier(t.resolved));
                cb.SetGlobalTexture(DepthMS, new RenderTargetIdentifier(t.depth), RenderTextureSubElement.Depth);
                cb.DrawProcedural(Matrix4x4.identity, resolveDepth, 0, MeshTopology.Triangles, 3, 1);
            }
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
            for (var i = 0; i < made.textureCount; i++) made.imageReads[Math.Min(i, MaxTextures - 1)] = W(at++);
            made.samplerCount = Math.Min((int)W(at++), MaxTextures);
            at += made.samplerCount; // ...and per sampler (its shader declares which kind)
            for (var i = 0; i < made.textureCount; i++) made.textureSamplers[Math.Min(i, MaxTextures - 1)] = W(at++);
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
        Shader ShaderOf(ulong key) => Loaded<Shader>(key);

        // ...a shader's, or a kernel's ComputeShader (a .compute).
        T Loaded<T>(ulong key) where T : UnityEngine.Object
        {
            var shader = Resources.Load<T>("TideShaders/" + key.ToString("x16"));
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
            return webKeys.TryGetValue(key, out var named) ? Resources.Load<T>("TideShaders/" + named.ToString("x16")) : null;
        }

        void Begin(uint target, bool clear)
        {
            inPass = true;
            stencil = 0; bias = slopeBias = 0;
            passStart = into.commands.Count;
            offscreenPass = false;
            if (target == 0 || !targets.TryGetValue(target, out var t))
            {
                // The camera's own: never cleared, Unity's scene is under what Tide draws
                flipped = false;
                into.commands.Add(new Command { kind = Kind.Begin });
                return;
            }
            offscreenPass = true;
            // Earlier camera draws retain the image they sampled. This pass writes a fresh image when needed,
            // preserving what it already holds with a copy ordered after the preceding work. A multisampled color is
            // read as it's drawn from here, not through its copy with smaller images, until those are made again.
            foreach (var id in t.imageIds)
            {
                if (!images.TryGetValue(id, out var image)) continue;
                if (image.drawn != null)
                {
                    Shown(image, image.drawn);
                    image.mips = false;
                }
                Written(image);
            }
            // A cube face or array layer attaches its owner's image, which deliberately has no per-target image
            // id. Preserve earlier camera reads of the whole cube/array before changing this face or layer too.
            WrittenAttachment(t.cube);
            WrittenAttachment(t.layers);
            WrittenAttachment(t.layersDepth);
            // A target's rows from the top, as Tide samples them: where Unity's go from the bottom (OpenGL), upside down
            flipped = !SystemInfo.graphicsUVStartsAtTop;
            into.commands.Add(new Command { kind = Kind.Begin, target = t, clear = clear, flipped = flipped, height = t.height });
        }

        void End()
        {
            into.commands.Add(new Command { kind = Kind.End });
            if (offscreenPass)
            {
                // Targets are independent of Unity's camera and run once, in the same stream as compute. Keeping
                // only camera-own draws also means a second camera never repeats the frame's image writes.
                Record(into, Work(), null, default, default, passStart, into.commands.Count);
                into.commands.RemoveRange(passStart, into.commands.Count - passStart);
            }
            inPass = offscreenPass = false;
        }

        // What a read of kind `read` (gpu.h's tide_gpu_read) gets where it's given no image that's there, of the read's
        // own dimension, as `element` says to bind it.
        Texture EmptyRead(uint read, out RenderTextureSubElement element)
        {
            element = RenderTextureSubElement.Default;
            switch (read)
            {
                case 1: case 2: case 7: // A depth, or a depth array's: the far limit, which a compare finds lit
                {
                    var layered = read == 7;
                    var depth = layered ? emptyDepthArray : emptyDepth;
                    if (depth == null)
                    {
                        depth = new RenderTexture(1, 1, GraphicsFormat.None, DepthFormat())
                        {
                            dimension = layered ? TextureDimension.Tex2DArray : TextureDimension.Tex2D, volumeDepth = 1,
                            hideFlags = HideFlags.HideAndDontSave,
                        };
                        depth.Create();
                        var cb = Work();
                        cb.SetRenderTarget(depth, 0, CubemapFace.Unknown, 0);
                        cb.ClearRenderTarget(true, false, Color.clear, 1f);
                        if (layered) emptyDepthArray = depth; else emptyDepth = depth;
                    }
                    element = RenderTextureSubElement.Depth;
                    return depth;
                }
                case 3: // A cube: dark
                    if (emptyCube == null)
                    {
                        emptyCube = new Cubemap(1, GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.None) { hideFlags = HideFlags.HideAndDontSave };
                        for (var face = 0; face < 6; face++) emptyCube.SetPixelData(new byte[] { 0, 0, 0, 255 }, 0, (CubemapFace)face);
                        emptyCube.Apply(false, true);
                    }
                    return emptyCube;
                case 6: case 13: // An array's colors, white, or its numbers, zeros
                {
                    var numbers = read == 13;
                    var array = numbers ? emptyNumberArray : emptyArray;
                    if (array == null)
                    {
                        array = new Texture2DArray(1, 1, 1, numbers ? GraphicsFormat.R32G32B32A32_SFloat : GraphicsFormat.R8G8B8A8_UNorm,
                            TextureCreationFlags.None) { hideFlags = HideFlags.HideAndDontSave };
                        if (numbers) array.SetPixelData(new float[4], 0, 0);
                        else array.SetPixelData(new byte[] { 255, 255, 255, 255 }, 0, 0);
                        array.Apply(false, true);
                        if (numbers) emptyNumberArray = array; else emptyArray = array;
                    }
                    return array;
                }
                case 14: // An array's ints: zeros, a render texture's, as an integer Texture2D is (below)
                    if (emptyIntArray == null)
                    {
                        emptyIntArray = new RenderTexture(new RenderTextureDescriptor(1, 1)
                        {
                            graphicsFormat = GraphicsFormat.R32_SInt, depthStencilFormat = GraphicsFormat.None,
                            dimension = TextureDimension.Tex2DArray, volumeDepth = 1, msaaSamples = 1,
                        }) { hideFlags = HideFlags.HideAndDontSave };
                        emptyIntArray.Create();
                        var cb = Work();
                        cb.SetRenderTarget(emptyIntArray, 0, CubemapFace.Unknown, 0);
                        cb.ClearRenderTarget(false, true, Color.clear);
                    }
                    return emptyIntArray;
            }
            if (read == 8 || read == 11) return emptyVolume;
            if (read == 9)
            {
                if (emptyNumber == null)
                {
                    emptyNumber = new Texture2D(1, 1, GraphicsFormat.R32G32B32A32_SFloat, TextureCreationFlags.None)
                    { hideFlags = HideFlags.HideAndDontSave };
                    emptyNumber.SetPixelData(new float[4], 0); emptyNumber.Apply(false, true);
                }
                return emptyNumber;
            }
            if (read == 10 || read == 12)
            {
                var volume = read == 12;
                var image = volume ? emptyIntVolume : emptyInteger;
                if (image == null)
                {
                    image = new RenderTexture(new RenderTextureDescriptor(1, 1)
                    {
                        graphicsFormat = GraphicsFormat.R32_SInt, depthStencilFormat = GraphicsFormat.None,
                        enableRandomWrite = true, dimension = volume ? TextureDimension.Tex3D : TextureDimension.Tex2D,
                        volumeDepth = 1, msaaSamples = 1,
                    }) { hideFlags = HideFlags.HideAndDontSave };
                    image.Create();
                    var cb = Work();
                    if (volume)
                    {
                        var kernel = copier.FindKernel("TideVolumeClearInt");
                        cb.SetComputeTextureParam(copier, kernel, "_TideVolumeToInt", image);
                        cb.DispatchCompute(copier, kernel, 1, 1, 1);
                    }
                    else { cb.SetRenderTarget(image); cb.ClearRenderTarget(false, true, Color.clear); }
                    if (volume) emptyIntVolume = image; else emptyInteger = image;
                }
                return image;
            }
            return white;
        }

        Material MaterialFor(Pipeline p)
        {
            var stores = p.stencilTest != 0 || p.stencilOp != 0;
            var key = (flipped ? 1 : 0) | (stores ? stencil << 1 : 0);
            if (p.materials.TryGetValue(key, out var m) && m != null) return m;
            m = new Material(p.shader) { hideFlags = HideFlags.HideAndDontSave, name = "Tide " + p.name };
            // How it blends: what tide_gpu_blend says, every color premultiplied
            switch (p.depth == 4 || p.depth == 6 ? 4 : p.blend) // Depth alone leaves every color as it is
            {
                case 4: Blend(m, BlendMode.Zero, BlendMode.One, BlendMode.Zero, BlendMode.One); break;
                case 1: Blend(m, BlendMode.One, BlendMode.One, BlendMode.OneMinusDstAlpha, BlendMode.One); break;
                case 2: Blend(m, BlendMode.DstColor, BlendMode.Zero, BlendMode.Zero, BlendMode.One); break;
                case 3: Blend(m, BlendMode.One, BlendMode.Zero, BlendMode.One, BlendMode.Zero); break;
                default: Blend(m, BlendMode.One, BlendMode.OneMinusSrcAlpha, BlendMode.One, BlendMode.OneMinusSrcAlpha); break;
            }
            if (m.HasProperty("_TideNumericMask")) m.SetFloat("_TideNumericMask", p.depth == 4 || p.depth == 6 ? 0f : 15f);
            // Depth: Unity turns the test around where it keeps depth from 1 near
            // (tide_gpu_depth: none, tested and written, tested, written whatever is there, depth alone, equal)
            m.SetFloat(ZTest, (float)(p.depth == 0 || p.depth == 3 || p.depth == 6 ? CompareFunction.Always
                                      : p.depth == 5 ? CompareFunction.Equal : CompareFunction.LessEqual));
            m.SetFloat(ZWrite, p.depth == 1 || p.depth == 3 || p.depth == 4 || p.depth == 6 ? 1f : 0f);
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
            if (!buffers.TryGetValue(vertexBuffer[slot], out var b)) return none;
            b.used = Time.frameCount;
            return b.raw;
        }

        // A draw whose numbers a storage buffer has (gpu.h's draw_indirect, draw_indexed_indirect): four or five
        // words at `offset`, copied as they are here in the frame.
        void DrawIndirect(uint buffer, uint offset, bool indexed)
        {
            if (!inPass || pipeline == null) return;
            var numbers = Arguments(buffer, offset, indexed ? 5 : 4, null);
            if (numbers != null) Draw(0, 1, 1, indexed, numbers);
        }

        void Draw(uint first, int count, int instances, bool indexed, GraphicsBuffer indirect)
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
            var material = MaterialFor(pipeline);
            var c = new Command
            {
                shaderPass = (pipeline.depth == 4 || pipeline.depth == 6) && material.passCount > 1 ? 1 : 0,
                kind = Kind.Draw, bias = bias, slopeBias = slopeBias, material = material, properties = pipeline.properties, count = count,
                instances = instances, first = indexed ? 0 : first,
                v0 = VertexBuffer(0), v1 = VertexBuffer(1), v2 = VertexBuffer(2),
                o0 = (int)vertexOffset[0], o1 = (int)vertexOffset[1], o2 = (int)vertexOffset[2],
                textures = into.textures.Count,
                uniformAt = uniformAt, uniformSize = pipeline.uniformSize > 0 ? Math.Max(16, (pipeline.uniformSize + 15) / 16 * 16) : 0,
                arguments = indirect,
            };
            if (pipeline.storageCount > 0)
            {
                c.storage = into.storage.Count;
                c.storageCount = pipeline.storageCount;
                for (var i = 0; i < c.storageCount; i++) into.storage.Add(StorageBuffer(i, false));
            }
            if (indexed)
            {
                if (!buffers.TryGetValue(indexBuffer, out var ib) || !ib.indices) return;
                c.index = IndexView(ib, indexWide);
            }
            Modes(pipeline, out var modes, out var modesHigh);
            uint unmipped = 0;
            ulong owns = AllUnknown;
            for (var i = 0; i < pipeline.textureCount && i < MaxTextures; i++)
            {
                Texture texture;
                RenderTextureSubElement element;
                if (!images.TryGetValue(sampled[i], out var image) || image.texture == null) texture = EmptyRead(pipeline.imageReads[i], out element);
                else
                {
                    if (!image.mips) unmipped |= 1u << i; // No smaller images yet: blended, as gpu.h has it (tidehl_mode)
                    texture = image.texture;
                    element = image.element;
                    if (ownSamplers && element == RenderTextureSubElement.Default) owns = OwnBits(owns, i, Own(image, OwnMode(pipeline, i, image.mips)));
                    image.used = Time.frameCount;
                }
                into.textures.Add(texture);
                into.elements.Add(element);
            }
            c.textureCount = into.textures.Count - c.textures;
            c.sampling = modes;
            c.samplingHigh = modesHigh;
            c.unmipped = unmipped;
            c.own = (uint)owns;
            c.ownHigh = (uint)(owns >> 32);
            into.commands.Add(c);
        }

        // Storage buffer `slot` as a draw (which reads it later, as the camera renders) or the work (now) binds it: a
        // buffer a draw reads is marked so that the work's writes after it rename it, and one the work writes is
        // renamed where a draw read it. What isn't there reads as nothing.
        GraphicsBuffer StorageBuffer(int slot, bool written)
        {
            if (!buffers.TryGetValue(storageBound[slot], out var b) || b.raw == null) return none;
            if (written) return Written(b);
            if (into != null && !inCompute) b.used = Time.frameCount;
            return b.raw;
        }

        // A buffer the work writes from here (a kernel, a copy): where a draw of this frame kept reads it, a new one,
        // its bytes copied first in the work's order, so that draw reads it as it was.
        GraphicsBuffer Written(BufferObject b)
        {
            if (b.raw == null) return none;
            if (b.used == Time.frameCount)
            {
                var renamed = Renamed(b.raw);
                Work().CopyBuffer(b.raw, renamed);
                b.raw = renamed;
                b.used = -1;
            }
            return b.raw;
        }

        // A camera-own draw can still be pending when later work changes an image. Rename it by actual texture
        // identity, updating every alias and destination; the draw kept the previous object. Offscreen passes have
        // already entered Work(), so the copy is after their writes and before this write.
        RenderTexture Written(Image image)
        {
            if (!(image.texture is RenderTexture texture)) return null;
            var used = image.used == Time.frameCount;
            foreach (var alias in images.Values) if (alias.texture == texture && alias.used == Time.frameCount) used = true;
            if (!used) return texture;
            // One a rename let go of where there is one: a target drawn into after a draw read it, each frame, goes
            // round a few images rather than making one a frame, as a buffer does (Renamed)
            var next = Renamed(texture);
            if (next == null) { VolumeUnavailable(); return null; }
            Work().CopyTexture(texture, next);
            foreach (var alias in images.Values)
            {
                if (alias.arrayDepth == texture) alias.arrayDepth = next;
                if (alias.drawn == texture) alias.drawn = next;
                if (alias.mipped == texture) alias.mipped = next;
                if (alias.texture != texture) continue;
                alias.texture = next;
                alias.used = -1;
                alias.own = 15;
            }
            foreach (var target in targets.Values)
            {
                if (target.colors != null)
                    for (var i = 0; i < target.colors.Length; i++) if (target.colors[i] == texture) target.colors[i] = next;
                if (target.cube == texture) target.cube = next;
                if (target.layers == texture) target.layers = next;
                if (target.depth == texture) target.depth = next;
                if (target.resolved == texture) target.resolved = next;
                if (target.layersDepth == texture) target.layersDepth = next;
            }
            doomed.Add((texture, Time.frameCount, true)); // A spare, once no camera's commands read it
            return next;
        }

        void WrittenAttachment(RenderTexture attachment)
        {
            if (attachment == null) return;
            foreach (var image in images.Values)
            {
                if (image.texture != attachment) continue;
                Written(image);
                return;
            }
        }

        CommandBuffer Work()
        {
            working = true;
            return work;
        }

        // The work so far, run now: before what's done at once (a buffer or texture written) and at the commands' end.
        // The kernels' values are in the recording's uniform buffer, which gets them first.
        void Flush()
        {
            if (!working || work == null) return;
            working = false;
            if (into != null && into.uniformBuffer != null && into.uniformUsed > 0 && into.uniformBytes.IsCreated) into.uniformBuffer.SetData(into.uniformBytes);
            Graphics.ExecuteCommandBuffer(work);
            work.Clear();
        }

        // A pooled buffer: the frame's `used`-th, made where the pool hasn't one yet.
        static GraphicsBuffer Pooled(List<GraphicsBuffer> pool, int used, GraphicsBuffer.Target target, int count, int stride)
        {
            if (used == pool.Count) pool.Add(new GraphicsBuffer(target, count, stride));
            return pool[used];
        }

        // `words` (at most 8) of a storage buffer at `offset`, as the work has them here, copied into buffers an
        // indirect call reads: its arguments, and a dispatch's TideGroups (`groupsBuffer`) too. Null where it can't.
        GraphicsBuffer Arguments(uint id, uint offset, int words, GraphicsBuffer groupsBuffer)
        {
            if (!buffers.TryGetValue(id, out var b) || b.raw == null || !CanCopy()) return null;
            var at = stagedUsed++;
            var numbers = Pooled(staged, at, GraphicsBuffer.Target.Raw | GraphicsBuffer.Target.CopySource | GraphicsBuffer.Target.CopyDestination, 8, 4);
            var given = Pooled(arguments, at, GraphicsBuffer.Target.IndirectArguments | GraphicsBuffer.Target.CopyDestination, 8, 4);
            Copy(b.raw, offset, numbers, 0, (uint)words);
            Work().CopyBuffer(numbers, given);
            if (groupsBuffer != null) work.CopyBuffer(numbers, groupsBuffer);
            return given;
        }

        bool CanCopy()
        {
            if (copyKernel >= 0) return true;
            if (!copySaid)
            {
                copySaid = true;
                Debug.LogError("Tide: com.tide.unity's Resources/TideCompute.compute isn't there or doesn't run on this GPU: Tide's indirect draws, dispatches and copies are left out.");
            }
            return false;
        }

        // `words` words from `from` at byte `fromOffset` to `to` at `toOffset`, in the work's order: gpu.h's
        // buffer_copy, which Unity's CopyBuffer (whole buffers alone) isn't.
        void Copy(GraphicsBuffer from, uint fromOffset, GraphicsBuffer to, uint toOffset, uint words)
        {
            if (words == 0 || !CanCopy()) return;
            if (from == to)
            {
                // Through a buffer of its own: one buffer isn't read and written by one dispatch
                var between = new GraphicsBuffer(GraphicsBuffer.Target.Raw, (int)words, 4);
                Copy(from, fromOffset, between, 0, words);
                Copy(between, 0, to, toOffset, words);
                Doom(between);
                return;
            }
            var cb = Work();
            var groupCount = (words + 63) / 64;
            var x = Math.Min(groupCount, 65535u);
            var y = (groupCount + x - 1) / x;
            cb.SetComputeBufferParam(copier, copyKernel, CopyFrom, from);
            cb.SetComputeBufferParam(copier, copyKernel, CopyTo, to);
            cb.SetComputeIntParams(copier, CopyWords, (int)fromOffset, (int)toOffset, (int)words, (int)x);
            cb.DispatchCompute(copier, copyKernel, (int)x, (int)y, 1);
        }

        // A kernel's ComputeShader and its kernel, looked for again each frame till Unity has imported it (a new
        // build's), as a draw's shader is.
        ComputeShader KernelOf(Pipeline p)
        {
            if (p.kernelShader == null && p.looked != Time.frameCount)
            {
                p.looked = Time.frameCount;
                webKeys = null;
                p.kernelShader = Loaded<ComputeShader>(p.key);
                if (p.kernelShader == null && Time.frameCount - p.made > 120 && missing.Add(p.key))
                {
                    Debug.LogError($"Tide: the kernel of Tide's {p.name} pipeline isn't in com.tide.game (Resources/TideShaders/{p.key:x16}.compute): tide writes it as it builds the game for Unity, so build the game with this tide.");
                }
            }
            if (p.kernelShader == null || p.kernel == -2) return null;
            if (p.kernel < 0)
            {
                p.kernel = p.kernelShader.HasKernel("tidehl_compute") ? p.kernelShader.FindKernel("tidehl_compute") : -2;
                if (p.kernel >= 0 && !p.kernelShader.IsSupported(p.kernel)) p.kernel = -2;
                if (p.kernel == -2) Debug.LogError($"Tide: the kernel of Tide's {p.name} pipeline ({p.key:x16}.compute) doesn't run on this GPU: its runs are left out.");
            }
            return p.kernel >= 0 ? p.kernelShader : null;
        }

        // A compute pass's dispatch: x * y * z workgroups, or as many as three words of a storage buffer say there
        // (`indirect`). Into the work, with the pipeline's storage, uniforms, textures and TideGroups.
        void Dispatch(uint x, uint y, uint z, uint buffer, uint offset, bool indirect)
        {
            if (!inCompute || pipeline == null || !pipeline.compute) return;
            if (!indirect && (x == 0 || y == 0 || z == 0)) return;
            var cs = KernelOf(pipeline);
            if (cs == null) return;
            var kernel = pipeline.kernel;
            // The same image may arrive through different fields. No backend permits a writable storage image to
            // alias a sampled image in this dispatch; refuse the run rather than submit an invalid binding set.
            for (var i = 0; i < pipeline.textureCount && i < MaxTextures; i++)
            {
                if ((pipeline.imageWrites >> i & 1) == 0) continue;
                if (!images.TryGetValue(sampled[i], out var image) || !(image.texture is RenderTexture rt) || !rt.enableRandomWrite
                    || !WritableImage(image.format) || image.format != pipeline.imageFormats[i]) return;
                for (var j = 0; j < pipeline.textureCount && j < MaxTextures; j++)
                    if (i != j && images.TryGetValue(sampled[j], out var other) && other.texture == image.texture) return;
            }
            for (var i = 0; i < pipeline.textureCount && i < MaxTextures; i++)
                if ((pipeline.imageWrites >> i & 1) != 0 && Written(images[sampled[i]]) == null) return;
            var groupsBuffer = Pooled(groups, groupsUsed++, GraphicsBuffer.Target.Constant | GraphicsBuffer.Target.CopyDestination, 2, 16);
            GraphicsBuffer numbers = null;
            if (indirect)
            {
                numbers = Arguments(buffer, offset, 3, groupsBuffer);
                if (numbers == null) return;
            }
            else
            {
                // Its own, so it's written now
                if (!groupWords.IsCreated) groupWords = new NativeArray<uint>(8, Allocator.Persistent);
                groupWords[0] = x;
                groupWords[1] = y;
                groupWords[2] = z;
                groupsBuffer.SetData(groupWords);
            }
            var cb = Work();
            for (var i = 0; i < pipeline.storageCount; i++) cb.SetComputeBufferParam(cs, kernel, Storage[i], StorageBuffer(i, (pipeline.storageWrites >> i & 1) != 0));
            if (pipeline.uniformSize > 0 && into.uniformBuffer != null)
                cb.SetComputeConstantBufferParam(cs, Uniforms, into.uniformBuffer, uniformAt, Math.Max(16, (pipeline.uniformSize + 15) / 16 * 16));
            cb.SetComputeConstantBufferParam(cs, Groups, groupsBuffer, 0, 16);
            Modes(pipeline, out var modes, out var modesHigh);
            uint unmipped = 0;
            ulong owns = AllUnknown;
            for (var i = 0; i < pipeline.textureCount && i < MaxTextures; i++)
            {
                Texture texture;
                RenderTextureSubElement element;
                if (!images.TryGetValue(sampled[i], out var image) || image.texture == null) texture = EmptyRead(pipeline.imageReads[i], out element);
                else
                {
                    if (!image.mips) unmipped |= 1u << i;
                    texture = image.texture;
                    element = image.element;
                    if (ownSamplers && element == RenderTextureSubElement.Default) owns = OwnBits(owns, i, Own(image, OwnMode(pipeline, i, image.mips)));
                    if ((pipeline.imageWrites >> i & 1) != 0) image.mips = false;
                }
                if (element == RenderTextureSubElement.Depth)
                    cb.SetComputeTextureParam(cs, kernel, Textures[i], new RenderTargetIdentifier(texture), 0, RenderTextureSubElement.Depth);
                else cb.SetComputeTextureParam(cs, kernel, Textures[i], texture);
            }
            cb.SetComputeIntParam(cs, Sampling, (int)modes);
            cb.SetComputeIntParam(cs, SamplingHigh, (int)modesHigh);
            cb.SetComputeIntParam(cs, Unmipped, (int)unmipped);
            cb.SetComputeIntParam(cs, OwnLow, (int)(uint)owns);
            cb.SetComputeIntParam(cs, OwnHigh, (int)(uint)(owns >> 32));
            if (indirect) cb.DispatchCompute(cs, kernel, numbers, 0);
            else cb.DispatchCompute(cs, kernel, (int)x, (int)y, (int)z);
        }

        // A read back (gpu.h's buffer_read), in the work's order: its bytes go to the embed as Unity has them
        // (tide_embed_render_read), or that it failed.
        void Read(uint token, uint id, uint offset, uint size)
        {
            if (!buffers.TryGetValue(id, out var b) || b.raw == null || !SystemInfo.supportsAsyncGPUReadback || size == 0
                || (ulong)offset + size > (ulong)b.raw.count * 4)
            {
                Answered(token, null, 0);
                return;
            }
            var epoch = readEpoch;
            Work().RequestAsyncReadback(b.raw, (int)size, (int)offset, request => Answer(epoch, token, size, request));
        }

        void Answer(int epoch, uint token, uint size, AsyncGPUReadbackRequest request)
        {
            if (epoch != readEpoch) return; // Let go of with everything else: the embed has forgotten it
            var data = request.hasError ? default : request.GetData<byte>();
            if (request.hasError || data.Length < size) Answered(token, null, 0);
            else Answered(token, (byte*)NativeArrayUnsafeUtility.GetUnsafeReadOnlyPtr(data), size);
        }

        // Where reads back (BUFFER_READ) go, for whoever replays commands it made itself rather than the runner's (a
        // test): each read's token, then its bytes, valid during the call, and their size, or IntPtr.Zero and 0 for
        // one that failed. Null, they go to the runner's embed.
        public delegate void ReadAnswer(uint token, IntPtr data, uint size);
        public ReadAnswer ReadsTo { get; set; }

        void Answered(uint token, byte* data, uint size)
        {
            if (ReadsTo != null) ReadsTo(token, (IntPtr)data, size);
            else if (Runner != null) Runner.RenderRead(token, data, size);
        }

        // How a pipeline's samplers sample, as SAMPLER said: 4 bits each, two banks of eight (_TideSampling,
        // _TideSamplingHigh). With none said apart (sampler_count 0), sampler i is texture i's own.
        void Modes(Pipeline pipeline, out uint modes, out uint modesHigh)
        {
            modes = modesHigh = 0;
            var count = pipeline.samplerCount > 0 ? pipeline.samplerCount : Math.Min(pipeline.textureCount, MaxTextures);
            for (var j = 0; j < count; j++)
            {
                if (j < 8) modes |= (sampling[j] & 15) << (4 * j);
                else modesHigh |= (sampling[j] & 15) << (4 * (j - 8));
            }
        }

        // The sampling texture `i` carries itself on OpenGL Core, which has no samplers apart from textures: its first
        // sampler's, blended for one without its smaller images yet.
        uint OwnMode(Pipeline pipeline, int i, bool mips)
        {
            var through = pipeline.samplerCount > 0 ? pipeline.textureSamplers[i] : 1u << i;
            var j = 0;
            while (j < MaxTextures - 1 && (through >> j & 1) == 0) j++;
            var mode = sampling[j];
            return (mode & 3) >= 2 && !mips ? mode & ~3u | 1 : mode;
        }

        // OpenGL Core's: how a texture samples itself this frame. Unity's OpenGL has no samplers apart from textures, and a
        // texture's filter and wrap are what every draw of the frame reads it with, as they run after it's all recorded:
        // so they're set by the frame's first draw that reads it, and a draw that reads it another way has its shader
        // emulate that way from its pixels (hlsl.c, tidehl_emulated). 15 a texture, as _TideOwn says, for none known.
        const ulong AllUnknown = ulong.MaxValue;

        static ulong OwnBits(ulong owns, int i, uint own) => owns & ~(15ul << (4 * i)) | (ulong)(own & 15u) << (4 * i);

        static uint Own(Image image, uint wanted)
        {
            if (image.used != Time.frameCount || image.own == 15u)
            {
                Sample(image.texture, wanted);
                image.own = wanted & 15u;
            }
            return image.own;
        }

        static void Sample(Texture texture, uint mode)
        {
            var filter = mode & 3;
            texture.filterMode = filter == 0 ? FilterMode.Point : filter == 1 ? FilterMode.Bilinear : FilterMode.Trilinear;
            texture.anisoLevel = filter == 3 ? 8 : 1; // Filter.Anisotropic: gpu.h's TIDE_GPU_ANISOTROPY, which the device clamps
            var wrap = mode >> 2 & 3;
            texture.wrapMode = wrap == 1 ? TextureWrapMode.Repeat : wrap == 2 ? TextureWrapMode.Mirror : TextureWrapMode.Clamp;
        }

        // A recording into a camera's commands, onto `color` and `depth`, the camera's own.
        void Record(Recording r, CommandBuffer cb, Camera camera, RenderTargetIdentifier color, RenderTargetIdentifier depth,
                    int start = 0, int end = -1)
        {
            var passHeight = camera != null ? camera.pixelHeight : 1;
            var own = true; // The pass is on the camera's own target
            var inverted = false; // A target's pass whose sides are turned back (Begin)
            Target face = null; // ...or on a cube's face
            if (end < 0) end = r.commands.Count;
            for (var at = start; at < end; at++)
            {
                var c = r.commands[at];
                switch (c.kind)
                {
                    case Kind.Begin:
                        if (c.target == null)
                        {
                            // Its rows go as Unity's projection says (_ProjectionParams.x), its colors as Unity's color space has them
                            cb.SetRenderTarget(color, depth);
                            passHeight = camera != null ? camera.pixelHeight : 1;
                            cb.SetGlobalFloat(Flip, 0f);
                            cb.SetGlobalFloat(Linear, r.linear ? 1f : 0f);
                        }
                        else
                        {
                            var t = c.target;
                            if (t.layer >= 0 && t.layers != null)
                                cb.SetRenderTarget(new RenderTargetIdentifier(t.layers, 0, CubemapFace.Unknown, t.layer),
                                                   new RenderTargetIdentifier(t.layersDepth, 0, CubemapFace.Unknown, t.layer));
                            else if (t.layer >= 0) cb.SetRenderTarget(new RenderTargetIdentifier(t.layersDepth, 0, CubemapFace.Unknown, t.layer));
                            else if (t.cube != null)
                            {
                                // The cube's one image for its faces' passes holds the face it's drawn into first, unless
                                // the pass clears it
                                if (!c.clear) cb.CopyTexture(t.cube, (int)t.face, 0, t.faceColor, 0, 0);
                                cb.SetRenderTarget(new RenderTargetIdentifier(t.faceColor), new RenderTargetIdentifier(t.depth));
                            }
                            else if (t.colors.Length == 0) cb.SetRenderTarget(new RenderTargetIdentifier(t.depth));
                            else if (t.colors.Length == 1) cb.SetRenderTarget(new RenderTargetIdentifier(t.colors[0]), new RenderTargetIdentifier(t.depth));
                            else
                            {
                                // Kept with the target: a pass on several colors each frame makes no array a frame
                                var ids = t.colorIds ??= new RenderTargetIdentifier[t.colors.Length];
                                for (var i = 0; i < ids.Length; i++) ids[i] = new RenderTargetIdentifier(t.colors[i]);
                                cb.SetRenderTarget(ids, new RenderTargetIdentifier(t.depth));
                            }
                            if (c.clear) cb.ClearRenderTarget(RTClearFlags.All, Color.black, 1f, 0);
                            passHeight = c.height;
                            cb.SetGlobalFloat(Flip, c.flipped ? -1f : 1f);
                            // Where a texture's first row is its top, Unity turns the rasterizer's sides round in a
                            // render texture, for the projection it flips there; Tide's pass isn't flipped, so its
                            // sides (culling and this.front) are turned back, until the pass ends
                            if (!c.flipped) { cb.SetInvertCulling(true); inverted = true; }
                            cb.SetGlobalFloat(Linear, 0f);
                        }
                        own = c.target == null;
                        face = c.target != null && c.target.cube != null ? c.target : null;
                        cb.SetGlobalFloat(Height, passHeight);
                        break;
                    case Kind.End:
                        cb.DisableScissorRect();
                        if (inverted) cb.SetInvertCulling(false);
                        inverted = false;
                        if (face != null) cb.CopyTexture(face.faceColor, 0, 0, face.cube, (int)face.face, 0);
                        face = null;
                        break;
                    case Kind.Scissor:
                        // Unity counts the camera's rows from the bottom. A target's are counted as they're stored, which
                        // is from Tide's top on every API: where Unity's rows go from the bottom (OpenGL) the pass is
                        // drawn upside down (Begin's flipped), elsewhere a texture's first row is its top
                        cb.EnableScissorRect(new Rect(c.x, own ? passHeight - c.y - c.height : c.y, c.width, c.height));
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
                        cb.SetGlobalBuffer(Vertices[0], c.v0);
                        cb.SetGlobalBuffer(Vertices[1], c.v1);
                        cb.SetGlobalBuffer(Vertices[2], c.v2);
                        cb.SetGlobalInteger(Offsets[0], c.o0);
                        cb.SetGlobalInteger(Offsets[1], c.o1);
                        cb.SetGlobalInteger(Offsets[2], c.o2);
                        cb.SetGlobalDepthBias(c.bias, c.slopeBias);
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
                        cb.SetGlobalInteger(Unmipped, (int)c.unmipped);
                        cb.SetGlobalInteger(OwnLow, (int)c.own);
                        cb.SetGlobalInteger(OwnHigh, (int)c.ownHigh);
                        if (c.uniformSize > 0) cb.SetGlobalConstantBuffer(r.uniformBuffer, Uniforms, c.uniformAt, c.uniformSize);
                        for (var i = 0; i < c.storageCount; i++) cb.SetGlobalBuffer(Storage[i], r.storage[c.storage + i]);
                        if (c.arguments != null)
                        {
                            // Its numbers on the GPU: the first vertex or index is theirs, not _TideFirst's
                            if (c.index != null)
                                cb.DrawProceduralIndirect(c.index, Matrix4x4.identity, c.material, c.shaderPass, MeshTopology.Triangles, c.arguments, 0, c.properties);
                            else cb.DrawProceduralIndirect(Matrix4x4.identity, c.material, c.shaderPass, MeshTopology.Triangles, c.arguments, 0, c.properties);
                        }
                        else if (c.index != null)
                        {
                            cb.DrawProcedural(c.index, Matrix4x4.identity, c.material, c.shaderPass, MeshTopology.Triangles, c.count, c.instances,
                                c.properties);
                        }
                        else cb.DrawProcedural(Matrix4x4.identity, c.material, c.shaderPass, MeshTopology.Triangles, c.count, c.instances, c.properties);
                        cb.SetGlobalDepthBias(0, 0);
                        break;
                }
            }
        }
    }
}
