using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;
using UnityEditor;

namespace Tide.Editor
{
    // Tide's language server (tidels) for the graph editor: one child process for the editor, spoken to in LSP's
    // JSON-RPC over its stdin and stdout. What it's sent goes out on a thread of its own, what it says is read on
    // another, and every answer is handed to the editor's thread (its update), so nothing waits on the server there.
    //
    // The documents are the graphs open in windows; the workspace is the project's Assets, so a graph there is read
    // with the game Unity's Tide builds (tidels finds a Unity project's game from its Assets folder), and a graph in a
    // folder with a tide.packages of its own with that folder's game.
    //
    // Work that changes what the server holds and then asks about it (a check, a completion) is an operation:
    // operations run one at a time, in the order they're queued. The server publishes a change's diagnostics before
    // it answers any request sent after that change, so a publication belongs to the change before the request whose
    // answer comes next. What graph editors share comes from it too: the graph schema, asked for when it starts
    // (GraphSchema.Adopt), and layouts, asked without a document.
    sealed class TideLanguageClient
    {
        // The one client of this editor, made when a graph first asks; null after it's given up or shut down
        static TideLanguageClient shared;

        public static TideLanguageClient Shared => shared;

        // The client, started if it isn't: null, with why, when there's no tidels to start
        public static TideLanguageClient Get(out string why)
        {
            why = null;
            if (shared != null && shared.program == Program(out _)) return shared;
            var program = Program(out why);
            if (program == null) return null;
            shared?.Shutdown();
            shared = new TideLanguageClient(program);
            return shared;
        }

