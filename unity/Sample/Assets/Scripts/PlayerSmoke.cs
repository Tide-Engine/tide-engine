using System;
using System.Collections;
using Tide;
using UnityEngine;

// A player's check of itself, for a build with no one at it: run it with
// -batchmode -nographics -tide-smoke (or open a web player's page with
// ?tide-smoke, as web-smoke.mjs does) and it says in its log whether the match
// ran, its ball is shown and the scene's pillars turn, and with a GPU whether
// Tide's renderer drew the game's frag and the cube over the ball into Unity's
// frame, then quits (1 if not).
public sealed class PlayerSmoke : MonoBehaviour
{
    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    static void Begin()
    {
        // A desktop player's command line, or a web player's address (index.html?tide-smoke)
        var asked = Array.IndexOf(Environment.GetCommandLineArgs(), "-tide-smoke") >= 0 || Application.absoluteURL.Contains("tide-smoke");
        if (!asked) return;
        new GameObject("Player smoke").AddComponent<PlayerSmoke>();
    }

    IEnumerator Start()
    {
        // The match first: on the web, Tide's program loads beside Unity's, which takes a moment
        var deadline = Time.realtimeSinceStartup + 30f;
        TideEntityView ball = null;
        while (Time.realtimeSinceStartup < deadline && (ball = Ball()) == null) yield return null;
        var pillar = GameObject.Find("Pillars")?.transform.GetChild(0);
        var before = pillar != null ? pillar.rotation : Quaternion.identity;
        var until = Time.realtimeSinceStartup + 2f;
        while (Time.realtimeSinceStartup < until) yield return null;
        var runner = TideRunner.Current;
        ball = Ball();
        var turned = pillar != null && Quaternion.Angle(before, pillar.rotation) > 10f;
        var drew = "no GPU";
        if (SystemInfo.graphicsDeviceType != UnityEngine.Rendering.GraphicsDeviceType.Null) yield return Drew(ball, seen => drew = seen);
        var ok = runner != null && runner.Running && ball != null && ball.Mine && turned && (drew == "no GPU" || drew == "yes");
        Debug.Log($"Tide smoke: running {runner != null && runner.Running}, ball {(ball != null ? ball.transform.position.ToString("F2") : "none")}, " +
                  $"mine {ball != null && ball.Mine}, pillar turned {turned}, drew {drew}: {(ok ? "PASS" : "FAIL")}");
        Application.Quit(ok ? 0 : 1);
    }

    // Whether Tide's renderer drew into the main camera, as the frame on the screen shows: the plate's orange stripe in
    // the game's frag, and the cube over the ball in its color. "yes", or what it saw.
    static IEnumerator Drew(TideEntityView ball, Action<string> said)
    {
        var camera = Camera.main;
        if (ball == null || camera == null)
        {
            said("nothing to look at");
            yield break;
        }
        if (TideRunner.Current == null || !TideRunner.Current.TryGetComponent(out TideRender render) || !render.enabled)
        {
            said($"not with Tide's renderer ({SystemInfo.graphicsDeviceType}, shader level {SystemInfo.graphicsShaderLevel}, " +
                 $"constant buffers {SystemInfo.supportsSetConstantBuffer})");
            yield break;
        }
        yield return new WaitForEndOfFrame();
        var read = ScreenCapture.CaptureScreenshotAsTexture();
        Color At(Vector3 world)
        {
            var p = camera.WorldToScreenPoint(world);
            return read.GetPixel(Mathf.RoundToInt(p.x), Mathf.RoundToInt(p.y));
        }
        static bool Near(Color a, Color b) => Mathf.Abs(a.r - b.r) < 0.05f && Mathf.Abs(a.g - b.g) < 0.05f && Mathf.Abs(a.b - b.b) < 0.05f;
        var stripe = At(new Vector3(-1.1f, 0.02f, -2.25f));
        var cube = At(ball.transform.position + new Vector3(0, 1.2f, 0));
        var color = ball.GetComponent<Tide.Game.Ball>().color;
        Destroy(read);
        said(Near(stripe, new Color(1f, 0.5f, 0f)) && Near(cube, color) ? "yes" : $"stripe {stripe}, cube {cube} for {color}");
    }

    static TideEntityView Ball()
    {
        foreach (var view in FindObjectsByType<TideEntityView>())
        {
            if (view.GetComponent<Tide.Game.Ball>() != null) return view;
        }
        return null;
    }
}