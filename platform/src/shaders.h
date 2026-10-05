#pragma once

// The renderer's shaders (platform.c), each written for every backend (see
// gpu.h): GLSL for OpenGL, without its version line, and on the web WGSL for
// WebGPU. They draw the same pixels. Until Tide code compiles to them, they're
// written by hand, so a change to one goes to the other.

// WGSL is only the web's.
#ifdef __wasm__
#define WGSL(source) source
#else
#define WGSL(source) NULL
#endif

// Shapes: each instance's quad covers its shape in window pixels, which the
// camera already applied, and `screen` takes to the target.
//
// There are two pipelines of them. The first draws the plain kinds (rects,
// their outlines, circles and lines a pixel wide) from an instance of 24
// bytes, and is what a run of those alone goes through, however long: its
// shaders are what they were before there were smooth shapes, so such a run
// costs what it did. The second draws every kind: a run with a smooth shape
// in it goes through that one, each shape with its extra (see shape_extra in
// platform.c). Both draw the plain kinds from the same lines below, so a rect
// is the same pixels whichever its run goes through.
//
// The second one's `scale` is the target's own pixels in a logical one,
// across and down: a smooth shape's edge is smooth over one of the target's,
// whatever the display's scale. A plain circle's is smooth over a logical
// pixel, which is the same where a display isn't scaled; where it is, the
// renderer draws circles as smooth shapes (see TIDE_DRAW_CIRCLE in platform.c).

#define SHAPE_GLSL_UNIFORMS "layout(std140) uniform uniforms { mat4 screen; vec2 texel; vec2 scale; };\n"

// Where a plain kind's corner goes. `c` is the quad's, from -1 to 1.
#define SHAPE_GLSL_PLACE                                                                                    \
    "if (kind > 3.5) {\n" /* A line: a pixel wide, reaching half a pixel past its ends */                  \
    "        vec2 d = shape.zw - shape.xy;\n"                                                               \
    "        float len = length(d);\n"                                                                      \
    "        vec2 along = len > 0.0 ? d / len : vec2(1.0, 0.0);\n"                                          \
    "        extent = vec2(len * 0.5 + 0.5, 0.5);\n"                                                        \
    "        local = c * extent;\n"                                                                         \
    "        p = (shape.xy + shape.zw) * 0.5 + along * local.x + vec2(-along.y, along.x) * local.y;\n"      \
    "    } else {\n"                                                                                        \
    "        extent = abs(shape.zw);\n"                                                                     \
    "        local = c * (extent + (kind > 1.5 ? 1.0 : 0.0));\n" /* Room for a circle's smooth edge */      \
    "        p = shape.xy + local;\n"                                                                       \
    "    }\n"

// How much of a pixel a plain kind covers.
#define SHAPE_GLSL_COVER                                                                                    \
    "float cover = 1.0;\n"                                                                                  \
    "    if (form > 0.5 && form < 1.5) {\n" /* A rect's outline: the pixel inside its edge */              \
    "        if (all(lessThan(abs(local), extent - 1.0))) discard;\n"                                       \
    "    } else if (form > 1.5 && form < 3.5) {\n" /* Circles, with a smooth edge */                        \
    "        float d = length(local);\n"                                                                    \
    "        cover = clamp(extent.x - d + 0.5, 0.0, 1.0);\n"                                                \
    "        if (form > 2.5) cover *= clamp(d - extent.x + 1.5, 0.0, 1.0);\n" /* An outline: the pixel inside it */ \
    "        if (cover <= 0.0) discard;\n"                                                                  \
    "    }\n"

static const char shape_glsl_vertex[] =
    "layout(location = 0) in vec2 corner;\n" // A corner of the quad, 0 to 1
    "layout(location = 1) in vec4 shape;\n"  // a, then b
    "layout(location = 2) in vec4 color;\n"
    "layout(location = 3) in float kind;\n"
    "layout(std140) uniform uniforms { mat4 screen; };\n"
    "out vec2 local;\n"  // Pixels from its middle; for a line, along and across it
    "out vec2 extent;\n" // Half its size in pixels
    "out vec4 tint;\n"
    "out float form;\n"
    "void main() {\n"
    "    vec2 c = corner * 2.0 - 1.0;\n"
    "    vec2 p;\n"
    "    " SHAPE_GLSL_PLACE
    "    tint = color;\n"
    "    form = kind;\n"
    "    gl_Position = screen * vec4(p, 0.0, 1.0);\n"
    "}\n";

