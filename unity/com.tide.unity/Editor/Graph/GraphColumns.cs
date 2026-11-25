using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // Rows that line up in columns, as the page's (style.css, .tg-cols): a box's rows are made of cells, each in its
    // column; a column is as wide as its widest cell in that box, its cells left-aligned. A cell with something in it
    // starts the look's columnGap after the column before it; an empty cell takes no room, so a column empty in every
    // row adds nothing. The parts within a cell are 5 px apart. What spans the box (a + button, a heading, a problem)
    // is no row of it. USS has no grid: the box is laid out, its cells measured, and their widths set.
    static class GraphColumns
    {
        const float Within = 5;

        // A row of a box: `items`, each with its column (1 on); the row's own other children (a menu over its end)
        // stay
        public static VisualElement Row(VisualElement row, IEnumerable<(VisualElement e, int column)> items)
        {
            row.AddToClassList("tg-colrow");
            var list = items.Where(i => i.e != null).ToList();
            var count = list.Count == 0 ? 0 : list.Max(i => i.column);
            for (var n = 1; n <= count; n++)
            {
                var cell = new VisualElement { pickingMode = PickingMode.Ignore };
                cell.AddToClassList("tg-cell");
                cell.AddToClassList("tg-c" + n);
                var first = true;
                foreach (var (e, column) in list)
                {
                    if (column != n) continue;
                    if (!first) e.style.marginLeft = Within + e.resolvedStyle.marginLeft;
                    cell.Add(e);
                    first = false;
                }
                cell.style.display = cell.childCount == 0 ? DisplayStyle.None : DisplayStyle.Flex;
                // Out of a box, its cells are as far apart as its parts
                if (n > 1 && cell.childCount > 0) cell.style.marginLeft = Within;
                row.Insert(n - 1, cell);
            }
            return row;
        }

        // A row's children, sorted into its cells by `column` (0 leaves one out of them); spaces between its
        // parts go
        public static VisualElement Sort(VisualElement row, System.Func<VisualElement, int, int> column)
        {
            var children = row.Children().Where(c => !c.ClassListContains("tg-gap") && !c.ClassListContains("tg-grow") && !(c.GetType() == typeof(VisualElement) && c.childCount == 0 && !c.GetClasses().Any())).ToList();
            var items = new List<(VisualElement, int)>();
            var stay = new List<VisualElement>();
            for (var i = 0; i < children.Count; i++)
            {
                var n = column(children[i], i);
                if (n > 0) items.Add((children[i], n));
                else stay.Add(children[i]);
            }
            row.Clear();
            Row(row, items);
            foreach (var s in stay) row.Add(s);
            return row;
        }

        // A box whose rows line up: they're aligned whenever it's laid out again
        public static void Box(VisualElement box)
        {
            box.AddToClassList("tg-cols");
            box.RegisterCallback<GeometryChangedEvent>(_ => Align(box));
        }

        // A box's rows, at any depth in it (a group of them), but not another box's in it
        static IEnumerable<VisualElement> Rows(VisualElement box) => box.Query(className: "tg-colrow").ToList().Where(r => BoxOf(r) == box);

        static VisualElement BoxOf(VisualElement row)
        {
            var e = row.parent;
            while (e != null && !e.ClassListContains("tg-cols")) e = e.parent;
            return e;
        }

        static IEnumerable<VisualElement> Cells(VisualElement row) => row.Children().Where(c => c.ClassListContains("tg-cell"));

        static int Column(VisualElement cell)
        {
            for (var n = 1; n <= 8; n++)
                if (cell.ClassListContains("tg-c" + n)) return n;
            return 0;
        }

        // A cell's own width: its parts', as they'd be laid out with nothing around them
        static float Natural(VisualElement cell)
        {
            var w = 0f;
            foreach (var c in cell.Children())
            {
                if (c.resolvedStyle.display == DisplayStyle.None || c.resolvedStyle.position == Position.Absolute) continue;
                var l = c.layout.width;
                if (float.IsNaN(l)) return float.NaN;
                w += l + c.resolvedStyle.marginLeft + c.resolvedStyle.marginRight;
            }
            return w;
        }

        // Each column as wide as its widest cell, a cell with something in it the gap after the column before
        public static void Align(VisualElement box)
        {
            var gap = GraphSchema.Available ? GraphSchema.Size("columnGap", 8) : 8;
            var widths = new Dictionary<int, float>();
            foreach (var row in Rows(box))
                foreach (var cell in Cells(row))
                {
                    if (cell.childCount == 0) continue;
                    var w = Natural(cell);
                    if (float.IsNaN(w)) return;
                    var n = Column(cell);
                    widths[n] = Mathf.Max(widths.TryGetValue(n, out var was) ? was : 0, w);
                }
            var last = widths.Count == 0 ? 0 : widths.Keys.Max();
            var first = widths.Count == 0 ? 0 : widths.Keys.Min();
            var signature = string.Join(",", widths.OrderBy(p => p.Key).Select(p => p.Key + ":" + Mathf.Round(p.Value * 10)));
            if (box.userData as string == signature) return;
            box.userData = signature;
            // The last column of a card's rows takes what's left of the row, as the page's (1fr); the entry's names do,
            // so that its pins are at its end
            var fill = box.ClassListContains("tg-rows") ? last : box.ClassListContains("tg-entry-rows") || box.ClassListContains("tg-entry-query") ? 3 : -1;
            foreach (var row in Rows(box))
                foreach (var cell in Cells(row))
                {
                    var n = Column(cell);
                    var track = widths.TryGetValue(n, out var w) ? w : 0;
                    var lead = n > first && track > 0 ? gap : 0;
                    cell.style.display = track > 0 ? DisplayStyle.Flex : DisplayStyle.None;
                    cell.style.marginLeft = 0;
                    cell.style.paddingLeft = cell.childCount > 0 ? lead : 0;
                    if (n == fill)
                    {
                        cell.style.width = StyleKeyword.Auto;
                        cell.style.flexGrow = 1;
                        cell.style.flexShrink = 1;
                        cell.style.minWidth = 0;
                    }
                    else cell.style.width = track + lead;
                }
        }

        // Where a box's columns start, from `of`'s left: those with something seen in them, for a box of two or more
        public static List<float> Starts(VisualElement box, VisualElement of)
        {
            var starts = new SortedDictionary<int, float>();
            foreach (var row in Rows(box))
                foreach (var cell in Cells(row))
                {
                    if (cell.resolvedStyle.display == DisplayStyle.None) continue;
                    var seen = cell.Children().Any(c => c.resolvedStyle.display != DisplayStyle.None && c.resolvedStyle.visibility == Visibility.Visible && !c.ClassListContains("tg-row-empty"));
                    if (!seen) continue;
                    var x = cell.ChangeCoordinatesTo(of, new Vector2(cell.resolvedStyle.paddingLeft, 0)).x;
                    var n = Column(cell);
                    starts[n] = starts.TryGetValue(n, out var was) ? Mathf.Min(was, x) : x;
                }
            return starts.Values.ToList();
        }
    }
}
