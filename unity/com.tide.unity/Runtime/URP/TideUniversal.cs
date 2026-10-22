using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.RenderGraphModule;
using UnityEngine.Rendering.Universal;

namespace Tide
{
    // Tide's drawing in URP: a pass after the transparent objects of each
    // camera TideRender draws into (the frame's camera, and the editor's scene
    // views), enqueued as the camera begins rendering, so no renderer feature
    // has to be added to the project's renderer. It records TideRender's
    // commands onto the camera's color and depth. Compiled where the project
    // has URP (Tide.Runtime.URP's TIDE_URP).
    static class TideUniversal
    {
        static readonly TidePass pass = new TidePass();

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        static void Hook()
        {
            RenderPipelineManager.beginCameraRendering -= Begin;
            RenderPipelineManager.beginCameraRendering += Begin;
        }

        static void Begin(ScriptableRenderContext context, Camera camera)
        {
            var render = TideRender.Current;
            if (render == null || !(GraphicsSettings.currentRenderPipeline is UniversalRenderPipelineAsset)) return;
            var renderer = camera.GetUniversalAdditionalCameraData().scriptableRenderer;
            if (renderer == null || !render.Prepare(camera)) return;
            pass.Camera = camera;
            renderer.EnqueuePass(pass);
        }

        sealed class TidePass : ScriptableRenderPass
        {
            public Camera Camera;

            sealed class Data
            {
                public Camera camera;
                public TextureHandle color, depth;
            }

            public TidePass()
            {
                renderPassEvent = RenderPassEvent.AfterRenderingTransparents;
                profilingSampler = new ProfilingSampler("Tide");
            }

            public override void RecordRenderGraph(RenderGraph renderGraph, ContextContainer frameData)
            {
                var resources = frameData.Get<UniversalResourceData>();
                using var builder = renderGraph.AddUnsafePass<Data>("Tide", out var data, profilingSampler);
                data.camera = Camera;
                data.color = resources.activeColorTexture;
                data.depth = resources.activeDepthTexture;
                builder.UseTexture(data.color, AccessFlags.ReadWrite);
                builder.UseTexture(data.depth, AccessFlags.ReadWrite);
                builder.AllowPassCulling(false);
                builder.AllowGlobalStateModification(true);
                builder.SetRenderFunc(static (Data data, UnsafeGraphContext context) =>
                {
                    var render = TideRender.Current;
                    if (render == null) return;
                    render.Record(data.camera, CommandBufferHelpers.GetNativeCommandBuffer(context.cmd), data.color, data.depth);
                });
            }
        }
    }
}