static const char shape_glsl_fragment[] =
    "in vec2 local;\n"
    "in vec2 extent;\n"
    "in vec4 tint;\n"
    "in float form;\n"
    "out vec4 pixel;\n"
    "void main() {\n"
    "    " SHAPE_GLSL_COVER
    "    pixel = vec4(tint.rgb, tint.a * cover);\n"
    "}\n";

// Smooth shapes are drawn by how far each pixel is from their edge, under 0
// inside it, in the shape's own frame: its middle at the origin, before it's
// turned. A box's is exact. An ellipse's has no formula a shader can afford,
// so it's found: the nearest point of the edge, by walking towards it a few
// times (three steps that never go wrong, from the middle of the quarter the
// pixel is in, then two of Newton's, which settle it where the edge is nearly
// flat and floats alone would leave it wandering). Part of a ring is a ring
// and the wedge it's cut to, turned so that it's the same on both sides of
// the x axis.
//
// From that distance: the fill covers a pixel by how much of it is inside,
// half at the edge itself, or fades over `soft`; an outline is the band
// inside the edge; and colors blend with their alpha already in them, so a
// fill that goes to a clear color fades out and doesn't darken on the way.

static const char smooth_shape_glsl_vertex[] =
    "layout(location = 0) in vec2 corner;\n"
    "layout(location = 1) in vec4 shape;\n"
    "layout(location = 2) in vec4 color;\n"
    "layout(location = 3) in float kind;\n"
    "layout(location = 4) in vec4 turn;\n" // Its rotation's cos and sin, its corners' radius, its outline's width
    "layout(location = 5) in vec2 fade;\n" // How far its edge fades, and half an arc's angle
    "layout(location = 6) in vec4 color2;\n"
    "layout(location = 7) in vec4 rim_color;\n"
    SHAPE_GLSL_UNIFORMS
    "out vec2 local;\n"
    "out vec2 extent;\n"
    "out vec4 tint;\n"
    "out float form;\n"
    "out vec4 look;\n" // Its corners' radius, its outline's width, how far its edge fades
    "out vec2 gap;\n"  // The cos and sin of half an arc's angle
    "out vec4 tint2;\n"
    "out vec4 rim;\n"
    "void main() {\n"
    "    vec2 c = corner * 2.0 - 1.0;\n"
    "    vec2 p;\n"
    "    if (kind > 4.5) {\n" // A smooth shape: turned about its middle, with room for its edge and what fades past it
    "        extent = abs(shape.zw);\n"
    "        local = c * (extent + max(fade.x, 0.0) + 1.0);\n"
    "        p = shape.xy + vec2(local.x * turn.x - local.y * turn.y, local.x * turn.y + local.y * turn.x);\n"
    "    } else " SHAPE_GLSL_PLACE
    "    tint = color;\n"
    "    form = kind;\n"
    "    look = vec4(turn.z, turn.w, fade.x, 0.0);\n"
    "    gap = vec2(cos(fade.y), sin(fade.y));\n"
    "    tint2 = color2;\n"
    "    rim = rim_color;\n"
    "    gl_Position = screen * vec4(p, 0.0, 1.0);\n"
    "}\n";

