using System;
using System.Collections;
using System.IO;
using System.Linq;
using System.Reflection;
using Tide.Editor;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

// Pictures of the graph editor's window, and the mouse and the keys as the system sends them. A window's view gives
// its pixels (GUIView.GrabPixels) once the editor has a graphics device, and the story's tests take one at each step
// when TIDE_GRAPH_SHOTS names a folder for them.
//
// A batch-mode editor WITH a graphics device (no -nographics) shows its editor windows on the desktop it runs on,
// and they come in front and take keys. So a run that takes pictures is started on a desktop of its own
// (CreateDesktop, and CreateProcess with STARTUPINFO.lpDesktop), where its windows are drawn and never seen: nothing
// of it is on the user's desktop. The usual run (-batchmode -nographics) makes no window visible, lays nothing out,
// and takes no pictures.
static class GraphShots
{
    const BindingFlags All = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;

    public static string Folder => Environment.GetEnvironmentVariable("TIDE_GRAPH_SHOTS");
    public static bool On => !string.IsNullOrEmpty(Folder) && SystemInfo.graphicsDeviceType != UnityEngine.Rendering.GraphicsDeviceType.Null;

    // The window at the size the pictures are taken at
    public static void Size(TideGraphWindow window)
    {
        if (On) window.position = new Rect(80, 80, 1280, 800);
    }

    // A few editor updates: layouts are done, and what was asked of the language server is back
    public static IEnumerator Settle(int frames = 3)
    {
        for (var i = 0; i < frames; i++) yield return null;
    }

    // The window's pixels, as a PNG named for the step and the theme
    public static IEnumerator Take(TideGraphWindow window, string name)
    {
        if (!On) yield break;
        yield return Settle(4);
        window.Repaint();
        yield return null;
        var view = typeof(EditorWindow).GetField("m_Parent", All)?.GetValue(window);
        var grab = view?.GetType().GetMethod("GrabPixels", All);
        if (grab == null) yield break;
        view.GetType().GetMethod("RepaintImmediately", All, null, Type.EmptyTypes, null)?.Invoke(view, null);
        var root = window.rootVisualElement;
        var width = Mathf.RoundToInt(root.layout.width);
        var height = Mathf.RoundToInt(root.layout.height);
        if (width <= 0 || height <= 0) yield break;
        var target = new RenderTexture(width, height, 24, RenderTextureFormat.ARGB32);
        target.Create();
        try
        {
            grab.Invoke(view, new object[] { target, new Rect(0, 0, width, height) });
            var was = RenderTexture.active;
            RenderTexture.active = target;
            var picture = new Texture2D(width, height, TextureFormat.RGBA32, false);
            picture.ReadPixels(new Rect(0, 0, width, height), 0, 0);
            RenderTexture.active = was;
            // The view's rows come bottom first
            var pixels = picture.GetPixels32();
            var flipped = new Color32[pixels.Length];
            for (var y = 0; y < height; y++) Array.Copy(pixels, y * width, flipped, (height - 1 - y) * width, width);
            for (var i = 0; i < flipped.Length; i++) flipped[i].a = 255;
            picture.SetPixels32(flipped);
            picture.Apply();
            Directory.CreateDirectory(Folder);
            File.WriteAllBytes(Path.Combine(Folder, name + (window.Light ? "-light" : "-dark") + ".png"), picture.EncodeToPNG());
            UnityEngine.Object.DestroyImmediate(picture);
        }
        finally
        {
            target.Release();
            UnityEngine.Object.DestroyImmediate(target);
        }
    }

    // The pointer over an element, as the window gets it: what shows only under the pointer shows
    public static void Hover(TideGraphWindow window, VisualElement element)
    {
        if (!On || element == null) return;
        var at = element.worldBound.center;
        window.SendEvent(new Event { type = EventType.MouseMove, mousePosition = at });
    }

    // What the mouse and the keyboard do, as the window gets them from the system: a press, a drag, a release, a key
    public static void Mouse(TideGraphWindow window, EventType type, Vector2 at, int button = 0, EventModifiers modifiers = EventModifiers.None)
        => window.SendEvent(new Event { type = type, mousePosition = at, button = button, modifiers = modifiers, clickCount = type == EventType.MouseDown ? 1 : 0 });

    public static void Click(TideGraphWindow window, Vector2 at, int button = 0)
    {
        Mouse(window, EventType.MouseMove, at);
        Mouse(window, EventType.MouseDown, at, button);
        Mouse(window, EventType.MouseUp, at, button);
    }

    public static void Drag(TideGraphWindow window, Vector2 from, Vector2 to, int button = 0, EventModifiers modifiers = EventModifiers.None)
    {
        Mouse(window, EventType.MouseMove, from);
        Mouse(window, EventType.MouseDown, from, button, modifiers);
        for (var i = 1; i <= 4; i++) Mouse(window, EventType.MouseDrag, Vector2.Lerp(from, to, i / 4f), button, modifiers);
        Mouse(window, EventType.MouseUp, to, button, modifiers);
    }

    public static void Press(TideGraphWindow window, KeyCode key, EventModifiers modifiers = EventModifiers.None)
    {
        window.SendEvent(new Event { type = EventType.KeyDown, keyCode = key, modifiers = modifiers });
        window.SendEvent(new Event { type = EventType.KeyUp, keyCode = key, modifiers = modifiers });
    }

    // Typed characters, as the keyboard sends them
    public static void Keys(TideGraphWindow window, string text)
    {
        foreach (var c in text)
        {
            window.SendEvent(new Event { type = EventType.KeyDown, keyCode = KeyCode.None, character = c });
        }
    }

    public static Vector2 Middle(VisualElement element) => element.worldBound.center;

    // A click on a button, as Enter on it makes one
    public static void Click(Button button)
    {
        using (var e = NavigationSubmitEvent.GetPooled())
        {
            e.target = button;
            button.SendEvent(e);
        }
    }

    // A double click on an element, as the pointer sends it: a name opens to be typed over
    public static void DoubleClick(VisualElement element)
    {
        var at = element.worldBound;
        var position = float.IsNaN(at.x) ? Vector2.zero : at.center;
        for (var click = 1; click <= 2; click++)
        {
            using (var down = PointerDownEvent.GetPooled(new Event { type = EventType.MouseDown, button = 0, clickCount = click, mousePosition = position }))
            {
                down.target = element;
                element.SendEvent(down);
            }
            using (var up = PointerUpEvent.GetPooled(new Event { type = EventType.MouseUp, button = 0, clickCount = click, mousePosition = position }))
            {
                up.target = element;
                element.SendEvent(up);
            }
        }
    }

    // A double click through the window, at a place of it
    public static void DoubleClick(TideGraphWindow window, Vector2 at)
    {
        Mouse(window, EventType.MouseMove, at);
        Mouse(window, EventType.MouseDown, at);
        Mouse(window, EventType.MouseUp, at);
        window.SendEvent(new Event { type = EventType.MouseDown, mousePosition = at, button = 0, clickCount = 2 });
        window.SendEvent(new Event { type = EventType.MouseUp, mousePosition = at, button = 0, clickCount = 2 });
    }

    // Types into a text field and leaves it: what's typed is kept as Enter or a click elsewhere keeps it
    public static void Enter(TextField field, string text) => field.value = text;

    // A key pressed in a field
    public static void Key(VisualElement field, KeyCode key)
    {
        using (var e = KeyDownEvent.GetPooled('\0', key, EventModifiers.None))
        {
            e.target = field;
            field.SendEvent(e);
        }
    }
}
