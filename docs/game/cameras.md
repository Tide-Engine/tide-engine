---
description: "Cameras in Tide views: screen and world coordinates and rays for picking, locking the cursor to look around, a projection of your own, and reading a camera back as matrices."
---

# Cameras and coordinates

A view sets its cameras with `Draw.SetCamera`: `(center, size)` is the 2D one (see [Draw functions](./views.md#draw-functions)) and `(position, rotation, fieldOfView)` the 3D one (see [Drawing in 3D](./3d.md)). This page is what else a camera is for: taking points between the screen and the world, turning with the mouse, any projection, and the matrices a frag needs of it.

## Screen and world coordinates

The conversion helpers use the currently selected camera, including a camera set earlier in the same frame. Their screen coordinates are logical pixels from the top left, with `y` down, as `Draw.Screen` and the GUI's are.

`Screen.width` and `Screen.height` are the window's size in those pixels. Both are floats, so `Screen.width / Screen.height` is the screen's shape with nothing rounded away. Views, and the functions they call, read them.

| Function | Result |
|---|---|
| `Draw.WorldToScreen(float2 point)` | The point's screen position |
| `Draw.ScreenToWorld(float2 point)` | Its position under the current 2D camera |
| `Draw.WorldToScreen(float3 point)` | Screen `x` and `y`, and normalized depth in `z` |
| `Draw.ScreenToWorld(float3 point)` | The world position at screen `x`, `y` and normalized depth `z` |
| `Draw.ScreenRayOrigin(float2 point)` | The world position on the camera's near plane |
| `Draw.ScreenRayDirection(float2 point)` | A normalized world direction through that pixel |

In 3D, depth is the depth buffer's value: 0 at the near plane and 1 at the far limit, rather than a distance in world units. The standard perspective camera has an infinite far limit, so depth 1 has no finite world position. The ray helpers work with perspective and orthographic cameras, including custom projection matrices. Intersect the ray with your game's objects or a plane to pick a world position; a pixel alone has no unique 3D position.

The devices count pixels the other way: from the bottom left, with `y` up, as Unity's Input System does. `Screen.Flip(position)` gives the same point counted from the screen's other edge, so it turns a device's position into a screen position, for these helpers, `Draw.Screen()` and a `Rect` of the GUI's. It's its own inverse: it turns a screen position into a device's too.

```csharp
view Pointer(Devices devices)
{
    var screen = Screen.Flip(devices.pointer.position);
    var world = Draw.ScreenToWorld(screen);
    Draw.WireCircle(world, 0.2, Color.yellow);

    var origin = Draw.ScreenRayOrigin(screen);
    var direction = Draw.ScreenRayDirection(screen);
    // Hit the world plane y = 0, when the ray points towards it.
    if (Math.Abs(direction.y) > 0.00001)
    {
        float distance = -origin.y / direction.y;
        if (distance >= 0) Draw.Sphere(origin + direction * distance, 0.1, Color.yellow);
    }
}
```

The cameras these go through are there as matrices too: see [A camera as matrices](#a-camera-as-matrices).

On the screen (`Draw.Screen`, `Draw.SetScreen`), its conversion helpers are identity mappings. A viewport with no size, a singular camera matrix, or a point whose projection has no finite position returns zero.

## Looking around

A camera that turns with the mouse wants the mouse for itself: the cursor out of sight, and no screen's edge for it to stop at. `Cursor.Lock()` asks for that. Call it every frame the game wants the mouse, as with `GUI.ShowKeyboard()`: the cursor comes back by itself once a frame goes by without it, so there's nothing to unlock. `Cursor.locked` says whether the lock holds.

```csharp
input PlayerInput
{
    float2 look;

    Sample(Devices devices)
    {
        if (Cursor.locked) look = devices.mouse.delta * 0.003;
    }
}

system Turn(PlayerInput input, mut Viewer viewer)
{
    viewer.yaw += input.look.x;
    viewer.pitch = Math.Clamp(viewer.pitch - input.look.y, -1.5, 1.5);
    viewer.rotation = quaternion.Euler(float3(viewer.pitch, viewer.yaw, 0));
}

local singleton Menu
{
    bool paused;
    bool looking; // The window held the cursor last frame
}

view Look(Devices devices, Viewer viewer, mut Menu menu)
{
    Draw.SetCamera(viewer.position, viewer.rotation, 75);
    if (devices.keyboard.escape.down) menu.paused = true;
    if (!menu.paused) Cursor.Lock();
    // The lock went away on its own: show the pause menu
    if (menu.looking && !Cursor.locked) menu.paused = true;
    menu.looking = Cursor.locked;
}

view PauseMenu(mut Menu menu)
{
    GUILayout.Modal(Anchor.MiddleCenter, menu.paused)
    {
        if (GUILayout.Button("Resume")) menu.paused = false;
    }
}
```

While the cursor is locked:

- It's hidden, and `devices.mouse.position` stays where it was, as does the pointer's.
- `devices.mouse.delta` keeps coming, however far the mouse goes. Where the system gives it, it's the mouse's own movement, without the acceleration a cursor has, so the camera turns evenly.
- The GUI sees no pointer: nothing hovers, and a click presses no widget, so it's all the game's. A finger on a touchscreen still presses what it touches.

::: warning Read `Cursor.locked`, don't assume
Asking isn't having. The system decides whether the window holds the cursor, and it differs:

- **In a browser,** the lock is only given at a click or a key press, so it takes hold at the player's next one, or at once when the game asks right after one, like a Resume button's click. The browser lets the cursor go when the player presses Escape, which a page can't prevent and often never sees the key of, and when the page loses the focus. For a moment after Escape it won't lock again, and then it takes another click or key press.
- **On desktop,** a window holds the cursor while it has the focus: Alt+Tab lets it go, and the lock returns with the focus if the game still asks. Escape lets it go too, as in a browser, and the game still gets the key: the cursor is the player's until they click on the window or press another key, or until the game stops asking and asks again, which locks at once.
- **On Android,** it never locks, for now.

So look only while `Cursor.locked`, and show the pause menu when it goes false while the game was looking, as above: that's what Escape does, in a browser and on desktop. A game with no pause menu just keeps asking, and the player's next click or key press locks the cursor again.
:::

Only views, and the functions they call, call `Cursor.Lock()`. They read `Cursor.locked`, and so does the input's `Sample`. The match can do neither: the cursor is this machine's, so what the match needs of it goes in the input. While a [modal](./gui.md#modals) is up the lock isn't asked for, whatever views ask, since its widgets need the pointer.

## Any projection

`Draw.SetCamera(transform, projection)` sets the 3D camera from matrices: `transform` places it in the world, looking along its `+z`, and `projection` takes what it sees to the screen, the screen's shape included. `float4x4.PerspectiveFov(verticalFov, aspect, near, far)` makes a perspective one, with the field of view in radians, and `float4x4.Ortho(width, height, near, far)` one without perspective, as isometric games have, as Unity.Mathematics' do. `Screen.width / Screen.height` is the screen's shape.

```csharp
view Isometric(Viewer viewer)
{
    var place = float4x4.TRS(viewer.position, quaternion.Euler(float3(Math.Radians(30), Math.Radians(45), 0)), float3(1));
    float height = 20;
    Draw.SetCamera(place, float4x4.Ortho(height * Screen.width / Screen.height, height, 0.1, 1000));
}
```

## A camera as matrices

Whatever set the camera, a view reads it back as the matrices the GPU draws through:

| Function | Result |
|---|---|
| `Draw.WorldToClip()` | The 3D camera as one `float4x4`, from the world to clip space |
| `Draw.CameraTransform()` | Where the 3D camera is in the world, a `float4x4`: it looks along its `+z` |
| `Draw.CameraProjection()` | Its projection, a `float4x4`: from what it sees to clip space |
| `Draw.WorldToScreenMatrix()` | The 2D camera as a `float3x3`, from the world to the screen's pixels |

- They're the camera as it's set when they're asked, in this frame: ask after `SetCamera`, or inside a `Camera` block. Before any camera is set, they're the camera each frame starts with.
- A render target has the same four, for its own cameras: `target.WorldToClip()`.
- `WorldToClip()` is what a 3D mesh's positions are multiplied by, exactly: the camera's transform undone, z turned around (a camera looks along its `+z`, and clip space along `-z`), then the projection, fitted to the shape of the window or the target. A point through it is in clip space: once divided by its `w`, `x` and `y` go from -1 to 1 across what the camera sees, `y` up, and `z` from -1 at the near plane to 1 at the far limit. It's what a frag needs to find a pixel in another camera's image, as a [shadow map](./shaders.md#a-shadow-map) does with its lamp's.
- `CameraTransform()` and `CameraProjection()` are the two `SetCamera(transform, projection)` takes, so `Draw.SetCamera(Draw.CameraTransform(), Draw.CameraProjection())` sets the camera there already was. A camera set with a field of view has them too, its projection with the screen's shape in it.
- `WorldToScreenMatrix()` does to `float3(x, y, 1)` what `Draw.WorldToScreen` does to a `float2`. It's the identity inside `Draw.Screen`.
- [`Draw.Transform`](./draw-settings.md#transforms) is no part of any of them: they're the camera's alone.

```csharp
view Mirror(mut Looking looking)
{
    Draw.SetCamera(looking.eye, looking.turned, 60);
    looking.toClip = Draw.WorldToClip();            // Kept for a frag, in a local singleton
    float4 clip = Math.Mul(looking.toClip, float4(looking.target, 1));
    looking.onScreen = clip.w > 0 && Math.Abs(clip.x) <= clip.w && Math.Abs(clip.y) <= clip.w;
}
```