static const char smooth_shape_glsl_fragment[] =
    "in vec2 local;\n"
    "in vec2 extent;\n"
    "in vec4 tint;\n"
    "in float form;\n"
    "in vec4 look;\n"
    "in vec2 gap;\n"
    "in vec4 tint2;\n"
    "in vec4 rim;\n"
    SHAPE_GLSL_UNIFORMS
    "out vec4 pixel;\n"
    "float ellipse(vec2 at, vec2 radii) {\n"
    "    vec2 q = abs(at);\n"
    "    vec2 t = vec2(0.70710678);\n" // Where on the edge: the nearest point is radii * t
    "    vec2 e = vec2(radii.x * radii.x - radii.y * radii.y, radii.y * radii.y - radii.x * radii.x) / radii;\n"
    "    for (int i = 0; i < 3; i++) {\n" // Towards the pixel, around the middle of the edge's curve there
    "        vec2 m = e * t * t * t;\n"
    "        vec2 w = q - m;\n"
    "        t = clamp((w * (length(radii * t - m) / max(length(w), 0.000001)) + m) / radii, 0.0, 1.0);\n"
    "        float n = length(t);\n"
    "        t = n > 0.00001 ? t / n : vec2(0.0, 1.0);\n"
    "    }\n"
    "    for (int i = 0; i < 2; i++) {\n" // Along the edge, to where the way to the pixel is straight across it
    "        vec2 on = radii * t;\n"
    "        vec2 along = radii * vec2(-t.y, t.x);\n"
    "        vec2 to = q - on;\n"
    "        float span = dot(along, along);\n"
    "        float curve = span + dot(to, on);\n"
    "        if (curve > 0.25 * span) {\n"
    "            float slide = dot(to, along) / curve;\n"
    "            vec2 next = clamp(vec2(t.x - t.y * slide, t.y + t.x * slide), 0.0, 1.0);\n"
    "            float n = length(next);\n"
    "            if (n > 0.00001) t = next / n;\n"
    "        }\n"
    "    }\n"
    "    float apart = length(q - radii * t);\n"
    "    vec2 s = q / radii;\n"
    "    return dot(s, s) < 1.0 ? -apart : apart;\n"
    "}\n"
    "void main() {\n"
    "    if (form < 4.5) {\n"
    "    " SHAPE_GLSL_COVER
    "        pixel = vec4(tint.rgb, tint.a * cover);\n"
    "        return;\n"
    "    }\n"
    "    float px = (scale.x + scale.y) * 0.5;\n"
    "    float d;\n"
    "    float outline = look.y;\n"
    "    if (form > 6.5) {\n" // Part of a ring `outline` thick inside its radius
    "        vec2 q = vec2(local.x, abs(local.y));\n"
    "        float ring = abs(length(q) - extent.x + outline * 0.5) - outline * 0.5;\n"
    "        float wedge = dot(q, gap) > 0.0 ? dot(q, vec2(-gap.y, gap.x)) : (gap.x > 0.0 ? length(q) : -length(q));\n"
    "        d = min(max(ring, wedge), 0.0) + length(max(vec2(ring, wedge), 0.0));\n"
    "        outline = 0.0;\n"
    "    } else if (form > 5.5) {\n"
    "        d = ellipse(local, extent);\n"
    "    } else {\n" // A box, its corners round
    "        vec2 q = abs(local) - extent + look.x;\n"
    "        d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - look.x;\n"
    "    }\n"
    "    float cover = clamp(0.5 - d * px, 0.0, 1.0);\n"
    "    if (look.z > 0.0) {\n" // Its color goes on past its edge, fading
    "        float glow = clamp(1.0 - (d * px + 0.5) / (look.z * px + 1.0), 0.0, 1.0);\n"
    "        cover = glow * glow;\n"
    "    } else if (look.z < 0.0) {\n" // It fades inwards from its edge
    "        float glow = clamp(1.0 + (d * px - 0.5) / (1.0 - look.z * px), 0.0, 1.0);\n"
    "        cover *= glow * glow;\n"
    "    }\n"
    "    if (cover <= 0.0) discard;\n"
    "    float down = clamp(local.y / max(extent.y, 0.000001) * 0.5 + 0.5, 0.0, 1.0);\n" // 0 at its top
    "    vec4 paint = mix(vec4(tint.rgb * tint.a, tint.a), vec4(tint2.rgb * tint2.a, tint2.a), down);\n"
    "    if (outline > 0.0) {\n"
    "        paint = mix(vec4(rim.rgb * rim.a, rim.a), paint, clamp(0.5 - (d + outline) * px, 0.0, 1.0));\n"
    "    }\n"
    "    if (paint.a <= 0.0) discard;\n"
    "    pixel = vec4(paint.rgb / paint.a, paint.a * cover);\n"
    "}\n";

