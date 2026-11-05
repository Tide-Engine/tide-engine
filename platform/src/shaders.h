#pragma once

// The renderer's shaders (platform.c), in WGSL: WebGPU's on the web and, as
// SPIR-V, Vulkan's on desktop and Android, translated as the platform layer
// builds (platform/tools/spirv_shaders.c, into shaders_spirv.h's). They're
// written by hand. (Until 2026-11-05 each had a GLSL twin for OpenGL 3.3,
// OpenGL ES 3 and WebGL 2, which went with those backends.)

// WGSL is the web's, and the SPIR-V tool's.
#if defined(__wasm__) || defined(TIDE_SHADERS_WGSL)
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
// shaders have no smooth-shape work, so such a run costs no more.
// The second draws every kind: a run with a smooth shape
// in it goes through that one, each shape with its extra (see shape_extra in
// platform.c). Both draw the plain kinds from the same lines below, so a rect
// is the same pixels whichever its run goes through.
//
// The second one's `scale` is the target's own pixels in a logical one,
// across and down: a smooth shape's edge is smooth over one of the target's,
// whatever the display's scale. A plain circle's is smooth over a logical
// pixel, which is the same where a display isn't scaled; where it is, the
// renderer draws circles as smooth shapes (see add_circle in platform.c).
//
// Every pipeline but the first is made once for each way of blending (see
// tide_gpu_blend), from the same shaders. What multiplies what's there needs
// a color that's gone to white where its alpha is 0, so that what's clear
// changes nothing: `fix.x` is 1 for those and 0 for the others, whose colors
// it leaves as they are. A run of plain shapes that multiplies goes through
// the second pipeline, so the first's shaders have none of it.
//
// Each is also made for each thing a draw does with stencil, from the same
// shaders again. Where a draw stores stencil, the pixels it stores at are the
// ones it draws at, so those have to be the ones it touches, whatever its
// color: `fix.y` is 1 for those draws and 0 for the others. A smooth shape
// that's clear is drawn then, where it's otherwise passed by, and text's
// pixels that no glyph covers aren't, where they're otherwise drawn clear.
// The plain kinds already draw exactly the pixels they touch. A pixel counts
// as touched from a 255th of it on, the least a color shows: a smooth shape
// whose edge is on the pixels' own is then the same pixels on every GPU,
// where the pixel just past it, which it covers none of, could come out as a
// millionth on one of them.

// A color as its pipeline blends it: times its alpha, which is how every
// pipeline's blend takes what a shader writes (see tide_gpu_blend), and
// towards white as it clears, by `by`. `whiten` takes a color whose alpha
// isn't worked into it yet. `over` takes a texture's pixel, which has its
// own worked in already, and the tint it's drawn with, which hasn't.
#define WGSL_WHITEN \
    "fn whiten(c: vec4f, by: f32) -> vec4f { return vec4f(c.rgb * c.a + by * (1.0 - c.a), c.a); }\n" \
    "fn over(p: vec4f, tint: vec4f, by: f32) -> vec4f {\n" \
    "    let c = p * vec4f(tint.rgb * tint.a, tint.a);\n" \
    "    return vec4f(c.rgb + by * (1.0 - c.a), c.a);\n" \
    "}\n"

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

#define SHAPE_WGSL_UNIFORMS                                                      \
    "struct Uniforms { screen: mat4x4f, texel: vec2f, scale: vec2f, fix: vec4f }\n" \
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"

// Where a plain kind's corner goes. `c` is the quad's, from -1 to 1.
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

// How much of a pixel a plain kind covers.
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
    "    return vec4f(in.tint.rgb * (in.tint.a * cover), in.tint.a * cover);\n"
    "}\n");

