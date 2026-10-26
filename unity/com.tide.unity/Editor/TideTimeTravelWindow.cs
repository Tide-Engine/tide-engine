using System.IO;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // Window > Tide > Time Travel: the running match's ticks, in Play. Pause it, step it a tick at a time, scrub back
    // through the ticks kept (the GameObjects, inspectors and Tide's drawing show the tick), go on from a tick shown,
    // and save the match's recording or replay one (a tester's, from a bug report), each tick checked against it.
    // What it does is TideRunner's (Pause, Step, ShowTick, ContinueFromHere, Recording, Replay): this draws it.
    public sealed class TideTimeTravelWindow : EditorWindow
    {
        const string Extension = "tiderec";

        [MenuItem("Window/Tide/Time Travel")]
        public static TideTimeTravelWindow Open()
        {
            var window = GetWindow<TideTimeTravelWindow>();
            window.titleContent = new GUIContent("Time Travel", AssetDatabase.LoadAssetAtPath<Texture2D>(TideIcons.FilePath));
            window.minSize = new Vector2(360, 150);
            return window;
        }

        void OnEnable()
        {
            EditorApplication.update += Follow;
        }

        void OnDisable()
        {
            EditorApplication.update -= Follow;
        }

        // While a match plays, the window keeps up with it
        void Follow()
        {
            if (EditorApplication.isPlaying && TideRunner.Current != null) Repaint();
        }

        // What the window says of where time travel is: a line for each thing, for its labels and for tests.
        public static string[] Describe(TideTravel t)
        {
            var lines = new System.Collections.Generic.List<string>();
            switch (t.state)
            {
            case TideTravelState.Off:
                lines.Add("Time travel is off: the runner's Time Travel setting keeps nothing.");
                break;
            case TideTravelState.None:
                lines.Add("Nothing to pause: " + t.Why + ".");
                break;
            case TideTravelState.Live:
                lines.Add($"Recording: tick {t.last}, from tick {t.first}.");
                break;
            case TideTravelState.Paused:
                lines.Add(t.shown == t.last ? $"Paused at tick {t.shown}." : $"Showing tick {t.shown} of {t.first} to {t.last}.");
                if (t.background != 0) lines.Add($"Other machines are in the match: it goes on behind what's shown (at tick {t.last}). Resume shows it.");
                break;
            case TideTravelState.Replay:
                lines.Add($"Replaying: tick {t.shown} of {t.first} to {t.last}{(t.playing != 0 ? ", playing" : "")}.");
                if (t.unreadable != 0) lines.Add($"This build can't replay tick {t.unreadable}: its input, commands or players aren't this build's.");
                else if (t.differs != 0) lines.Add($"Goes otherwise from tick {t.differs}: the recording's world there has hash {t.expected:x16}, this build's {t.got:x16}.");
                else if (t.checkedTo > t.first) lines.Add($"Every tick to {t.checkedTo} made the recording's world.");
                if (t.otherBuild != 0) lines.Add("Recorded by a build whose code was another.");
                break;
            }
            if (t.state == TideTravelState.Paused || t.state == TideTravelState.Replay) lines.Add($"World hash {t.hash:x16}");
            return lines.ToArray();
        }

        void OnGUI()
        {
            var runner = TideRunner.Current;
            if (!EditorApplication.isPlaying || runner == null || !runner.Running)
            {
                EditorGUILayout.HelpBox("Time travel shows a match in Play: enter Play with a TideRunner in the scene.", MessageType.Info);
                return;
            }
            var t = runner.Travel;
            foreach (var line in Describe(t)) EditorGUILayout.LabelField(line, EditorStyles.wordWrappedLabel);
            if (!string.IsNullOrEmpty(t.Refused)) EditorGUILayout.HelpBox("The recording wasn't replayed: " + t.Refused + ".", MessageType.Warning);
            EditorGUILayout.Space();
            if (t.state == TideTravelState.Replay) Replaying(runner, t);
            else Playing(runner, t);
            EditorGUILayout.Space();
            Recordings(runner, t);
        }

        void Playing(TideRunner runner, TideTravel t)
        {
            var paused = t.state == TideTravelState.Paused;
            using (new EditorGUILayout.HorizontalScope())
            {
                using (new EditorGUI.DisabledScope(t.state != TideTravelState.Live && !paused))
                {
                    if (GUILayout.Button(paused ? "Resume" : "Pause")) _ = paused ? runner.Resume() : runner.Pause();
                }
                using (new EditorGUI.DisabledScope(!paused))
                {
                    if (GUILayout.Button("Step")) runner.Step();
                    if (GUILayout.Button(new GUIContent("Continue From Here", "The match goes back to the tick shown and on from there: the ticks after it are gone, for every player"))) runner.ContinueFromHere();
                }
            }
            if (paused) Scrub(runner, t);
        }

        void Replaying(TideRunner runner, TideTravel t)
        {
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button(t.playing != 0 ? "Stop" : "Play")) _ = t.playing != 0 ? runner.Pause() : runner.Resume();
                if (GUILayout.Button("Step")) runner.Step();
                if (GUILayout.Button("Back to the Match")) runner.EndReplay();
            }
            Scrub(runner, t);
        }

        // A slider over the ticks: those before `kept` are made again from the start as they're shown
        static void Scrub(TideRunner runner, TideTravel t)
        {
            if (t.last <= t.first) return;
            var tick = EditorGUILayout.IntSlider("Tick", (int)t.shown, (int)t.first, (int)t.last);
            if (tick != (int)t.shown) runner.ShowTick((uint)tick);
            if (t.kept > t.first) EditorGUILayout.LabelField($"Ticks before {t.kept} are made again from the start as they're shown.", EditorStyles.miniLabel);
        }

        static void Recordings(TideRunner runner, TideTravel t)
        {
            using (new EditorGUILayout.HorizontalScope())
            {
                using (new EditorGUI.DisabledScope(t.state != TideTravelState.Live && t.state != TideTravelState.Paused))
                {
                    if (GUILayout.Button("Save Recording..."))
                    {
                        var path = EditorUtility.SaveFilePanel("Save the match's recording", "", "match." + Extension, Extension);
                        if (!string.IsNullOrEmpty(path)) runner.SaveRecording(path);
                    }
                    if (GUILayout.Button(new GUIContent("Replay This Match", "Its recording, replayed from its start: each tick made again and checked")))
                    {
                        runner.Replay(runner.Recording());
                    }
                }
                if (GUILayout.Button("Open Recording..."))
                {
                    var path = EditorUtility.OpenFilePanel("Replay a recording", "", Extension);
                    if (!string.IsNullOrEmpty(path) && File.Exists(path)) runner.ReplayFile(path);
                }
            }
        }
    }
}
