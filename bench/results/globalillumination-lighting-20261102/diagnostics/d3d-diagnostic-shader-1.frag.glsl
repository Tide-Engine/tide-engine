layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    vec4 tide_u0;
    ivec4 tide_v0;
};
uniform sampler2D pixels0;
in vec2 tide_s1;
out vec4 tide_pixel;
struct tt_Vertex {
    vec2 m_position;
    vec2 m_uv;
    vec4 m_color;
};
float tide_f(float x) {
    return x;
}
int tide_i(int x) {
    return x;
}
bool tide_nan(float x) {
    return (floatBitsToInt(x) & 0x7fffffff) > 0x7f800000;
}
float tide_min1(float a, float b) {
    if (tide_nan(b)) { return a; }
    if (tide_nan(a)) { return b; }
    if (a < b) { return a; }
    return b;
}
float tide_max1(float a, float b) {
    if (tide_nan(b)) { return a; }
    if (tide_nan(a)) { return b; }
    if (a > b) { return a; }
    return b;
}
float tide_clamp1(float a, float b, float c) {
    if (tide_nan(a)) { return b; }
    return tide_max1(b, tide_min1(c, a));
}
vec3 tide_clamp3(vec3 a, vec3 b, vec3 c) {
    return vec3(tide_clamp1(a.x, b.x, c.x), tide_clamp1(a.y, b.y, c.y), tide_clamp1(a.z, b.z, c.z));
}
vec4 tide_straight(vec4 c) {
    if (c.a == 0.0) { return vec4(0.0); }
    return vec4(c.rgb / c.a, c.a);
}
vec3 tf_Render3D_1ClampHdr(vec3 t_radiance) {
    return tide_clamp3(t_radiance, vec3(tide_f(0.0)), vec3(tide_f(65504.0)));
}

vec3 tf_Render3D_1BlurBloomSamples(vec3 t_center, vec3 t_nearPlus, vec3 t_nearMinus, vec3 t_farPlus, vec3 t_farMinus) {
    return tf_Render3D_1ClampHdr((((tf_Render3D_1ClampHdr(t_center) * (tide_f(84.0) / tide_f(370.0))) + ((tf_Render3D_1ClampHdr(t_nearPlus) + tf_Render3D_1ClampHdr(t_nearMinus)) * (tide_f(117.0) / tide_f(370.0)))) + ((tf_Render3D_1ClampHdr(t_farPlus) + tf_Render3D_1ClampHdr(t_farMinus)) * (tide_f(26.0) / tide_f(370.0)))));
}

vec3 tf_Render3D_1HdrRgb(vec4 t_color) {
    return tf_Render3D_1ClampHdr(vec3(t_color.r, t_color.g, t_color.b));
}

void ts_Render3D_1BlurBloom(tt_Vertex t_vertex, inout vec4 t_color) {
    vec2 t_pixel = (tide_u0.xy / vec2(max(tide_v0.xy, ivec2(tide_i(1)))));
    vec2 t_near = (t_pixel * (tide_f(18.0) / tide_f(13.0)));
    vec2 t_far = (t_pixel * (tide_f(42.0) / tide_f(13.0)));
    vec3 t_light = tf_Render3D_1BlurBloomSamples(tf_Render3D_1HdrRgb(tide_straight(textureLod(pixels0, t_vertex.m_uv, 0.0))), tf_Render3D_1HdrRgb(tide_straight(textureLod(pixels0, (t_vertex.m_uv + t_near), 0.0))), tf_Render3D_1HdrRgb(tide_straight(textureLod(pixels0, (t_vertex.m_uv - t_near), 0.0))), tf_Render3D_1HdrRgb(tide_straight(textureLod(pixels0, (t_vertex.m_uv + t_far), 0.0))), tf_Render3D_1HdrRgb(tide_straight(textureLod(pixels0, (t_vertex.m_uv - t_far), 0.0))));
    t_color = vec4(t_light, 1.0);
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    ts_Render3D_1BlurBloom(tt_Vertex(vec2(0.0), tide_s1, vec4(0.0)), tide_color);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_pixel = tide_color;
}