// In two pieces, which the renderer joins: together they're longer than C
// promises a string literal can be.
#define SMOOTH_SHAPE_WGSL_CORNER                                                                                       \
    SHAPE_WGSL_UNIFORMS                                                                                                \
    "struct Corner {\n"                                                                                                \
    "    @builtin(position) position: vec4f,\n"                                                                        \
    "    @location(0) local: vec2f,\n"                                                                                 \
    "    @location(1) extent: vec2f,\n"                                                                                \
    "    @location(2) tint: vec4f,\n"                                                                                  \
    "    @location(3) form: f32,\n"                                                                                    \
    "    @location(4) look: vec4f,\n"                                                                                  \
    "    @location(5) gap: vec2f,\n"                                                                                   \
    "    @location(6) tint2: vec4f,\n"                                                                                 \
    "    @location(7) rim: vec4f,\n"                                                                                   \
    "}\n"

// From a shape's corner, its numbers (shape, color, kind) and its extra's (turn, fade, color2, rim_color).
#define SMOOTH_SHAPE_WGSL_MAIN                                                                                         \
    "    let c = corner * 2.0 - 1.0;\n"                                                                                \
    "    var out: Corner;\n"                                                                                           \
    "    var p: vec2f;\n"                                                                                              \
    "    if (kind > 4.5) {\n"                                                                                          \
    "        out.extent = abs(shape.zw);\n"                                                                            \
    "        out.local = c * (out.extent + select(max(fade.x, 0.0), 0.0, kind > 7.5) + 1.0);\n"                                                 \
    "        p = shape.xy + vec2f(out.local.x * turn.x - out.local.y * turn.y,\n"                                      \
    "                             out.local.x * turn.y + out.local.y * turn.x);\n"                                     \
    "    } else " SHAPE_WGSL_PLACE                                                                                     \
    "    out.tint = color;\n"                                                                                          \
    "    out.form = kind;\n"                                                                                           \
    "    out.look = select(vec4f(turn.z, turn.w, fade.x, 0.0), vec4f(turn.zw, fade), kind > 7.5);\n"                                                             \
    "    out.gap = vec2f(cos(fade.y), sin(fade.y));\n"                                                                 \
    "    out.tint2 = color2;\n"                                                                                        \
    "    out.rim = rim_color;\n"                                                                                       \
    "    out.position = u.screen * vec4f(p, 0.0, 1.0);\n"                                                              \
    "    return out;\n"

static const char *const smooth_shape_wgsl_vertex = WGSL(
    SMOOTH_SHAPE_WGSL_CORNER
    "@vertex fn vertex(@location(0) corner: vec2f, @location(1) shape: vec4f, @location(2) color: vec4f,\n"
    "                  @location(3) kind: f32, @location(4) turn: vec4f, @location(5) fade: vec2f,\n"
    "                  @location(6) color2: vec4f, @location(7) rim_color: vec4f) -> Corner {\n"
    SMOOTH_SHAPE_WGSL_MAIN
    "}\n");

// A run whose smooth shapes are all solid ones (see solid_shape in
// platform.c): a box or an ellipse, unturned, of one color, with no outline
// and no fade. Of its extra, such a shape needs only its corners' radius,
// which is all its run has beside its shapes: the rest is what its extra
// would have said, for the same lines above and the same fragment shader.
static const char *const solid_shape_wgsl_vertex = WGSL(
    SMOOTH_SHAPE_WGSL_CORNER
    "@vertex fn vertex(@location(0) corner: vec2f, @location(1) shape: vec4f, @location(2) color: vec4f,\n"
    "                  @location(3) kind: f32, @location(4) radius: f32) -> Corner {\n"
    "    let turn = vec4f(1.0, 0.0, radius, 0.0);\n"
    "    let fade = vec2f(0.0, 0.0);\n"
    "    let color2 = color;\n"
    "    let rim_color = vec4f(0.0);\n"
    SMOOTH_SHAPE_WGSL_MAIN
    "}\n");

