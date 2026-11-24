using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // The commands that turn a .tide file into a graph and a graph into its Tide: `tide graph`, with the tide the
    // package builds with (TideToolchain), or a tidec a test names. Each is one command line, made in one place
    // (ToGraphCommand, GraphTextCommand).
    static class TideGraphCommand
    {
        // A tidec to use instead of tide: the Sample's tests set it to a checkout's build
        internal static string TidecOverride;

        // The program to run, and whether it's a tidec (whose spelling of the commands is its own), or null with why
        static string Program(out bool tidec, out string why)
        {
            why = null;
            tidec = !string.IsNullOrEmpty(TidecOverride);
            if (tidec)
            {
                if (File.Exists(TidecOverride)) return Path.GetFullPath(TidecOverride);
                why = $"The tidec set for this session isn't there: {TidecOverride}";
                return null;
            }
            var tide = TideToolchain.Find(out var where, false);
            if (tide == null) why = $"Tide graphs need tide, and none was found: {where}";
            return tide;
        }

        // The command lines: the program and its arguments
        internal static bool ToGraphCommand(string tideFile, string graphFile, out string program, out string arguments, out string why)
        {
            program = Program(out var tidec, out why);
            arguments = tidec ? $"--to-graph {Quote(tideFile)} -o {Quote(graphFile)}" : $"graph {Quote(tideFile)} -o {Quote(graphFile)}";
            return program != null;
        }

        internal static bool GraphTextCommand(string graphFile, out string program, out string arguments, out string why)
        {
            program = Program(out var tidec, out why);
            arguments = tidec ? $"--graph-text {Quote(graphFile)}" : $"graph --text {Quote(graphFile)}";
            return program != null;
        }

        static string Quote(string s) => "\"" + s.Replace("\"", "\\\"") + "\"";

        // Runs a program to its end: its exit code and what it wrote, or -1 when it can't start or doesn't end in time
        public static int Run(string program, string arguments, string folder, out string output, out string errors, int timeoutMs = 60000)
        {
            var process = Start(program, arguments, folder, out var o, out var e, out var startError);
            if (process == null)
            {
                output = "";
                errors = startError;
                return -1;
            }
            using (process)
            {
                if (!process.WaitForExit(timeoutMs))
                {
                    try { process.Kill(); } catch (InvalidOperationException) { }
                    output = o.ToString();
                    errors = $"{Path.GetFileName(program)} didn't finish in {timeoutMs / 1000} s";
                    return -1;
                }
                process.WaitForExit();
                lock (o) output = o.ToString();
                lock (e) errors = e.ToString();
                return process.ExitCode;
            }
        }

        static Process Start(string program, string arguments, string folder, out StringBuilder output, out StringBuilder errors, out string why)
        {
            var o = output = new StringBuilder();
            var e = errors = new StringBuilder();
            why = null;
            var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = program,
                    Arguments = arguments,
                    WorkingDirectory = folder ?? TideSettings.ProjectFolder,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    StandardOutputEncoding = Encoding.UTF8,
                    StandardErrorEncoding = Encoding.UTF8,
                },
            };
            process.OutputDataReceived += (_, a) => { if (a.Data != null) lock (o) o.Append(a.Data).Append('\n'); };
            process.ErrorDataReceived += (_, a) => { if (a.Data != null) lock (e) e.Append(a.Data).Append('\n'); };
            try
            {
                process.Start();
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();
                return process;
            }
            catch (Exception x)
            {
                process.Dispose();
                why = $"can't start {program}: {x.Message}";
                return null;
            }
        }

        // Converts a .tide file into a graph beside it (or at `graphFile`): null when done, else what went wrong
        public static string Convert(string tideFile, string graphFile)
        {
            if (!ToGraphCommand(Path.GetFullPath(tideFile), Path.GetFullPath(graphFile), out var program, out var arguments, out var why)) return why;
            var code = Run(program, arguments, null, out var output, out var errors);
            if (code == 0 && File.Exists(graphFile)) return null;
            var said = (errors + output).Trim();
            return said.Length > 0 ? said : $"{Path.GetFileName(program)} stopped with exit code {code}";
        }
    }

    // A place in a graph tidec found something wrong with
    sealed class GraphDiagnostic
    {
        public int Id;            // The element it's about, 0 for none
        public string Severity;   // error, warning or note
        public string Message;
        public string Detail = ""; // What tidec wrote after it: the line of Tide it's in
    }

    sealed class GraphCheckResult
    {
        public string Text = "";   // The Tide the graph stands for
        public string Failure;     // Why there's no result: tidec missing, or it failed to run
        public readonly List<GraphDiagnostic> Diagnostics = new List<GraphDiagnostic>();
    }

    // What says what a graph's Tide is and what's wrong with it. tidec's lowering for now; the language server can
    // take its place for checked results.
    interface ITideGraphChecker
    {
        // Starts a check of the graph `json`, whose file is `path`; `done` is called on the editor's thread
        void Check(string path, string json, Action<GraphCheckResult> done);
    }

    // The node kinds and names a search offers in a graph: the catalog's for now; the language server can add what
    // it knows of the game's types and functions.
    interface ITideGraphSearch
    {
        IEnumerable<GraphSearchEntry> Entries(GraphSearchContext context);
    }

    static class TideGraphServices
    {
        public static ITideGraphChecker Checker = new TidecGraphChecker();
        public static readonly List<ITideGraphSearch> Searches = new List<ITideGraphSearch> { new GraphCatalog() };
    }

    // Writes the graph to a file of its own in Temp and runs `tidec --graph-text` on it
    sealed class TidecGraphChecker : ITideGraphChecker
    {
        static readonly Regex Line = new Regex(@"^(?<path>.*?):@(?<id>\d+): (?<severity>error|warning|note): (?<message>.*)$");
        static readonly Regex Plain = new Regex(@"^(?<path>.*?):(?:\d+:)*\s*(?<severity>error|warning|note): (?<message>.*)$");

        public void Check(string path, string json, Action<GraphCheckResult> done)
        {
            var result = new GraphCheckResult();
            if (!TideGraphCommand.GraphTextCommand("", out var program, out _, out var why))
            {
                result.Failure = why;
                done(result);
                return;
            }
            var folder = Path.Combine(TideSettings.ProjectFolder, "Temp", "TideGraphs");
            Directory.CreateDirectory(folder);
            var name = string.IsNullOrEmpty(path) ? "Graph.tidegraph" : Path.GetFileName(path);
            var file = Path.Combine(folder, Guid.NewGuid().ToString("N").Substring(0, 8) + "-" + name);
            File.WriteAllText(file, json, new UTF8Encoding(false));
            TideGraphCommand.GraphTextCommand(file, out program, out var arguments, out _);
            int code = 0;
            string output = "", errors = "";
            var thread = new Thread(() => code = TideGraphCommand.Run(program, arguments, folder, out output, out errors, 30000)) { IsBackground = true };
            thread.Start();
            void Wait()
            {
                if (thread.IsAlive) return;
                EditorApplication.update -= Wait;
                try { File.Delete(file); } catch (IOException) { }
                Parse(code, output, errors, result);
                done(result);
            }
            EditorApplication.update += Wait;
        }

        internal static void Parse(int code, string output, string errors, GraphCheckResult result)
        {
            result.Text = code == 0 ? output : "";
            GraphDiagnostic last = null;
            foreach (var raw in (errors ?? "").Split('\n'))
            {
                var line = raw.TrimEnd('\r');
                var m = Line.Match(line);
                if (m.Success)
                {
                    last = new GraphDiagnostic { Id = int.Parse(m.Groups["id"].Value), Severity = m.Groups["severity"].Value, Message = m.Groups["message"].Value };
                    result.Diagnostics.Add(last);
                    continue;
                }
                m = Plain.Match(line);
                if (m.Success && !line.StartsWith(" "))
                {
                    last = new GraphDiagnostic { Severity = m.Groups["severity"].Value, Message = m.Groups["message"].Value };
                    result.Diagnostics.Add(last);
                    continue;
                }
                if (last != null && line.Length > 0) last.Detail += (last.Detail.Length > 0 ? "\n" : "") + line;
            }
            if (code != 0 && result.Diagnostics.Count == 0)
            {
                var said = ((errors ?? "") + (output ?? "")).Trim();
                result.Failure = said.Length > 0 ? said : $"tidec stopped with exit code {code}";
            }
        }
    }
}
