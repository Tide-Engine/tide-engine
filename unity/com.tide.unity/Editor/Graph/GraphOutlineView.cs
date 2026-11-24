using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // The file's outline: its namespace and using lines, every declaration (in namespace blocks too), each type's
    // members, a property's accessors, an enum's members, the settings' entries and a prefab's entities. A row's
    // menu adds, renames, duplicates, moves and deletes.
    sealed class GraphOutlineView : VisualElement
    {
        readonly TideGraphWindow window;
        readonly ScrollView list;
        readonly Dictionary<int, VisualElement> rows = new Dictionary<int, VisualElement>();
        readonly HashSet<int> errors = new HashSet<int>();
        int marked = -1;
        TextField renaming;

        // The file's own row: its namespace and using lines
        public const int FileRow = 0;

        static readonly Color selectedColor = new Color(0.17f, 0.36f, 0.53f);
        static readonly Color errorColor = new Color(1f, 0.45f, 0.4f);

        public GraphOutlineView(TideGraphWindow window)
        {
            this.window = window;
            style.flexGrow = 1;
            style.minWidth = 150;
            var header = new VisualElement { style = { flexDirection = FlexDirection.Row, paddingLeft = 4, paddingTop = 2, paddingBottom = 2 } };
            header.Add(new Label("Declarations") { style = { flexGrow = 1, unityFontStyleAndWeight = FontStyle.Bold, unityTextAlign = TextAnchor.MiddleLeft } });
            header.Add(new Button(() => AddMenu(null).ShowAsContext()) { text = "+", tooltip = "Add a declaration" });
            Add(header);
            list = new ScrollView { style = { flexGrow = 1 } };
            Add(list);
            focusable = true;
            RegisterCallback<KeyDownEvent>(OnKey);
        }

        GraphDocument Doc => window.Doc;

        public void Rebuild()
        {
            list.Clear();
            rows.Clear();
            Row(FileRow, "File: namespace and using", 0, null);
            Decls(Doc.Decls, 0);
            Mark(window.SelectedElement);
        }

        void Decls(JValue decls, int depth)
        {
            foreach (var d in decls.Items)
            {
                var id = d.Wire("id");
                var e = Doc.Find(id);
                if (e == null) continue;
                Row(id, Doc.Describe(e), depth, e);
                var kind = d.Str("kind");
                if (kind == "namespace" && d.Arr("decls") != null) Decls(d.Arr("decls"), depth + 1);
                var members = d.Arr("members");
                if (members != null)
                {
                    foreach (var m in members.Items)
                    {
                        var me = Doc.Find(m.Wire("id"));
                        if (me == null) continue;
                        Row(me.Id, me.Role == GraphRole.EnumMember ? m.Str("name") : Doc.Describe(me), depth + 1, me);
                        var accessors = m.Arr("accessors");
                        if (accessors != null)
                            foreach (var a in accessors.Items)
                            {
                                var ae = Doc.Find(a.Wire("id"));
                                if (ae != null) Row(ae.Id, a.Str("word"), depth + 2, ae);
                            }
                    }
                }
                var entries = d.Arr("entries");
                if (entries != null)
                    foreach (var en in entries.Items)
                    {
                        var ee = Doc.Find(en.Wire("id"));
                        if (ee != null) Row(ee.Id, en.Str("name"), depth + 1, ee);
                    }
                var root = d.Get("root");
                if (kind == "prefab" && root != null && root.IsObject) Prefab(root, depth + 1);
            }
        }

        void Prefab(JValue p, int depth)
        {
            var e = Doc.Find(p.Wire("id"));
            if (e == null) return;
            Row(e.Id, Doc.Describe(e), depth, e);
            var children = p.Arr("children");
            if (children != null) foreach (var c in children.Items) Prefab(c, depth + 1);
        }

        void Row(int id, string text, int depth, GraphElement e)
        {
            var row = new VisualElement { style = { flexDirection = FlexDirection.Row, paddingLeft = 6 + depth * 14, height = 20, alignItems = Align.Center } };
            var label = new Label(text) { name = "label", style = { flexGrow = 1, unityTextAlign = TextAnchor.MiddleLeft, overflow = Overflow.Hidden } };
            if (e != null && e.Role == GraphRole.Decl) label.style.unityFontStyleAndWeight = FontStyle.Bold;
            if (errors.Contains(id)) label.style.color = errorColor;
            row.Add(label);
            row.RegisterCallback<PointerDownEvent>(ev =>
            {
                if (ev.button != 0) return;
                Focus();
                if (ev.clickCount == 2 && e != null && CanRename(e)) StartRename(id);
                else window.Select(id);
            });
            row.AddManipulator(new ContextualMenuManipulator(ev => Menu(ev.menu, id, e)));
            rows[id] = row;
            list.Add(row);
        }

        public void Mark(int id)
        {
            if (rows.TryGetValue(marked, out var old)) old.style.backgroundColor = StyleKeyword.Null;
            marked = id;
            if (rows.TryGetValue(id, out var row))
            {
                row.style.backgroundColor = selectedColor;
                if (list.panel != null) list.ScrollTo(row);
            }
        }

        public void MarkDiagnostics(IEnumerable<GraphDiagnostic> diagnostics)
        {
            errors.Clear();
            foreach (var d in diagnostics)
            {
                if (d.Severity != "error") continue;
                // The declaration or member it's in
                var e = Doc.Find(d.Id);
                while (e != null && !rows.ContainsKey(e.Id))
                {
                    var owner = e.Owner?.Wire("id") ?? 0;
                    e = owner != 0 && owner != e.Id ? Doc.Find(owner) : null;
                }
                if (e != null) errors.Add(e.Id);
            }
            foreach (var pair in rows)
            {
                var label = pair.Value.Q<Label>("label");
                if (label != null) label.style.color = errors.Contains(pair.Key) ? errorColor : StyleKeyword.Null;
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Menus

        GenericMenu AddMenu(JValue into)
        {
            var menu = new GenericMenu();
            foreach (var kind in GraphSchema.DeclKinds)
            {
                var k = kind;
                menu.AddItem(new GUIContent(k), false, () => AddDecl(k, into));
            }
            return menu;
        }

        internal void AddDecl(string kind, JValue into)
        {
            JValue made = null;
            window.Edit($"Add {kind}", () =>
            {
                made = Doc.NewDecl(kind, UniqueName(Capital(kind)));
                (into?.Arr("decls") ?? Doc.Decls).Items.Add(made);
            });
            if (made != null) window.Select(made.Wire("id"));
        }

        static string Capital(string s) => string.IsNullOrEmpty(s) ? s : char.ToUpperInvariant(s[0]) + s.Substring(1);

        string UniqueName(string name)
        {
            var taken = new HashSet<string>(Doc.Elements.Select(e => e.Value.Str("name")).Where(n => n != null));
            if (!taken.Contains(name)) return name;
            for (var i = 2; ; i++)
                if (!taken.Contains(name + i)) return name + i;
        }

        void Menu(DropdownMenu menu, int id, GraphElement e)
        {
            if (id == FileRow || e == null)
            {
                foreach (var kind in GraphSchema.DeclKinds)
                {
                    var k = kind;
                    menu.AppendAction("Add declaration/" + k, _ => AddDecl(k, null));
                }
                return;
            }
            var v = e.Value;
            var kindOf = v.Str("kind");
            if (e.Role == GraphRole.Decl && kindOf == "namespace")
                foreach (var kind in GraphSchema.DeclKinds)
                {
                    var k = kind;
                    menu.AppendAction("Add declaration inside/" + k, _ => AddDecl(k, v));
                }
            if (e.Role == GraphRole.Decl && System.Array.IndexOf(GraphSchema.DataKinds, kindOf) >= 0)
                foreach (var kind in GraphSchema.MemberKinds)
                {
                    var k = kind;
                    menu.AppendAction("Add member/" + k, _ => AddMember(v, k));
                }
            if (e.Role == GraphRole.Decl && kindOf == "enum") menu.AppendAction("Add enum member", _ => AddNamed(v, "members", "Member"));
            if (e.Role == GraphRole.Decl && kindOf == "settings") menu.AppendAction("Add setting", _ => AddNamed(v, "entries", "setting", "int", "0"));
            if (e.Role == GraphRole.Member && kindOf == "property")
            {
                var accessors = v.Arr("accessors");
                foreach (var word in new[] { "get", "set" })
                {
                    var w = word;
                    var has = accessors != null && accessors.Items.Any(a => a.Str("word") == w);
                    menu.AppendAction("Add " + w, _ => window.Edit("Add " + w, () =>
                    {
                        var list = v.Arr("accessors");
                        if (list == null) v.Set("accessors", list = JValue.NewArray(), GraphSchema.MemberOrder("property"));
                        list.Items.Add(Doc.NewAccessor(w));
                    }), has ? DropdownMenuAction.Status.Disabled : DropdownMenuAction.Status.Normal);
                }
            }
            if (e.Role == GraphRole.PrefabNode)
                menu.AppendAction("Add child entity", _ => window.Edit("Add child entity", () =>
                {
                    var children = v.Arr("children");
                    if (children == null) v.Set("children", children = JValue.NewArray(), GraphSchema.PrefabNodeOrder);
                    children.Items.Add(Doc.NewPrefabNode(UniqueName("Child")));
                }));
            menu.AppendSeparator();
            if (CanRename(e)) menu.AppendAction("Rename", _ => StartRename(id));
            if (e.List != null)
            {
                menu.AppendAction("Duplicate", _ => Duplicate(e));
                var at = e.List.Items.IndexOf(v);
                menu.AppendAction("Move up", _ => Move(e, -1), at > 0 ? DropdownMenuAction.Status.Normal : DropdownMenuAction.Status.Disabled);
                menu.AppendAction("Move down", _ => Move(e, 1), at < e.List.Items.Count - 1 ? DropdownMenuAction.Status.Normal : DropdownMenuAction.Status.Disabled);
                menu.AppendAction("Delete", _ => Delete(e));
            }
        }

        internal void AddMember(JValue decl, string kind)
        {
            JValue made = null;
            window.Edit($"Add {kind}", () =>
            {
                var members = decl.Arr("members");
                if (members == null)
                {
                    members = JValue.NewArray();
                    decl.Set("members", members, GraphSchema.DeclOrder(decl.Str("kind")));
                }
                var name = kind == "field" ? "value" : kind == "method" ? "Method" : kind == "property" ? "Property" : kind == "tweak" ? "tweak" : null;
                made = Doc.NewMember(kind, name != null ? UniqueName(name) : null);
                if (kind == "sample" && decl.Str("kind") == "input") made.Set("name", JValue.Of("Sample"));
                members.Items.Add(made);
            });
            if (made != null) window.Select(made.Wire("id"));
        }

        void AddNamed(JValue owner, string key, string name, string valueKind = null, string valueText = null)
        {
            JValue made = null;
            window.Edit("Add " + name, () =>
            {
                var a = owner.Arr(key);
                if (a == null) owner.Set(key, a = JValue.NewArray(), GraphSchema.DeclOrder(owner.Str("kind")));
                made = Doc.NewNamed(UniqueName(name), valueKind, valueText);
                a.Items.Add(made);
            });
            if (made != null) window.Select(made.Wire("id"));
        }

        static bool CanRename(GraphElement e)
        {
            if (e.Role == GraphRole.PrefabNode) return e.List != null;
            if (e.Role == GraphRole.Accessor) return false;
            var kind = e.Value.Str("kind");
            return kind != "settings" && kind != "text" && kind != "sanitize" && kind != "predict" && kind != "prepare" && kind != "operator";
        }

        void StartRename(int id)
        {
            if (!rows.TryGetValue(id, out var row)) return;
            var e = Doc.Find(id);
            if (e == null) return;
            var label = row.Q<Label>("label");
            renaming = new TextField { value = e.Value.Str("name") ?? "", style = { flexGrow = 1 } };
            label.style.display = DisplayStyle.None;
            row.Add(renaming);
            var field = renaming;
            void Finish(bool keep)
            {
                if (renaming != field) return;
                renaming = null;
                var name = field.value.Trim();
                if (keep && name.Length > 0 && name != e.Value.Str("name")) Rename(e, name);
                else
                {
                    field.RemoveFromHierarchy();
                    label.style.display = DisplayStyle.Flex;
                }
            }
            field.RegisterCallback<FocusOutEvent>(_ => Finish(true));
            field.RegisterCallback<KeyDownEvent>(ev =>
            {
                if (ev.keyCode == KeyCode.Return || ev.keyCode == KeyCode.KeypadEnter) Finish(true);
                else if (ev.keyCode == KeyCode.Escape) Finish(false);
            });
            field.schedule.Execute(() =>
            {
                field.Focus();
                field.SelectAll();
            });
        }

        public void Rename(GraphElement e, string name)
        {
            window.Edit("Rename", () => e.Value.Set("name", JValue.Of(name), GraphDocument.OrderOf(e)));
        }

        public void Duplicate(GraphElement e)
        {
            JValue copy = null;
            window.Edit("Duplicate", () =>
            {
                copy = Doc.Duplicate(e.Value);
                var name = copy.Str("name");
                if (!string.IsNullOrEmpty(name)) copy.Set("name", JValue.Of(UniqueName(name)));
                e.List.Items.Insert(e.List.Items.IndexOf(e.Value) + 1, copy);
            });
            if (copy != null) window.Select(copy.Wire("id"));
        }

        public void Move(GraphElement e, int by)
        {
            var at = e.List.Items.IndexOf(e.Value);
            var to = at + by;
            if (at < 0 || to < 0 || to >= e.List.Items.Count) return;
            window.Edit(by < 0 ? "Move up" : "Move down", () =>
            {
                e.List.Items.RemoveAt(at);
                e.List.Items.Insert(to, e.Value);
            });
            window.Select(e.Id, false);
        }

        public void Delete(GraphElement e)
        {
            window.Edit("Delete " + Doc.Describe(e), () => e.List.Items.Remove(e.Value));
            window.Select(FileRow, false);
        }

        void OnKey(KeyDownEvent ev)
        {
            if (renaming != null) return;
            var e = Doc.Find(window.SelectedElement);
            if (e == null || e.Role == GraphRole.Node || e.Role == GraphRole.Note) return;
            if (ev.keyCode == KeyCode.F2 && CanRename(e)) StartRename(e.Id);
            else if ((ev.keyCode == KeyCode.Delete || ev.keyCode == KeyCode.Backspace) && e.List != null) Delete(e);
            else if (ev.keyCode == KeyCode.D && (ev.ctrlKey || ev.commandKey) && e.List != null) Duplicate(e);
            else if (ev.keyCode == KeyCode.UpArrow && ev.altKey && e.List != null) Move(e, -1);
            else if (ev.keyCode == KeyCode.DownArrow && ev.altKey && e.List != null) Move(e, 1);
            else return;
            ev.StopPropagation();
        }
    }
}