        // tidels: the one a test names, the one beside a tidec a test names, or the one beside the tide the package
        // builds with
        internal static string Program(out string why)
        {
            why = null;
            var exe = UnityEngine.Application.platform == UnityEngine.RuntimePlatform.WindowsEditor ? "tidels.exe" : "tidels";
            if (!string.IsNullOrEmpty(TideGraphCommand.TidelsOverride))
            {
                if (File.Exists(TideGraphCommand.TidelsOverride)) return Path.GetFullPath(TideGraphCommand.TidelsOverride);
                why = $"The tidels set for this session isn't there: {TideGraphCommand.TidelsOverride}";
                return null;
            }
            string tide;
            if (!string.IsNullOrEmpty(TideGraphCommand.TidecOverride)) tide = TideGraphCommand.TidecOverride;
            else
            {
                tide = TideToolchain.Find(out var where, false);
                if (tide == null)
                {
                    why = $"No tide was found for the language server: {where}";
                    return null;
                }
            }
            var beside = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(tide)) ?? "", exe);
            if (File.Exists(beside)) return beside;
            why = $"There's no {exe} beside {tide}";
            return null;
        }

        // Stops the server for good: the editor is quitting or reloading its scripts. Waits a moment for it to go.
        public static void ShutdownAll()
        {
            var c = shared;
            shared = null;
            c?.Shutdown(true);
        }

        [InitializeOnLoadMethod]
        static void Hooks()
        {
            AssemblyReloadEvents.beforeAssemblyReload += ShutdownAll;
            EditorApplication.quitting += ShutdownAll;
        }

        // ---------------------------------------------------------------------------------------------------------

        sealed class Pending
        {
            public string Method;
            public double Deadline;
            public Action<JValue, string> Done;
        }

        sealed class Document
        {
            public string Text;         // The file as the window has it
            public int Version;
        }

        readonly string program;
        Process process;
        BlockingCollection<byte[]> outbox;
        readonly ConcurrentQueue<Action> inbox = new ConcurrentQueue<Action>();
        readonly Dictionary<int, Pending> pending = new Dictionary<int, Pending>();
        readonly Dictionary<string, Document> docs = new Dictionary<string, Document>();
        readonly Dictionary<string, JValue> diagnostics = new Dictionary<string, JValue>();
        readonly List<JValue> held = new List<JValue>(); // Publications whose change isn't known yet (reader's thread)
        readonly Queue<Action<Action>> operations = new Queue<Action<Action>>();
        readonly StringBuilder stderr = new StringBuilder();
        readonly List<double> starts = new List<double>();
        bool operating, stopping, gaveUp;
        double operationDeadline;
        int nextId = 1;
        int generation; // Of the process: what an old process's threads say is dropped

        public const double RequestTimeout = 30;

        // Why it's no use, when it isn't: tidels keeps stopping
        public string Failure { get; private set; }
        public bool Running => process != null && !HasExited(process);
        public int ProcessId => process != null && !HasExited(process) ? SafeId(process) : 0;
        public int Starts { get; private set; }
        public bool Usable => !gaveUp;
        // The last lines tidels wrote to its stderr
        public string Errors
        {
            get
            {
                lock (stderr) return stderr.ToString();
            }
        }

        // A document's diagnostics changed: its uri (on the editor's thread)
        public event Action<string> DiagnosticsChanged;

        TideLanguageClient(string program)
        {
            this.program = program;
            EditorApplication.update += Pump;
        }

        static bool HasExited(Process p)
        {
            try
            {
                return p.HasExited;
            }
            catch (InvalidOperationException)
            {
                return true;
            }
        }

        static int SafeId(Process p)
        {
            try
            {
                return p.Id;
            }
            catch (InvalidOperationException)
            {
                return 0;
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Uris

        public static string UriOf(string path)
        {
            var full = Path.GetFullPath(path).Replace('\\', '/');
            var sb = new StringBuilder("file://");
            if (!full.StartsWith("/")) sb.Append('/');
            foreach (var b in Encoding.UTF8.GetBytes(full))
            {
                var c = (char)b;
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || "-._~/:".IndexOf(c) >= 0) sb.Append(c);
                else sb.Append('%').Append(b.ToString("X2"));
            }
            return sb.ToString();
        }

        // Whether two uris name one file, however each spells it
        public static bool SameUri(string a, string b)
        {
            if (a == b) return true;
            if (a == null || b == null) return false;
            return string.Equals(Unescape(a), Unescape(b), UnityEngine.Application.platform == UnityEngine.RuntimePlatform.WindowsEditor ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
        }

        static string Unescape(string uri)
        {
            try
            {
                return Uri.UnescapeDataString(uri).Replace('\\', '/');
            }
            catch (UriFormatException)
            {
                return uri;
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Starting and stopping

        bool Start()
        {
            if (Running) return true;
            if (gaveUp) return false;
            var now = EditorApplication.timeSinceStartup;
            starts.RemoveAll(t => now - t > 60);
            if (starts.Count >= 4)
            {
                gaveUp = true;
                var said = Errors.Trim();
                Failure = "tidels keeps stopping" + (said.Length > 0 ? ": " + Last(said, 600) : "");
                return false;
            }
            starts.Add(now);
            generation++;
            var mine = generation;
            var p = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = program,
                    WorkingDirectory = TideSettings.ProjectFolder,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardInput = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    StandardInputEncoding = new UTF8Encoding(false),
                    StandardErrorEncoding = Encoding.UTF8,
                },
            };
            try
            {
                p.Start();
            }
            catch (Exception e)
            {
                p.Dispose();
                gaveUp = true;
                Failure = $"can't start {program}: {e.Message}";
                return false;
            }
            process = p;
            Starts++;
            stopping = false;
            var box = outbox = new BlockingCollection<byte[]>();
            var input = p.StandardInput.BaseStream;
            var output = p.StandardOutput.BaseStream;
            var errorReader = p.StandardError;
            new Thread(() => Write(box, input)) { IsBackground = true, Name = "tidels writer" }.Start();
            new Thread(() => Read(output, mine)) { IsBackground = true, Name = "tidels reader" }.Start();
            new Thread(() => ReadErrors(errorReader)) { IsBackground = true, Name = "tidels stderr" }.Start();

            // Its workspace: the project's Assets
            var folders = JValue.NewArray();
            var folder = JValue.NewObject();
            folder.Set("uri", JValue.Of(UriOf(Path.Combine(TideSettings.ProjectFolder, "Assets"))));
            folder.Set("name", JValue.Of("Assets"));
            folders.Items.Add(folder);
            var init = JValue.NewObject();
            init.Set("processId", JValue.Of(SafeId(Process.GetCurrentProcess())));
            init.Set("rootUri", JValue.Of(UriOf(TideSettings.ProjectFolder)));
            init.Set("workspaceFolders", folders);
            init.Set("capabilities", JValue.NewObject());
            init.Set("clientInfo", Obj("name", JValue.Of("Tide's graph editor")));
            SendRequest("initialize", init, (_, __) => { });
            Notify("initialized", JValue.NewObject());
            // The schema of graphs this tidels has, which is the one every graph editor reads
            SendRequest("tide/graphSchema", JValue.Null(), (schema, why) =>
            {
                if (why == null) GraphSchema.Adopt(schema, "tidels");
            });
            // The documents it had before it stopped
            foreach (var pair in docs) Notify("textDocument/didOpen", Obj("textDocument", OpenItem(pair.Key, pair.Value)));
            return true;
        }

        static string Last(string s, int n) => s.Length <= n ? s : "…" + s.Substring(s.Length - n);

        // Shuts the server down: asks it to, and kills it if it doesn't go. `wait`: waits for that here (a reload or
        // quit, after which nothing would)
        public void Shutdown(bool wait = false)
        {
            EditorApplication.update -= Pump;
            if (shared == this) shared = null;
            var p = process;
            var box = outbox;
            process = null;
            stopping = true;
            FailAll("tidels was shut down");
            if (p == null) return;
            try
            {
                if (!HasExited(p))
                {
                    box?.Add(Frame(Message("shutdown", JValue.Null(), nextId++)));
                    box?.Add(Frame(Message("exit", null, 0)));
                }
            }
            catch (InvalidOperationException)
            {
                // Its writer has stopped
            }
            box?.CompleteAdding();
            void Finish()
            {
                try
                {
                    if (!p.WaitForExit(2000)) p.Kill();
                }
                catch (Exception)
                {
                    // Gone already
                }
                p.Dispose();
            }
            if (wait) Finish();
            else new Thread(Finish) { IsBackground = true, Name = "tidels shutdown" }.Start();
        }

        // ---------------------------------------------------------------------------------------------------------
        // The wire

        static JValue Obj(string key, JValue value)
        {
            var o = JValue.NewObject();
            o.Set(key, value);
            return o;
        }

        static JValue Message(string method, JValue parameters, int id)
        {
            var m = JValue.NewObject();
            m.Set("jsonrpc", JValue.Of("2.0"));
            if (id != 0) m.Set("id", JValue.Of(id));
            m.Set("method", JValue.Of(method));
            if (parameters != null) m.Set("params", parameters);
            return m;
        }

        static byte[] Frame(JValue message)
        {
            var body = Encoding.UTF8.GetBytes(Compact(message));
            var head = Encoding.ASCII.GetBytes($"Content-Length: {body.Length}\r\n\r\n");
            var all = new byte[head.Length + body.Length];
            Buffer.BlockCopy(head, 0, all, 0, head.Length);
            Buffer.BlockCopy(body, 0, all, head.Length, body.Length);
            return all;
        }

        // JSON on one line, as messages are written
        internal static string Compact(JValue v)
        {
            var sb = new StringBuilder();
            Compact(sb, v);
            return sb.ToString();
        }

        static void Compact(StringBuilder sb, JValue v)
        {
            switch (v.Kind)
            {
                case JKind.Array:
                    sb.Append('[');
                    for (var i = 0; i < v.Items.Count; i++)
                    {
                        if (i > 0) sb.Append(',');
                        Compact(sb, v.Items[i]);
                    }
                    sb.Append(']');
                    return;
                case JKind.Object:
                    sb.Append('{');
                    for (var i = 0; i < v.Keys.Count; i++)
                    {
                        if (i > 0) sb.Append(',');
                        sb.Append(GraphJson.Write(JValue.Of(v.Keys[i]))).Append(':');
                        Compact(sb, v.Values[i]);
                    }
                    sb.Append('}');
                    return;
                default:
                    sb.Append(GraphJson.Write(v));
                    return;
            }
        }

        static void Write(BlockingCollection<byte[]> box, Stream input)
        {
            try
            {
                foreach (var bytes in box.GetConsumingEnumerable())
                {
                    input.Write(bytes, 0, bytes.Length);
                    input.Flush();
                }
                input.Close();
            }
            catch (Exception)
            {
                // The server has gone: the editor's thread sees it and starts another
            }
        }

        void ReadErrors(StreamReader r)
        {
            try
            {
                string line;
                while ((line = r.ReadLine()) != null)
                {
                    lock (stderr)
                    {
                        stderr.Append(line).Append('\n');
                        if (stderr.Length > 8000) stderr.Remove(0, stderr.Length - 6000);
                    }
                }
            }
            catch (Exception)
            {
                // Its stream closed
            }
        }

        void Read(Stream output, int mine)
        {
            var buffer = new byte[65536];
            var data = new MemoryStream();
            try
            {
                while (true)
                {
                    var n = output.Read(buffer, 0, buffer.Length);
                    if (n <= 0) break;
                    data.Write(buffer, 0, n);
                    while (TakeMessage(data, out var body)) Handle(body, mine);
                }
            }
            catch (Exception)
            {
                // The server has gone
            }
        }

        // A whole message from what's been read, if one is there
        static bool TakeMessage(MemoryStream data, out string body)
        {
            body = null;
            var bytes = data.GetBuffer();
            var length = (int)data.Length;
            var end = -1;
            for (var i = 0; i + 3 < length; i++)
            {
                if (bytes[i] == '\r' && bytes[i + 1] == '\n' && bytes[i + 2] == '\r' && bytes[i + 3] == '\n')
                {
                    end = i;
                    break;
                }
            }
            if (end < 0) return false;
            var head = Encoding.ASCII.GetString(bytes, 0, end);
            var size = -1;
            foreach (var line in head.Split('\n'))
            {
                var l = line.Trim();
                if (l.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase) && int.TryParse(l.Substring(15).Trim(), out var s)) size = s;
            }
            if (size < 0)
            {
                // Not a header: skip it
                Shift(data, end + 4);
                return true;
            }
            if (length < end + 4 + size) return false;
            body = Encoding.UTF8.GetString(bytes, end + 4, size);
            Shift(data, end + 4 + size);
            return true;
        }

        static void Shift(MemoryStream data, int count)
        {
            var bytes = data.GetBuffer();
            var rest = (int)data.Length - count;
            Buffer.BlockCopy(bytes, count, bytes, 0, rest);
            data.SetLength(rest);
        }

        // A message from the server, on the reader's thread
        void Handle(string body, int mine)
        {
            if (body == null) return;
            JValue msg;
            try
            {
                msg = GraphJson.Parse(body);
            }
            catch (GraphJson.ParseError)
            {
                return;
            }
            var method = msg.Str("method");
            var id = msg.Get("id");
            if (method != null)
            {
                if (method == "textDocument/publishDiagnostics")
                {
                    lock (held) held.Add(msg.Get("params"));
                }
                else if (id != null && !id.IsNull)
                {
                    // A request of the server's (it registers nothing with a client that says it takes nothing): no
                    var reply = JValue.NewObject();
                    reply.Set("jsonrpc", JValue.Of("2.0"));
                    reply.Set("id", id);
                    reply.Set("result", JValue.Null());
                    try
                    {
                        if (generation == mine) outbox?.Add(Frame(reply));
                    }
                    catch (InvalidOperationException)
                    {
                        // Shutting down
                    }
                }
                return;
            }
            var number = JValue.AsId(id);
            List<JValue> publications;
            lock (held)
            {
                publications = new List<JValue>(held);
                held.Clear();
            }
            var result = msg.Get("result");
            var error = msg.Get("error");
            inbox.Enqueue(() =>
            {
                if (generation != mine) return;
                pending.TryGetValue(number, out var p);
                if (p != null) pending.Remove(number);
                // What it published before this answer was about the change before this request
                foreach (var d in publications) Publish(d);
                p?.Done(error != null ? null : result ?? JValue.Null(), error != null ? error.Str("message") ?? "the request failed" : null);
            });
        }

        void Publish(JValue parameters)
        {
            var uri = parameters?.Str("uri");
            if (uri == null) return;
            var list = parameters.Arr("diagnostics") ?? JValue.NewArray();
            string key = null;
            foreach (var k in docs.Keys) if (SameUri(k, uri)) key = k;
            if (key == null) return;
            diagnostics[key] = list;
            DiagnosticsChanged?.Invoke(key);
        }

        // The editor's thread: answers, timeouts, and a server that stopped
        void Pump()
        {
            while (inbox.TryDequeue(out var a))
            {
                try
                {
                    a();
                }
                catch (Exception e)
                {
                    UnityEngine.Debug.LogException(e);
                }
            }
            var now = EditorApplication.timeSinceStartup;
            if (pending.Count > 0)
            {
                List<int> late = null;
                foreach (var pair in pending) if (now > pair.Value.Deadline) (late ??= new List<int>()).Add(pair.Key);
                if (late != null)
                    foreach (var id in late)
                    {
                        var p = pending[id];
                        pending.Remove(id);
                        p.Done(null, $"tidels didn't answer {p.Method} in {RequestTimeout} s");
                    }
            }
            // Publications no request will claim: a change's with nothing asked after it
            if (pending.Count == 0)
            {
                List<JValue> publications = null;
                lock (held)
                {
                    if (held.Count > 0)
                    {
                        publications = new List<JValue>(held);
                        held.Clear();
                    }
                }
                if (publications != null) foreach (var d in publications) Publish(d);
            }
            if (process != null && !stopping && HasExited(process))
            {
                // It stopped: requests waiting fail, and it starts again for the documents open
                var p = process;
                process = null;
                try
                {
                    outbox?.CompleteAdding();
                }
                catch (ObjectDisposedException)
                {
                }
                p.Dispose();
                FailAll("tidels stopped");
                lock (held) held.Clear();
                if (docs.Count > 0) Start();
            }
            if (operating && now > operationDeadline)
            {
                // An operation that never said it was done: the next goes on
                operating = false;
                NextOperation();
            }
        }

        void FailAll(string why)
        {
            if (pending.Count == 0) return;
            var all = new List<Pending>(pending.Values);
            pending.Clear();
            foreach (var p in all) p.Done(null, why);
        }

        void SendRequest(string method, JValue parameters, Action<JValue, string> done)
        {
            var id = nextId++;
            pending[id] = new Pending { Method = method, Deadline = EditorApplication.timeSinceStartup + RequestTimeout, Done = done };
            Send(Message(method, parameters, id));
        }

        void Notify(string method, JValue parameters) => Send(Message(method, parameters, 0));

        void Send(JValue message)
        {
            try
            {
                outbox?.Add(Frame(message));
            }
            catch (InvalidOperationException)
            {
                // It's stopping: the next pump starts it again
            }
        }

        static JValue OpenItem(string uri, Document d)
        {
            var item = JValue.NewObject();
            item.Set("uri", JValue.Of(uri));
            item.Set("languageId", JValue.Of("tidegraph"));
            item.Set("version", JValue.Of(d.Version));
            item.Set("text", JValue.Of(d.Text));
            return item;
        }

        // ---------------------------------------------------------------------------------------------------------
        // What graphs ask

        // Runs an operation when those before it are done: it's given the action that says it's done
        public void Operate(Action<Action> operation)
        {
            operations.Enqueue(operation);
            if (!operating) NextOperation();
        }

        void NextOperation()
        {
            while (!operating && operations.Count > 0)
            {
                var op = operations.Dequeue();
                operating = true;
                operationDeadline = EditorApplication.timeSinceStartup + 3 * RequestTimeout;
                var finished = false;
                try
                {
                    op(() =>
                    {
                        if (finished) return;
                        finished = true;
                        operating = false;
                        // Not inside the operation's own call: the next starts on the next pump
                        inbox.Enqueue(NextOperation);
                    });
                }
                catch (Exception e)
                {
                    finished = true;
                    operating = false;
                    UnityEngine.Debug.LogException(e);
                }
            }
        }

        // Sends a document's text, the file's as the window has it: opens it if the server hasn't it. False when the
        // server can't be started.
        public bool SetText(string uri, string text)
        {
            if (!Start()) return false;
            if (!docs.TryGetValue(uri, out var d))
            {
                d = docs[uri] = new Document { Text = text, Version = 1 };
                Notify("textDocument/didOpen", Obj("textDocument", OpenItem(uri, d)));
                return true;
            }
            if (d.Text == text) return true;
            d.Version++;
            d.Text = text;
            var p = JValue.NewObject();
            var item = JValue.NewObject();
            item.Set("uri", JValue.Of(uri));
            item.Set("version", JValue.Of(d.Version));
            p.Set("textDocument", item);
            var changes = JValue.NewArray();
            changes.Items.Add(Obj("text", JValue.Of(text)));
            p.Set("contentChanges", changes);
            Notify("textDocument/didChange", p);
            return true;
        }

        public bool IsOpen(string uri) => docs.ContainsKey(uri);

        public string TextOf(string uri) => docs.TryGetValue(uri, out var d) ? d.Text : null;

        // A request about a document, at an offset into its Tide (or none, -1): `done` gets its result, or null and why
        public void Ask(string method, string uri, int offset, Action<JValue, string> done)
        {
            var p = JValue.NewObject();
            if (offset >= 0) p.Set("offset", JValue.Of(offset));
            Ask(method, uri, p, done);
        }

        // A request about a document with parameters of its own (tide/graphMembers' node)
        public void Ask(string method, string uri, JValue parameters, Action<JValue, string> done)
        {
            if (!Start())
            {
                done(null, Failure ?? "tidels isn't running");
                return;
            }
            var p = Obj("textDocument", Obj("uri", JValue.Of(uri)));
            for (var i = 0; parameters != null && parameters.IsObject && i < parameters.Keys.Count; i++) p.Set(parameters.Keys[i], parameters.Values[i]);
            SendRequest(method, p, done);
        }

        // A request about no document (tide/graphLayout): when no graph is open, the server stops again after it
        public void Request(string method, JValue parameters, Action<JValue, string> done)
        {
            if (!Start())
            {
                done(null, Failure ?? "tidels isn't running");
                return;
            }
            SendRequest(method, parameters, (result, why) =>
            {
                if (docs.Count == 0 && pending.Count == 0) Shutdown();
                done(result, why);
            });
        }

        // The diagnostics it last published for a document's real text
        public JValue DiagnosticsOf(string uri) => diagnostics.TryGetValue(uri, out var d) ? d : null;

        // A window closed its graph: when it was the last, the server stops
        public void Close(string uri)
        {
            if (!docs.Remove(uri)) return;
            diagnostics.Remove(uri);
            if (Running) Notify("textDocument/didClose", Obj("textDocument", Obj("uri", JValue.Of(uri))));
            if (docs.Count == 0) Shutdown();
        }

        // Files Unity imported, moved or deleted: the server reads them again
        public void FilesChanged(IEnumerable<string> paths, IEnumerable<string> deleted)
        {
            if (!Running) return;
            var changes = JValue.NewArray();
            void Add(string path, int type)
            {
                var c = JValue.NewObject();
                c.Set("uri", JValue.Of(UriOf(Path.Combine(TideSettings.ProjectFolder, path))));
                c.Set("type", JValue.Of(type));
                changes.Items.Add(c);
            }
            bool Tide(string path)
            {
                var extension = Path.GetExtension(path).ToLowerInvariant();
                return extension == ".tide" || extension == ".tidegraph" || extension == ".packages" || extension == ".tprefab";
            }
            foreach (var p in paths) if (Tide(p)) Add(p, 2);
            foreach (var p in deleted) if (Tide(p)) Add(p, 3);
            if (changes.Items.Count > 0) Notify("workspace/didChangeWatchedFiles", Obj("changes", changes));
        }
    }
}