static const char *const smooth_shape_wgsl_fragment = WGSL(
    WGSL_WHITEN
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
    "        return whiten(vec4f(in.tint.rgb, in.tint.a * cover), u.fix.x);\n"
    "    }\n"
    "    let px = (u.scale.x + u.scale.y) * 0.5;\n"
    "    var d: f32;\n"
    "    var outline = in.look.y;\n"
    "    let soft = select(in.look.z, 0.0, in.form > 7.5);\n"
    "    if (in.form > 7.5) {\n"
    "        let top = select(in.look.y, in.look.x, in.local.x < 0.0);\n"
    "        let bottom = select(in.look.z, in.look.w, in.local.x < 0.0);\n"
    "        let r = select(bottom, top, in.local.y < 0.0);\n"
    "        let q = abs(in.local) - in.extent + r;\n"
    "        d = length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;\n"
    "        outline = 0.0;\n"
    "    } else if (in.form > 6.5) {\n"
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
    "    if (soft > 0.0) {\n"
    "        let glow = clamp(1.0 - (d * px + 0.5) / (soft * px + 1.0), 0.0, 1.0);\n"
    "        cover = glow * glow;\n"
    "    } else if (soft < 0.0) {\n"
    "        let glow = clamp(1.0 + (d * px - 0.5) / (1.0 - soft * px), 0.0, 1.0);\n"
    "        cover *= glow * glow;\n"
    "    }\n"
    "    if (cover <= u.fix.y * 0.004) { discard; }\n"
    "    let down = clamp(in.local.y / max(in.extent.y, 0.000001) * 0.5 + 0.5, 0.0, 1.0);\n"
    "    var paint = mix(vec4f(in.tint.rgb * in.tint.a, in.tint.a), vec4f(in.tint2.rgb * in.tint2.a, in.tint2.a), down);\n"
    "    if (outline > 0.0) {\n"
    "        paint = mix(vec4f(in.rim.rgb * in.rim.a, in.rim.a), paint, clamp(0.5 - (d + outline) * px, 0.0, 1.0));\n"
    "    }\n"
    "    if (paint.a <= 0.0) {\n"
    "        if (u.fix.y < 0.5) { discard; }\n"
    "        return whiten(vec4f(0.0), u.fix.x);\n"
    "    }\n"
    "    return paint * cover + vec4f(vec3f(u.fix.x * (1.0 - paint.a * cover)), 0.0);\n"
    "}\n");

// Meshes: a mesh's pixels are its corners' colors, blended across each
// triangle, times its texture's (a white pixel, for a mesh with none). The
// vertex stage hands the pixels `fix.x` (see above), as 2D and 3D meshes
// share their fragment shader and not their uniforms.

#define MESH_WGSL_CORNER                                                                                               \
    SHAPE_WGSL_UNIFORMS                                                                                                \
    "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"                                                            \
    "@group(0) @binding(2) var how0: sampler;\n"                                                                       \
    "struct Corner {\n"                                                                                                \
    "    @builtin(position) position: vec4f,\n"                                                                        \
    "    @location(0) at: vec2f,\n"                                                                                    \
    "    @location(1) tint: vec4f,\n"                                                                                  \
    "    @location(2) white: f32,\n"                                                                                   \
    "}\n"                                                                                                              \
    WGSL_WHITEN

#define MESH_WGSL_VERTEX                                                                                               \
    MESH_WGSL_CORNER                                                                                                   \
    "@vertex fn vertex(@location(0) position: vec2f, @location(1) uv: vec2f, @location(2) color: vec4f) -> Corner {\n" \
    "    var out: Corner;\n"                                                                                           \
    "    out.at = uv;\n"                                                                                               \
    "    out.tint = color;\n"                                                                                          \
    "    out.white = u.fix.x;\n"                                                                                       \
    "    out.position = u.screen * vec4f(position, 0.0, 1.0);\n"                                                       \
    "    return out;\n"                                                                                                \
    "}\n"

