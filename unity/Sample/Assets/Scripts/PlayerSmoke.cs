using System;
using System.Collections;
using Tide;
using UnityEngine;

// A player's check of itself, for a build with no one at it: run it with
// -batchmode -nographics -tide-smoke (or open a web player's page with
// ?tide-smoke, as web-smoke.mjs does) and it says in its log whether the match
// ran, its ball is shown and the scene's pillars turn, then quits (1 if not).
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
        var ok = runner != null && runner.Running && ball != null && ball.Mine && turned;
        Debug.Log($"Tide smoke: running {runner != null && runner.Running}, ball {(ball != null ? ball.transform.position.ToString("F2") : "none")}, " +
                  $"mine {ball != null && ball.Mine}, pillar turned {turned}: {(ok ? "PASS" : "FAIL")}");
        Application.Quit(ok ? 0 : 1);
    }

    static TideEntityView Ball()
    {
        foreach (var view in FindObjectsByType<TideEntityView>(FindObjectsSortMode.None))
        {
            if (view.Has<Tide.Game.Ball>()) return view;
        }
        return null;
    }
}