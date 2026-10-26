using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEditor.Compilation;
using UnityEngine;

namespace Tide.Editor
{
    // A script that no longer compiles because a name of the game's C# was renamed or removed in Tide: next to Unity's
    // error, the Console says what became of it ("`Health.hp` is now `Health.health` (Assets/Tide/game.tide:12)", or
    // that it was removed), from the record tide unity keeps of the game's names (Library/Tide/renamed.json, this
    // machine's alone). A note of a rename opens the new declaration; one of a removal, the script. They go as the
    // scripts compile again.
    [InitializeOnLoad]
    public static class TideRenames
    {
        // Ours among the Console's compiler messages, apart from tide's own build's (TideBuildService)
        internal const int ConsoleIdentifier = 0x7469646e; // "tidn"

        [Serializable]
        public sealed class Entry
        {
            public string was;  // A C# name after Tide.Game.: "Combat.Health.hp"
            public string now;  // ...what it is now, or null where it was removed
            public string file; // Where `now` is declared, as the project names it
            public int line;
            public bool removed;
        }

        [Serializable]
        sealed class Record
        {
            public Entry[] renamed = Array.Empty<Entry>();
        }

        // A note for one of Unity's errors: its text, and where it opens
        public readonly struct Note
        {
            public readonly string Message, File;
            public readonly int Line, Column;

            public Note(string message, string file, int line, int column)
            {
                Message = message;
                File = file;
                Line = line;
                Column = column;
            }
        }

        static readonly HashSet<string> said = new HashSet<string>();

        static TideRenames()
        {
            if (AssetDatabase.IsAssetImportWorkerProcess()) return;
            CompilationPipeline.compilationStarted += _ => Clear();
            CompilationPipeline.assemblyCompilationFinished += (_, messages) => Explain(messages);
        }

        public static string RecordPath => Path.Combine(TideLibrary.BuildFolder, "renamed.json");

        // The record tide keeps: empty before it's written anything
        public static Entry[] Read()
        {
            try
            {
                if (!File.Exists(RecordPath)) return Array.Empty<Entry>();
                return JsonUtility.FromJson<Record>(File.ReadAllText(RecordPath))?.renamed ?? Array.Empty<Entry>();
            }
            catch (Exception)
            {
                return Array.Empty<Entry>(); // tide is writing it: nothing to say this time
            }
        }

        // C#'s errors on a name that isn't there: a member of a type (CS0117, CS1061), a type (CS0246, CS0103), a type
        // in a namespace (CS0234) or in a type (CS0426)
        static readonly Regex memberMissing = new Regex(@"'(?<type>[^']+)' does not contain a definition for '(?<name>[^']+)'");
        static readonly Regex typeMissing = new Regex(@"The (?:type or namespace )?name '(?<name>[^']+)' (?:could not be found|does not exist in the current context)");
        static readonly Regex inNamespace = new Regex(@"The type or namespace name '(?<name>[^']+)' does not exist in the namespace '(?<type>[^']+)'");
        static readonly Regex inType = new Regex(@"The type name '(?<name>[^']+)' does not exist in the type '(?<type>[^']+)'");

        // The name an error says isn't there, after Tide.Game. as the record has names, or null for another error
        static string Missing(string message)
        {
            Match match;
            if ((match = memberMissing.Match(message)).Success || (match = inNamespace.Match(message)).Success || (match = inType.Match(message)).Success)
            {
                return Short(match.Groups["type"].Value) + "." + match.Groups["name"].Value;
            }
            match = typeMissing.Match(message);
            return match.Success ? Short(match.Groups["name"].Value) : null;
        }

        static string Short(string name)
        {
            if (name.StartsWith("global::")) name = name.Substring(8);
            if (name.StartsWith("Tide.Game.")) name = name.Substring(10);
            else if (name == "Tide.Game") name = "";
            return name.TrimStart('.');
        }

        // The record's entry for a name C# says isn't there: its own, or one whose name ends with it (a type in a
        // namespace the script names it without, or a class's Value, whose fields are its); null for none
        static Entry EntryOf(string missing, Entry[] record)
        {
            if (string.IsNullOrEmpty(missing)) return null;
            var plain = missing.Replace(".Value.", ".");
            Entry ending = null;
            foreach (var e in record)
            {
                if (string.IsNullOrEmpty(e.was)) continue;
                if (e.was == missing || e.was == plain) return e;
                if (ending == null && (e.was.EndsWith("." + missing) || e.was.EndsWith("." + plain))) ending = e;
            }
            return ending;
        }

        // Notes for the compiler's errors the record explains, each once
        public static List<Note> NotesFor(IEnumerable<CompilerMessage> messages, Entry[] record)
        {
            var notes = new List<Note>();
            if (record.Length == 0) return notes;
            foreach (var message in messages)
            {
                if (message.type != CompilerMessageType.Error) continue;
                var entry = EntryOf(Missing(message.message), record);
                if (entry == null) continue;
                if (entry.removed || string.IsNullOrEmpty(entry.now))
                {
                    notes.Add(new Note($"tide: `{entry.was}` was removed from the game's Tide code", message.file, message.line, message.column));
                }
                else
                {
                    var where = string.IsNullOrEmpty(entry.file) ? "" : $" ({entry.file}:{entry.line})";
                    var file = string.IsNullOrEmpty(entry.file) ? message.file : entry.file;
                    var line = string.IsNullOrEmpty(entry.file) ? message.line : entry.line;
                    notes.Add(new Note($"tide: `{entry.was}` is now `{entry.now}`{where}", file, line, 0));
                }
            }
            return notes;
        }

        // The notes of an assembly's compile, beside Unity's errors
        public static void Explain(CompilerMessage[] messages)
        {
            if (messages == null) return;
            foreach (var note in NotesFor(messages, Read()))
            {
                if (!said.Add(note.Message + "\n" + note.File + ":" + note.Line)) continue;
                if (!TideBuildService.LogLocated(note.Message, note.File ?? "", note.Line, note.Column, false, ConsoleIdentifier))
                {
                    Debug.LogFormat(LogType.Warning, LogOption.NoStacktrace, null, "{0}", note.Message);
                }
            }
        }

        // The last compile's notes go as the scripts compile again
        public static void Clear()
        {
            said.Clear();
            TideBuildService.RemoveLocated(ConsoleIdentifier);
        }
    }
}