#define MESH_WGSL_FRAGMENT                                                                                             \
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"                                                      \
    "    return over(textureSample(pixels0, how0, in.at), in.tint, in.white);\n"                                       \
    "}\n"

static const char *const mesh_wgsl = WGSL(MESH_WGSL_VERTEX MESH_WGSL_FRAGMENT);

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

#define TEXT_WGSL_FRAGMENT                                                                                             \
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"                                                      \
    "    var cover = textureSample(pixels0, how0, in.at * u.texel).r;\n"                                              \
    "    if (u.fix.y > 0.5 && cover < 0.002) { discard; }\n"                                                           \
    "    let light = dot(in.tint.rgb, vec3f(0.2126, 0.7152, 0.0722));\n"                                               \
    "    if (cover > 0.0) { cover = pow(cover, 1.0 - 0.45 * light); }\n"                                               \
    "    return whiten(vec4f(in.tint.rgb, in.tint.a * cover), u.fix.x);\n"                                             \
    "}\n"

static const char *const text_wgsl = WGSL(MESH_WGSL_VERTEX TEXT_WGSL_FRAGMENT);

// Quads: two of a mesh's or of text's triangles at once, an instance each
// (see quad in platform.c), whose corners each take one of the instance's two
// numbers on each axis, by the four bits of `pattern` that are the corner's:
// the meshes' and the text's pixels from the very numbers their six vertices
// would have had, through the same arithmetic, and their fragment shaders.

#define QUAD_WGSL_VERTEX                                                                                               \
    MESH_WGSL_CORNER                                                                                                   \
    "@vertex fn vertex(@builtin(vertex_index) corner: u32, @location(0) box: vec4f, @location(1) span: vec4f,\n"      \
    "                  @location(2) color: vec4f, @location(3) pattern: i32) -> Corner {\n"                           \
    "    let which = pattern >> (4u * corner);\n"                                                                      \
    "    let position = vec2f(select(box.x, box.z, (which & 1) != 0), select(box.y, box.w, (which & 2) != 0));\n"   \
    "    var out: Corner;\n"                                                                                           \
    "    out.at = vec2f(select(span.x, span.z, (which & 4) != 0), select(span.y, span.w, (which & 8) != 0));\n"     \
    "    out.tint = color;\n"                                                                                          \
    "    out.white = u.fix.x;\n"                                                                                       \
    "    out.position = u.screen * vec4f(position, 0.0, 1.0);\n"                                                       \
    "    return out;\n"                                                                                                \
    "}\n"

static const char *const quad_wgsl = WGSL(QUAD_WGSL_VERTEX MESH_WGSL_FRAGMENT);
static const char *const text_quad_wgsl = WGSL(QUAD_WGSL_VERTEX TEXT_WGSL_FRAGMENT);

// 3D meshes: a mesh's corners go through its instance's transform and the
// camera, with depth; its pixels are as a mesh's, times `tint`, the run's.

static const char *const mesh_3d_wgsl = WGSL(
    "struct Uniforms { camera: mat4x4f, tint: vec4f, fix: vec4f, origin: vec4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"
    "@group(0) @binding(2) var how0: sampler;\n"
    "struct Corner {\n"
    "    @builtin(position) position: vec4f,\n"
    "    @location(0) at: vec2f,\n"
    "    @location(1) tint: vec4f,\n"
    "    @location(2) white: f32,\n"
    "}\n"
    WGSL_WHITEN
    "@vertex fn vertex(@location(0) position: vec3f, @location(1) uv: vec2f, @location(2) color: vec4f,\n"
    "                  @location(3) model0: vec4f, @location(4) model1: vec4f, @location(5) model2: vec4f,\n"
    "                  @location(6) model3: vec4f) -> Corner {\n"
    "    let relative = mat4x4f(model0 - u.origin * model0.w, model1 - u.origin * model1.w,\n"
    "                          model2 - u.origin * model2.w, model3 - u.origin * model3.w);\n"
    "    let p = u.camera * (relative * vec4f(position, 1.0));\n"
    "    var out: Corner;\n"
    "    out.at = uv;\n"
    "    out.tint = color * u.tint;\n"
    "    out.white = u.fix.x;\n"
    "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n" // The camera's clip space is OpenGL's: depth from -1
    "    return out;\n"
    "}\n"
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"
    "    return over(textureSample(pixels0, how0, in.at), in.tint, in.white);\n"
    "}\n");