#define SHAPE_WGSL_UNIFORMS                                         \
    "struct Uniforms { screen: mat4x4f, texel: vec2f, scale: vec2f }\n" \
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"

#define SHAPE_WGSL_PLACE                                                                                            \
    "if (kind > 3.5) {\n"                                                                                           \
    "        let d = shape.zw - shape.xy;\n"                                                                        \
    "        let len = length(d);\n"                                                                                \
    "        var along = vec2f(1.0, 0.0);\n"                                                                        \
    "        if (len > 0.0) { along = d / len; }\n"                                                                 \
    "        out.extent = vec2f(len * 0.5 + 0.5, 0.5);\n"                                                           \
    "        out.local = c * out.extent;\n"                                                                         \
    "        p = (shape.xy + shape.zw) * 0.5 + along * out.local.x + vec2f(-along.y, along.x) * out.local.y;\n"     \
    "    } else {\n"                                                                                                \
    "        out.extent = abs(shape.zw);\n"                                                                         \
    "        var room = 0.0;\n"                                                                                     \
    "        if (kind > 1.5) { room = 1.0; }\n"                                                                     \
    "        out.local = c * (out.extent + room);\n"                                                                \
    "        p = shape.xy + out.local;\n"                                                                           \
    "    }\n"

#define SHAPE_WGSL_COVER                                                                                            \
    "var cover = 1.0;\n"                                                                                            \
    "    if (in.form > 0.5 && in.form < 1.5) {\n"                                                                   \
    "        if (all(abs(in.local) < in.extent - 1.0)) { discard; }\n"                                              \
    "    } else if (in.form > 1.5 && in.form < 3.5) {\n"                                                            \
    "        let d = length(in.local);\n"                                                                           \
    "        cover = clamp(in.extent.x - d + 0.5, 0.0, 1.0);\n"                                                     \
    "        if (in.form > 2.5) { cover *= clamp(d - in.extent.x + 1.5, 0.0, 1.0); }\n"                             \
    "        if (cover <= 0.0) { discard; }\n"                                                                      \
    "    }\n"

