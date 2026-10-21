using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace Tide.Editor
{
    // A .tide file's inspector, over its text: whose it is, what the last build said of it, and what it declares as
    // the newest build has it (outline.json): each type's fields and defaults, what each system reads, writes and
    // waits for and why, and each event's handlers. In Play, how many shown entities have each of its components and
    // its singletons' values. Each line opens the file there.
    [CustomEditor(typeof(TideSourceImporter))]
    sealed class TideSourceImporterEditor : ScriptedImporterEditor
    {
        protected override bool needsApplyRevert => false;

        public override bool RequiresConstantRepaint() => EditorApplication.isPlaying;

        static readonly (string kind, string title)[] groups =
        {
            ("scene", "Scenes"), ("component", "Components"), ("singleton", "Singletons"), ("input", "Input"),
            ("event", "Events"), ("command", "Commands"), ("system", "Systems"), ("handler", "Event handlers"),
            ("view", "Views"), ("localHandler", "Local handlers"), ("struct", "Structs"), ("enum", "Enums"),
            ("const", "Constants"), ("function", "Functions"), ("audio", "Audio"), ("shader", "Shaders"),
            ("asset", "Assets"), ("prefab", "Prefabs"), ("settings", "Settings"),
        };

        static readonly Dictionary<string, bool> open = new Dictionary<string, bool>();

        public override void OnInspectorGUI()
        {
            var asset = ((AssetImporter)target).assetPath;
            var outline = TideOutline.Current;
            EditorGUILayout.LabelField(Owner(outline, asset), EditorStyles.miniLabel);
            Problems(asset);
            if (outline == null)
            {
                EditorGUILayout.HelpBox("What this file declares shows once tide has built the game (Project Settings > Tide).",
                                        MessageType.Info);
                return;
            }
            var declared = outline.In(asset);
            if (declared.Count == 0)
            {
                EditorGUILayout.LabelField(TideBuildService.HasErrors ? "Declarations show once the game builds again." : "It declares nothing.",
                                           EditorStyles.miniLabel);
                return;
            }
            var schema = TideEditorSchema.Current;
            var runner = EditorApplication.isPlaying && TideRunner.Current != null && TideRunner.Current.Running ? TideRunner.Current : null;
            var unfold = declared.Count <= 6; // A small file's details show at first
            foreach (var (kind, title) in groups)
            {
                var these = declared.Where(d => d.kind == kind).ToList();
                if (these.Count == 0) continue;
                EditorGUILayout.Space(4);
                EditorGUILayout.LabelField(title, EditorStyles.boldLabel);
                foreach (var d in these) Declaration(asset, d, schema, runner, unfold);
            }
            EditorGUILayout.Space(4);
        }

        // "The game's", or the package's whose folder it's in
        static string Owner(TideOutline outline, string asset)
        {
            if (outline != null)
            {
                foreach (var p in outline.packages)
                {
                    if (string.IsNullOrEmpty(p.asset) || !asset.StartsWith(p.asset.TrimEnd('/') + "/", StringComparison.OrdinalIgnoreCase)) continue;
                    return $"Package {p.name}'s";
                }
            }
            return "The game's";
        }

        // The last build's errors and warnings in this file
        static void Problems(string asset)
        {
            foreach (var message in TideBuildService.LastMessages)
            {
                if (!TideBuildService.Locate(message, out var file, out var line, out var column) ||
                    !string.Equals(file, asset, StringComparison.OrdinalIgnoreCase)) continue;
                var first = message.Split('\n')[0];
                var error = !first.Contains(": warning");
                var at = first.IndexOf(error ? ": error" : ": warning", StringComparison.Ordinal);
                var text = at >= 0 ? first.Substring(at + 2) : first;
                var icon = EditorGUIUtility.IconContent(error ? "console.erroricon.sml" : "console.warnicon.sml").image;
                var content = new GUIContent($"line {line}: {text}", icon, message);
                var rect = EditorGUILayout.GetControlRect(false, EditorGUIUtility.singleLineHeight);
                EditorGUIUtility.AddCursorRect(rect, MouseCursor.Link);
                if (GUI.Button(rect, content, EditorStyles.label)) Open(asset, line, column);
            }
        }

        static void Open(string asset, int line, int column)
        {
            var file = AssetDatabase.LoadMainAssetAtPath(asset);
            if (file != null) AssetDatabase.OpenAsset(file, line, column);
        }

        static void Declaration(string asset, TideOutlineDeclaration d, TideSchema schema, TideRunner runner, bool unfold)
        {
            var key = asset + "|" + d.kind + "|" + d.name;
            if (!open.TryGetValue(key, out var expanded)) expanded = unfold;
            var label = d.name + (d.local ? "  (local)" : "") + (d.async ? "  (async)" : "") + (d.@extern ? "  (C)" : "");
            var live = Live(d, schema, runner);
            if (live != null) label += "   " + live;
            var rect = EditorGUILayout.GetControlRect();
            var link = new GUIContent("line " + d.line);
            var linkWidth = EditorStyles.linkLabel.CalcSize(link).x;
            var linkRect = new Rect(rect.xMax - linkWidth, rect.y, linkWidth, rect.height);
            var nameRect = new Rect(rect.x, rect.y, rect.width - linkWidth - 4, rect.height);
            var details = HasDetails(d);
            if (details) expanded = EditorGUI.Foldout(nameRect, expanded, label, true);
            else EditorGUI.LabelField(nameRect, new GUIContent(label));
            open[key] = expanded;
            EditorGUIUtility.AddCursorRect(linkRect, MouseCursor.Link);
            if (GUI.Button(linkRect, link, EditorStyles.linkLabel)) Open(asset, d.line, d.column);
            if (!details || !expanded) return;
            using (new EditorGUI.IndentLevelScope())
            {
                switch (d.kind)
                {
                case "system":
                case "view":
                case "handler":
                case "localHandler":
                    Runs(d);
                    break;
                case "enum":
                    Line("Members", string.Join(", ", d.members));
                    break;
                default:
                    Fields(d, schema, runner);
                    if (d.handlers.Length > 0) Line("Handled by", string.Join(", ", d.handlers));
                    if (d.localHandlers.Length > 0) Line("Heard by", string.Join(", ", d.localHandlers) + " (local)");
                    break;
                }
            }
        }

        static bool HasDetails(TideOutlineDeclaration d)
        {
            switch (d.kind)
            {
            case "system":
            case "view":
            case "handler":
            case "localHandler":
                return true;
            case "enum":
                return d.members.Length > 0;
            default:
                return d.fields.Length > 0 || d.handlers.Length > 0 || d.localHandlers.Length > 0;
            }
        }

        // In Play: how many shown entities have a component, or that a singleton is the match's
        static string Live(TideOutlineDeclaration d, TideSchema schema, TideRunner runner)
        {
            if (runner == null || schema == null || d.local || d.kind != "component" && d.kind != "scene") return null;
            var index = schema.TypeIndex(d.name);
            if (index < 0) return null;
            var count = runner.Packet.Count((uint)index, out _);
            return count == 1 ? "1 entity" : $"{count} entities";
        }

        static void Fields(TideOutlineDeclaration d, TideSchema schema, TideRunner runner)
        {
            var type = schema?.Type(schema.TypeIndex(d.name));
            var defaults = type?.DefaultBytes();
            var values = d.kind == "singleton" && runner != null && type != null ? SingletonBytes(runner, schema.TypeIndex(d.name), type.size) : null;
            foreach (var f in d.fields)
            {
                var field = f.local ? null : type?.Field(f.name);
                string value = null;
                if (field != null && values != null) value = Shown(schema, field, values, runner);
                else if (field != null && defaults != null) value = TideEntityEditor.Default(schema, field, defaults);
                var text = f.type + (value != null ? (values != null ? "   " : "   = ") + value : "") + (f.local ? "   local" : "");
                Line(f.name, text);
            }
        }

        static unsafe byte[] SingletonBytes(TideRunner runner, int index, uint size)
        {
            var at = runner.Packet.Singleton((uint)index, out var stride);
            if (at == null || stride < size) return null;
            var bytes = new byte[size];
            for (var i = 0; i < bytes.Length; i++) bytes[i] = at[i];
            return bytes;
        }

        // A live value: as a literal where it's a number, a vector, a color or an enum; text as it reads
        static string Shown(TideSchema schema, TideSchemaField field, byte[] bytes, TideRunner runner)
        {
            if (field.kind == "text" && field.offset + 4 <= bytes.Length)
            {
                return "\"" + runner.Packet.Text(BitConverter.ToUInt32(bytes, (int)field.offset)) + "\"";
            }
            if (field.kind == "entity" && field.offset + 8 <= bytes.Length)
            {
                var generation = BitConverter.ToUInt32(bytes, (int)field.offset + 4);
                return generation == 0 ? "none" : $"entity {BitConverter.ToUInt32(bytes, (int)field.offset)}";
            }
            return TideEntityEditor.Default(schema, field, bytes);
        }

        // What a system, view or handler does: when it runs, what it reads and writes, and in the tick what it
        // waits for and why, and what runs alongside it
        static void Runs(TideOutlineDeclaration d)
        {
            var each = d.perEntity ? "for each entity it matches" : "once";
            switch (d.kind)
            {
            case "system":
                Line("Runs", $"each tick, {each}, at stage {d.stage}");
                break;
            case "view":
                Line("Runs", $"each frame, {each}, on this machine");
                break;
            case "handler":
                Line("Runs", $"when {d.@event} is sent" + (d.perEntity ? ", for the entity it's sent to" : ""));
                break;
            default:
                Line("Runs", $"on this machine, at the start of a frame, for each {d.@event} the ticks since sent");
                break;
            }
            var reads = d.access.Where(a => !a.every && !a.writes).Select(a => a.name).ToArray();
            var writes = d.access.Where(a => !a.every && a.writes).Select(a => a.name).ToArray();
            var anyReads = d.access.Where(a => a.every && !a.writes).Select(a => a.name).ToArray();
            var anyWrites = d.access.Where(a => a.every && a.writes).Select(a => a.name).ToArray();
            if (reads.Length > 0) Line("Reads", string.Join(", ", reads));
            if (writes.Length > 0) Line("Writes", string.Join(", ", writes));
            if (anyReads.Length > 0) Line("Reads any", string.Join(", ", anyReads) + " (World, queries)");
            if (anyWrites.Length > 0) Line("Writes any", string.Join(", ", anyWrites) + " (World)");
            if (d.with.Length > 0) Line("With", string.Join(", ", d.with));
            if (d.without.Length > 0) Line("Without", string.Join(", ", d.without));
            foreach (var w in d.after) Line("Waits for", $"{w.name}: {w.why}");
            if (d.afterMore > 0) Line("Waits for", $"{d.afterMore} more, through those");
            if (d.kind == "system")
            {
                var alongside = d.alongside.Length == 0 ? "nothing" : string.Join(", ", d.alongside);
                if (d.alongsideMore > 0) alongside += $" and {d.alongsideMore} more";
                Line("Alongside", alongside);
            }
        }

        static GUIStyle wrapped;

        static void Line(string label, string text)
        {
            wrapped = wrapped ?? new GUIStyle(EditorStyles.label) { wordWrap = true };
            EditorGUILayout.LabelField(label, text, wrapped);
        }
    }

    // A tide.packages' inspector, over its text. The game's (Assets/tide.packages): its packages as the newest build
    // found them, where each comes from, and Add, Update and Remove, which run tide add and tide update or take the
    // line out. A package's own (`package Name`): its name, and what of it the game builds.
    [CustomEditor(typeof(TidePackagesImporter))]
    sealed class TidePackagesImporterEditor : ScriptedImporterEditor
    {
        protected override bool needsApplyRevert => false;

        static string adding = "";
        static string running;  // What tide is doing for the inspector, while it runs
        static string result;   // ...and what it said when it failed
        static MessageType resultType;

        public override void OnInspectorGUI()
        {
            var asset = ((AssetImporter)target).assetPath;
            if (Path.GetFileName(asset) != "tide.packages") return; // Another .packages file is text
            var folder = Path.GetDirectoryName(asset)?.Replace('\\', '/');
            var outline = TideOutline.Current;
            if (folder != "Assets")
            {
                Own(folder, outline);
                return;
            }
            EditorGUILayout.LabelField("The game's packages", EditorStyles.boldLabel);
            if (outline == null)
            {
                EditorGUILayout.HelpBox("Its packages show once tide has built the game (Project Settings > Tide).", MessageType.Info);
            }
            else if (outline.packages.Length == 0)
            {
                EditorGUILayout.LabelField("None yet: add one below.", EditorStyles.miniLabel);
            }
            else
            {
                foreach (var p in outline.packages.OrderBy(p => p.kind == "found").ThenBy(p => p.name)) Package(asset, p);
            }
            EditorGUILayout.Space(6);
            Add(outline);
            if (running != null) EditorGUILayout.HelpBox(running, MessageType.None);
            else if (!string.IsNullOrEmpty(result)) EditorGUILayout.HelpBox(result, resultType);
            EditorGUILayout.Space(4);
        }

        static void Package(string asset, TideOutlinePackage p)
        {
            using (new EditorGUILayout.HorizontalScope())
            {
                EditorGUILayout.LabelField(new GUIContent(p.name, p.folder), EditorStyles.boldLabel, GUILayout.Width(EditorGUIUtility.labelWidth));
                EditorGUILayout.LabelField(Where(p), EditorStyles.label);
                using (new EditorGUI.DisabledScope(running != null))
                {
                    if (p.kind == "found" || p.kind == "local")
                    {
                        var shown = AssetDatabase.LoadMainAssetAtPath(p.asset ?? "");
                        using (new EditorGUI.DisabledScope(shown == null))
                        {
                            if (GUILayout.Button("Show", EditorStyles.miniButton, GUILayout.Width(52))) EditorGUIUtility.PingObject(shown);
                        }
                    }
                    if (p.kind == "git" && GUILayout.Button(new GUIContent("Update", $"tide update {p.name}: to the newest commit of what it follows"),
                                                             EditorStyles.miniButton, GUILayout.Width(52)))
                    {
                        Run($"update {p.name}", $"Updating {p.name}...");
                    }
                    if (p.line > 0 && GUILayout.Button(new GUIContent("Remove", "Takes its line out of tide.packages"), EditorStyles.miniButton, GUILayout.Width(58)))
                    {
                        Remove(asset, p);
                    }
                }
            }
        }

        // Where it comes from: "comes with tide", its repository at its commit, or its folder
        static string Where(TideOutlinePackage p)
        {
            switch (p.kind)
            {
            case "builtin": return "comes with tide";
            case "found": return "this project's " + p.asset + " (its tide.packages says package " + p.name + ")";
            case "local": return "the folder " + p.source;
            default:
                var commit = string.IsNullOrEmpty(p.commit) ? "not fetched yet" : p.commit.Substring(0, Math.Min(8, p.commit.Length));
                return p.source + (string.IsNullOrEmpty(p.@ref) ? "" : "@" + p.@ref) + " at " + commit;
            }
        }

        static void Add(TideOutline outline)
        {
            using (new EditorGUILayout.HorizontalScope())
            {
                adding = EditorGUILayout.TextField(new GUIContent("Add a package",
                    "A package that comes with tide by its name (rooms), a git repository (github.com/owner/repo, @branch to follow one), " +
                    "or a folder (../shared)"), adding);
                var builtin = outline?.builtin ?? Array.Empty<string>();
                var listed = new HashSet<string>(outline?.packages.Select(p => p.name.ToLowerInvariant()) ?? Enumerable.Empty<string>());
                if (builtin.Length > 0 && GUILayout.Button(new GUIContent("▾", "The packages that come with tide"), EditorStyles.miniButton, GUILayout.Width(22)))
                {
                    var menu = new GenericMenu();
                    foreach (var name in builtin)
                    {
                        if (listed.Contains(name)) menu.AddDisabledItem(new GUIContent(name));
                        else menu.AddItem(new GUIContent(name), false, () => adding = name);
                    }
                    menu.ShowAsContext();
                }
                using (new EditorGUI.DisabledScope(running != null || string.IsNullOrWhiteSpace(adding)))
                {
                    if (GUILayout.Button("Add", EditorStyles.miniButton, GUILayout.Width(52)))
                    {
                        var source = adding.Trim();
                        Run($"add \"{source}\"", $"Adding {source}...", () => adding = "");
                    }
                }
            }
        }

        // tide add or update in the Assets folder, which then imports tide.packages again, so the game builds
        static void Run(string arguments, string doing, Action succeeded = null)
        {
            running = doing;
            result = null;
            TideToolchain.Run(arguments, TideSettings.instance.GameFolderPath, (code, said) =>
            {
                running = null;
                if (code == 0)
                {
                    result = null;
                    succeeded?.Invoke();
                }
                else
                {
                    result = string.IsNullOrEmpty(said) ? $"tide {arguments} failed ({code})" : said;
                    resultType = MessageType.Error;
                }
                AssetDatabase.ImportAsset("Assets/tide.packages");
                UnityEditorInternal.InternalEditorUtility.RepaintAllViews();
            });
        }

        // Takes its line out of the file, as an edit by hand would
        static void Remove(string asset, TideOutlinePackage p)
        {
            var text = File.ReadAllText(asset);
            var newline = text.Contains("\r\n") ? "\r\n" : "\n";
            var lines = text.Split('\n').ToList();
            if (p.line - 1 >= lines.Count) return;
            lines.RemoveAt(p.line - 1);
            File.WriteAllText(asset, string.Join("\n", lines.Select(l => l.TrimEnd('\r'))).Replace("\n", newline));
            AssetDatabase.ImportAsset(asset);
            result = $"Took {p.name} out. What only it needed stays listed until you take that out too.";
            resultType = MessageType.Info;
        }

        // A package's own tide.packages: its name, whether the game builds it, and its files
        static void Own(string folder, TideOutline outline)
        {
            var package = outline?.packages.FirstOrDefault(p => string.Equals(p.asset?.TrimEnd('/'), folder, StringComparison.OrdinalIgnoreCase));
            var name = package?.name ?? NameIn(folder + "/tide.packages");
            if (name == null)
            {
                EditorGUILayout.HelpBox("A game's tide.packages goes in the Assets folder: the game is the whole of Assets. " +
                                        "Start this one with `package Name` to make its folder a package.", MessageType.Warning);
                return;
            }
            EditorGUILayout.LabelField("Package " + name, EditorStyles.boldLabel);
            EditorGUILayout.LabelField(package != null ? "The game builds it: its folder is found as an assembly definition is." :
                                       outline == null ? "Whether the game builds it shows once tide has built the game." :
                                       "The game doesn't build it yet: it's found as the game next builds.", EditorStyles.wordWrappedMiniLabel);
            var files = Directory.Exists(folder) ? Directory.GetFiles(folder, "*", SearchOption.AllDirectories) : Array.Empty<string>();
            var tide = files.Count(f => f.EndsWith(".tide", StringComparison.OrdinalIgnoreCase));
            var c = files.Count(f => new[] { ".c", ".cpp", ".cc", ".cxx" }.Contains(Path.GetExtension(f).ToLowerInvariant()));
            EditorGUILayout.LabelField("Files", $"{tide} .tide" + (c > 0 ? $", {c} C or C++ (built with it)" : ""));
            EditorGUILayout.Space(4);
        }

        // The name its `package` line gives, before any other line: null for none
        static string NameIn(string file)
        {
            if (!File.Exists(file)) return null;
            foreach (var line in File.ReadLines(file))
            {
                var text = line.Split('#')[0].Trim();
                if (text.Length == 0) continue;
                return text.StartsWith("package ") ? text.Substring(8).Trim() : null;
            }
            return null;
        }
    }
}