// A clear: one triangle over the whole target, as far as depth goes, in one
// color, which the clip cuts as it does everything else.

static const char *const clear_wgsl = WGSL(
    "struct Uniforms { color: vec4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
    "    let corner = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
    "    return vec4f(corner * 2.0 - 1.0, 1.0, 1.0);\n"
    "}\n"
    "@fragment fn fragment() -> @location(0) vec4f {\n"
    "    return vec4f(u.color.rgb * u.color.a, u.color.a);\n"
    "}\n");

// A clear of a target of several color images: the same color into each of
// them, as many as it has.

static const char *const clear_all_wgsl = WGSL(
    "struct Uniforms { color: vec4f }\n"
    "struct Colors { @location(0) a: vec4f, @location(1) b: vec4f, @location(2) c: vec4f, @location(3) d: vec4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
    "    let corner = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
    "    return vec4f(corner * 2.0 - 1.0, 1.0, 1.0);\n"
    "}\n"
    "@fragment fn fragment() -> Colors {\n"
    "    let c = vec4f(u.color.rgb * u.color.a, u.color.a);\n"
    "    return Colors(c, c, c, c);\n"
    "}\n");

// The renderer's own kernels (compute.inc, notes/compute.md), WGSL alone: no GL backend runs kernels.
//
// A run over a GPU list's count, on the GPU: its record (TIDE_GPU_RUN_WORDS words: the dispatch's groups, its
// invocations, its size) from the list's count, for a kernel of `u.x` invocations a group along x. At most 65535
// groups, what every GPU dispatches along an axis.
static const char *const run_size_wgsl = WGSL(
    "@group(0) @binding(0) var<uniform> u: vec4<u32>;\n"
    "@group(0) @binding(25) var<storage, read> list: array<u32>;\n"
    "@group(0) @binding(26) var<storage, read_write> run: array<u32>;\n"
    "@compute @workgroup_size(1) fn compute(@builtin(local_invocation_index) i: u32) {\n"
    "    if (i == 0u) {\n"
    "        let count = min(list[0], list[1]);\n"
    "        let groups = min((count + u.x - 1u) / u.x, 65535u);\n"
    "        run[0] = groups;\n"
    "        run[1] = 1u;\n"
    "        run[2] = 1u;\n"
    "        run[3] = count;\n"
    "        run[4] = count;\n"
    "        run[5] = 1u;\n"
    "        run[6] = 1u;\n"
    "        run[7] = 0u;\n"
    "    }\n"
    "}\n");

