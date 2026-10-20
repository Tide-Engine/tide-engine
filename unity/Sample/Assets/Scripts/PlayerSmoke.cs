using System;
using System.Collections;
using Tide;
using UnityEngine;

// A player's check of itself, for a build with no one at it: run it with
// -batchmode -nographics -tide-smoke and it says in its log whether the match
// ran, its ball is shown and the scene's pillars turn, then quits (1 if not).
public sealed class PlayerSmoke : MonoBehaviour
{
    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    static void Begin()
    {
        if (Array.IndexOf(Environment.GetCommandLineArgs(), "-tide-smoke") < 0) return;
        new GameObject("Player smoke").AddComponent<PlayerSmoke>();
    }

    IEnumerator Start()
    {
        var pillar = GameObject.Find("Pillars")?.transform.GetChild(0);
        var before = pillar != null ? pillar.rotation : Quaternion.identity;
        var until = Time.realtimeSinceStartup + 3f;
        while (Time.realtimeSinceStartup < until) yield return null;
        var runner = TideRunner.Current;
        TideEntityView ball = null;
        foreach (var view in FindObjectsByType<TideEntityView>(FindObjectsSortMode.None))
        {
            if (view.Has<Tide.Game.Ball>()) ball = view;
        }
        var turned = pillar != null && Quaternion.Angle(before, pillar.rotation) > 10f;
        var ok = runner != null && runner.Running && ball != null && ball.Mine && turned;
        Debug.Log($"Tide smoke: running {runner != null && runner.Running}, ball {(ball != null ? ball.transform.position.ToString("F2") : "none")}, " +
                  $"mine {ball != null && ball.Mine}, pillar turned {turned}: {(ok ? "PASS" : "FAIL")}");
        Application.Quit(ok ? 0 : 1);
    }
}