static const char *const shape_wgsl = WGSL(
    "struct Uniforms { screen: mat4x4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "struct Corner {\n"
    "    @builtin(position) position: vec4f,\n"
    "    @location(0) local: vec2f,\n"
    "    @location(1) extent: vec2f,\n"
    "    @location(2) tint: vec4f,\n"
    "    @location(3) form: f32,\n"
    "}\n"
    "@vertex fn vertex(@location(0) corner: vec2f, @location(1) shape: vec4f, @location(2) color: vec4f,\n"
    "                  @location(3) kind: f32) -> Corner {\n"
    "    let c = corner * 2.0 - 1.0;\n"
    "    var out: Corner;\n"
    "    var p: vec2f;\n"
    "    " SHAPE_WGSL_PLACE
    "    out.tint = color;\n"
    "    out.form = kind;\n"
    "    out.position = u.screen * vec4f(p, 0.0, 1.0);\n"
    "    return out;\n"
    "}\n"
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"
    "    " SHAPE_WGSL_COVER
    "    return vec4f(in.tint.rgb, in.tint.a * cover);\n"
    "}\n");

// In two pieces, which the renderer joins: together they're longer than C
// promises a string literal can be.
static const char *const smooth_shape_wgsl_vertex = WGSL(
    SHAPE_WGSL_UNIFORMS
    "struct Corner {\n"
    "    @builtin(position) position: vec4f,\n"
    "    @location(0) local: vec2f,\n"
    "    @location(1) extent: vec2f,\n"
    "    @location(2) tint: vec4f,\n"
    "    @location(3) form: f32,\n"
    "    @location(4) look: vec4f,\n"
    "    @location(5) gap: vec2f,\n"
    "    @location(6) tint2: vec4f,\n"
    "    @location(7) rim: vec4f,\n"
    "}\n"
    "@vertex fn vertex(@location(0) corner: vec2f, @location(1) shape: vec4f, @location(2) color: vec4f,\n"
    "                  @location(3) kind: f32, @location(4) turn: vec4f, @location(5) fade: vec2f,\n"
    "                  @location(6) color2: vec4f, @location(7) rim_color: vec4f) -> Corner {\n"
    "    let c = corner * 2.0 - 1.0;\n"
    "    var out: Corner;\n"
    "    var p: vec2f;\n"
    "    if (kind > 4.5) {\n"
    "        out.extent = abs(shape.zw);\n"
    "        out.local = c * (out.extent + max(fade.x, 0.0) + 1.0);\n"
    "        p = shape.xy + vec2f(out.local.x * turn.x - out.local.y * turn.y,\n"
    "                             out.local.x * turn.y + out.local.y * turn.x);\n"
    "    } else " SHAPE_WGSL_PLACE
    "    out.tint = color;\n"
    "    out.form = kind;\n"
    "    out.look = vec4f(turn.z, turn.w, fade.x, 0.0);\n"
    "    out.gap = vec2f(cos(fade.y), sin(fade.y));\n"
    "    out.tint2 = color2;\n"
    "    out.rim = rim_color;\n"
    "    out.position = u.screen * vec4f(p, 0.0, 1.0);\n"
    "    return out;\n"
    "}\n");

static const char *const smooth_shape_wgsl_fragment = WGSL(
    "fn ellipse(at: vec2f, radii: vec2f) -> f32 {\n"
    "    let q = abs(at);\n"
    "    var t = vec2f(0.70710678);\n"
    "    let e = vec2f(radii.x * radii.x - radii.y * radii.y, radii.y * radii.y - radii.x * radii.x) / radii;\n"
    "    for (var i = 0; i < 3; i++) {\n"
    "        let m = e * t * t * t;\n"
    "        let w = q - m;\n"
    "        t = clamp((w * (length(radii * t - m) / max(length(w), 0.000001)) + m) / radii, vec2f(0.0), vec2f(1.0));\n"
    "        let n = length(t);\n"
    "        if (n > 0.00001) { t = t / n; } else { t = vec2f(0.0, 1.0); }\n"
    "    }\n"
    "    for (var i = 0; i < 2; i++) {\n"
    "        let on = radii * t;\n"
    "        let along = radii * vec2f(-t.y, t.x);\n"
    "        let to = q - on;\n"
    "        let span = dot(along, along);\n"
    "        let curve = span + dot(to, on);\n"
    "        if (curve > 0.25 * span) {\n"
    "            let slide = dot(to, along) / curve;\n"
    "            let next = clamp(vec2f(t.x - t.y * slide, t.y + t.x * slide), vec2f(0.0), vec2f(1.0));\n"
    "            let n = length(next);\n"
    "            if (n > 0.00001) { t = next / n; }\n"
    "        }\n"
    "    }\n"
    "    let apart = length(q - radii * t);\n"
    "    let s = q / radii;\n"
    "    if (dot(s, s) < 1.0) { return -apart; }\n"
    "    return apart;\n"
    "}\n"
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"
    "    if (in.form < 4.5) {\n"
    "    " SHAPE_WGSL_COVER
    "        return vec4f(in.tint.rgb, in.tint.a * cover);\n"
    "    }\n"
    "    let px = (u.scale.x + u.scale.y) * 0.5;\n"
    "    var d: f32;\n"
    "    var outline = in.look.y;\n"
    "    if (in.form > 6.5) {\n"
    "        let q = vec2f(in.local.x, abs(in.local.y));\n"
    "        let ring = abs(length(q) - in.extent.x + outline * 0.5) - outline * 0.5;\n"
    "        var wedge = dot(q, vec2f(-in.gap.y, in.gap.x));\n"
    "        if (dot(q, in.gap) <= 0.0) {\n"
    "            if (in.gap.x > 0.0) { wedge = length(q); } else { wedge = -length(q); }\n"
    "        }\n"
    "        d = min(max(ring, wedge), 0.0) + length(max(vec2f(ring, wedge), vec2f(0.0)));\n"
    "        outline = 0.0;\n"
    "    } else if (in.form > 5.5) {\n"
    "        d = ellipse(in.local, in.extent);\n"
    "    } else {\n"
    "        let q = abs(in.local) - in.extent + in.look.x;\n"
    "        d = length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - in.look.x;\n"
    "    }\n"
    "    var cover = clamp(0.5 - d * px, 0.0, 1.0);\n"
    "    if (in.look.z > 0.0) {\n"
    "        let glow = clamp(1.0 - (d * px + 0.5) / (in.look.z * px + 1.0), 0.0, 1.0);\n"
    "        cover = glow * glow;\n"
    "    } else if (in.look.z < 0.0) {\n"
    "        let glow = clamp(1.0 + (d * px - 0.5) / (1.0 - in.look.z * px), 0.0, 1.0);\n"
    "        cover *= glow * glow;\n"
    "    }\n"
    "    if (cover <= 0.0) { discard; }\n"
    "    let down = clamp(in.local.y / max(in.extent.y, 0.000001) * 0.5 + 0.5, 0.0, 1.0);\n"
    "    var paint = mix(vec4f(in.tint.rgb * in.tint.a, in.tint.a), vec4f(in.tint2.rgb * in.tint2.a, in.tint2.a), down);\n"
    "    if (outline > 0.0) {\n"
    "        paint = mix(vec4f(in.rim.rgb * in.rim.a, in.rim.a), paint, clamp(0.5 - (d + outline) * px, 0.0, 1.0));\n"
    "    }\n"
    "    if (paint.a <= 0.0) { discard; }\n"
    "    return vec4f(paint.rgb / paint.a, paint.a * cover);\n"
    "}\n");

// Meshes: a mesh's pixels are its corners' colors, blended across each
// triangle, times its texture's (a white pixel, for a mesh with none).

static const char mesh_glsl_vertex[] =
    "layout(location = 0) in vec2 position;\n" // In window pixels
    "layout(location = 1) in vec2 uv;\n"
    "layout(location = 2) in vec4 color;\n"
    "layout(std140) uniform uniforms { mat4 screen; vec2 texel; };\n"
    "out vec2 at;\n"
    "out vec4 tint;\n"
    "void main() {\n"
    "    at = uv;\n"
    "    tint = color;\n"
    "    gl_Position = screen * vec4(position, 0.0, 1.0);\n"
    "}\n";

static const char mesh_glsl_fragment[] =
    "in vec2 at;\n"
    "in vec4 tint;\n"
    "uniform sampler2D pixels0;\n"
    "out vec4 pixel;\n"
    "void main() {\n"
    "    pixel = texture(pixels0, at) * tint;\n"
    "}\n";

#define MESH_WGSL_VERTEX                                                                                               \
    "struct Uniforms { screen: mat4x4f, texel: vec2f }\n"                                                              \
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"                                                                \
    "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"                                                            \
    "@group(0) @binding(2) var how0: sampler;\n"                                                                       \
    "struct Corner {\n"                                                                                                \
    "    @builtin(position) position: vec4f,\n"                                                                        \
    "    @location(0) at: vec2f,\n"                                                                                    \
    "    @location(1) tint: vec4f,\n"                                                                                  \
    "}\n"                                                                                                              \
    "@vertex fn vertex(@location(0) position: vec2f, @location(1) uv: vec2f, @location(2) color: vec4f) -> Corner {\n" \
    "    var out: Corner;\n"                                                                                           \
    "    out.at = uv;\n"                                                                                               \
    "    out.tint = color;\n"                                                                                          \
    "    out.position = u.screen * vec4f(position, 0.0, 1.0);\n"                                                       \
    "    return out;\n"                                                                                                \
    "}\n"

static const char *const mesh_wgsl = WGSL(MESH_WGSL_VERTEX
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"
    "    return textureSample(pixels0, how0, in.at) * in.tint;\n"
    "}\n");

// Text: its pixels are its color, as much as the font atlas says its glyph
// covers each. Its corners are a mesh's, their uv in the atlas's pixels, so
// the atlas can grow under them: `texel` is 1 over its size.
//
// Colors blend as the display's values, not as light, which leaves a pixel
// that light text half covers darker than half: a glyph's slanted and round
// strokes, which cover pixels in part, would fade beside its stems, which
// fill theirs. So the lighter the text, the more a part counts for: its share
// to a power under 1, down to 0.55 for white (about what blending as light
// would give it on a dark background). Dark text is left as it is.

static const char text_glsl_fragment[] =
    "in vec2 at;\n"
    "in vec4 tint;\n"
    "uniform sampler2D pixels0;\n"
    "layout(std140) uniform uniforms { mat4 screen; vec2 texel; };\n"
    "out vec4 pixel;\n"
    "void main() {\n"
    "    float cover = texture(pixels0, at * texel).r;\n"
    "    float light = dot(tint.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
    "    if (cover > 0.0) cover = pow(cover, 1.0 - 0.45 * light);\n"
    "    pixel = vec4(tint.rgb, tint.a * cover);\n"
    "}\n";

static const char *const text_wgsl = WGSL(MESH_WGSL_VERTEX
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"
    "    var cover = textureSample(pixels0, how0, in.at * u.texel).r;\n"
    "    let light = dot(in.tint.rgb, vec3f(0.2126, 0.7152, 0.0722));\n"
    "    if (cover > 0.0) { cover = pow(cover, 1.0 - 0.45 * light); }\n"
    "    return vec4f(in.tint.rgb, in.tint.a * cover);\n"
    "}\n");

// 3D meshes: a mesh's corners go through its instance's transform and the
// camera, with depth; its pixels are as a mesh's.

static const char mesh_3d_glsl_vertex[] =
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 uv;\n"
    "layout(location = 2) in vec4 color;\n"
    "layout(location = 3) in vec4 model0;\n" // Its instance's transform, column by column
    "layout(location = 4) in vec4 model1;\n"
    "layout(location = 5) in vec4 model2;\n"
    "layout(location = 6) in vec4 model3;\n"
    "layout(std140) uniform uniforms { mat4 camera; };\n" // From the world to clip space
    "out vec2 at;\n"
    "out vec4 tint;\n"
    "void main() {\n"
    "    at = uv;\n"
    "    tint = color;\n"
    "    gl_Position = camera * (mat4(model0, model1, model2, model3) * vec4(position, 1.0));\n"
    "}\n";

static const char *const mesh_3d_wgsl = WGSL(
    "struct Uniforms { camera: mat4x4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"
    "@group(0) @binding(2) var how0: sampler;\n"
    "struct Corner {\n"
    "    @builtin(position) position: vec4f,\n"
    "    @location(0) at: vec2f,\n"
    "    @location(1) tint: vec4f,\n"
    "}\n"
    "@vertex fn vertex(@location(0) position: vec3f, @location(1) uv: vec2f, @location(2) color: vec4f,\n"
    "                  @location(3) model0: vec4f, @location(4) model1: vec4f, @location(5) model2: vec4f,\n"
    "                  @location(6) model3: vec4f) -> Corner {\n"
    "    let p = u.camera * (mat4x4f(model0, model1, model2, model3) * vec4f(position, 1.0));\n"
    "    var out: Corner;\n"
    "    out.at = uv;\n"
    "    out.tint = color;\n"
    "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n" // The camera's clip space is OpenGL's: depth from -1
    "    return out;\n"
    "}\n"
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"
    "    return textureSample(pixels0, how0, in.at) * in.tint;\n"
    "}\n");

// A clear: one triangle over the whole target, as far as depth goes, in one
// color, which the clip cuts as it does everything else.

static const char clear_glsl_vertex[] =
    "void main() {\n"
    "    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n" // (0, 0), (2, 0), (0, 2)
    "    gl_Position = vec4(corner * 2.0 - 1.0, 1.0, 1.0);\n"
    "}\n";

static const char clear_glsl_fragment[] =
    "layout(std140) uniform uniforms { vec4 color; };\n"
    "out vec4 pixel;\n"
    "void main() {\n"
    "    pixel = color;\n"
    "}\n";

static const char *const clear_wgsl = WGSL(
    "struct Uniforms { color: vec4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
    "    let corner = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
    "    return vec4f(corner * 2.0 - 1.0, 1.0, 1.0);\n"
    "}\n"
    "@fragment fn fragment() -> @location(0) vec4f {\n"
    "    return u.color;\n"
    "}\n");