// Ordered Add, once a kernel's groups have each written their survivors to their region of the scratch (`u.x`
// words each: how many, where they go, then them): where each group's go in the list, after what it holds (an
// exclusive prefix of the groups' counts, from the list's count), and the list's new count, no more than its
// capacity. One group of 256, each invocation a run of the groups.
static const char *const add_offsets_wgsl = WGSL(
    "@group(0) @binding(0) var<uniform> u: vec4<u32>;\n"
    "@group(0) @binding(25) var<storage, read_write> scratch: array<u32>;\n"
    "@group(0) @binding(26) var<storage, read_write> list: array<u32>;\n"
    "@group(0) @binding(27) var<storage, read> run: array<u32>;\n"
    "var<workgroup> sums: array<u32, 256>;\n"
    "@compute @workgroup_size(256) fn compute(@builtin(local_invocation_index) t: u32) {\n"
    "    let groups = run[0] * run[1] * run[2];\n"
    "    let each = (groups + 255u) / 256u;\n"
    "    let first = t * each;\n"
    "    let last = min(first + each, groups);\n"
    "    let base = list[0];\n"
    "    let capacity = list[1];\n"
    "    var mine = 0u;\n"
    "    for (var g = first; g < last; g = g + 1u) {\n"
    "        mine = mine + scratch[g * u.x];\n"
    "    }\n"
    "    sums[t] = mine;\n"
    "    workgroupBarrier();\n"
    "    for (var d = 1u; d < 256u; d = d * 2u) {\n"
    "        var v = 0u;\n"
    "        if (t >= d) {\n"
    "            v = sums[t - d];\n"
    "        }\n"
    "        workgroupBarrier();\n"
    "        sums[t] = sums[t] + v;\n"
    "        workgroupBarrier();\n"
    "    }\n"
    "    var at = base + sums[t] - mine;\n"
    "    for (var g = first; g < last; g = g + 1u) {\n"
    "        scratch[g * u.x + 1u] = at;\n"
    "        at = at + scratch[g * u.x];\n"
    "    }\n"
    "    if (t == 255u) {\n"
    "        list[0] = min(base + sums[255], capacity);\n"
    "    }\n"
    "}\n");

// ...then each group's survivors to where they go, `u.y` words each, those past the capacity dropped: one group of
// 64 for each of the kernel's.
static const char *const add_scatter_wgsl = WGSL(
    "@group(0) @binding(0) var<uniform> u: vec4<u32>;\n"
    "@group(0) @binding(25) var<storage, read> scratch: array<u32>;\n"
    "@group(0) @binding(26) var<storage, read_write> list: array<u32>;\n"
    "@compute @workgroup_size(64) fn compute(@builtin(local_invocation_index) t: u32, @builtin(workgroup_id) w: vec3u,\n"
    "                                        @builtin(num_workgroups) n: vec3u) {\n"
    "    let region = (w.x + n.x * (w.y + n.y * w.z)) * u.x;\n"
    "    let count = scratch[region];\n"
    "    let at = scratch[region + 1u];\n"
    "    let capacity = list[1];\n"
    "    for (var j = t; j < count; j = j + 64u) {\n"
    "        let to = at + j;\n"
    "        if (to < capacity) {\n"
    "            for (var k = 0u; k < u.y; k = k + 1u) {\n"
    "                list[4u + to * u.y + k] = scratch[region + 2u + j * u.y + k];\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "}\n");

// Each pipeline's WGSL as the renderer gives it, by the name its SPIR-V has
// (shaders_spirv.h): one piece, or two it joins (joined in platform.c), whose
// whole is longer than C promises a string literal can be. What its SPIR-V
// and its HLSL, Unity's (tide unity), are made of.
#define TIDE_RENDERER_WGSL(X)                                                                                          \
    X(shape, shape_wgsl, NULL)                                                                                         \
    X(smooth_shape, smooth_shape_wgsl_vertex, smooth_shape_wgsl_fragment)                                              \
    X(solid_shape, solid_shape_wgsl_vertex, smooth_shape_wgsl_fragment)                                                \
    X(mesh, mesh_wgsl, NULL)                                                                                           \
    X(text, text_wgsl, NULL)                                                                                           \
    X(quad, quad_wgsl, NULL)                                                                                           \
    X(text_quad, text_quad_wgsl, NULL)                                                                                 \
    X(mesh_3d, mesh_3d_wgsl, NULL)                                                                                     \
    X(clear, clear_wgsl, NULL)                                                                                         \
    X(clear_all, clear_all_wgsl, NULL)                                                                                 \
    X(run_size, run_size_wgsl, NULL)                                                                                   \
    X(add_offsets, add_offsets_wgsl, NULL)                                                                             \
    X(add_scatter, add_scatter_wgsl, NULL)
