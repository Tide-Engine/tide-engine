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
struct tt_Render3D_1BloomSettings {
    bool m_enabled;
    float m_threshold;
    float m_softness;
    float m_intensity;
    float m_radius;
    int m_iterations;
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

vec3 tf_Render3D_1HdrRgb(vec4 t_color) {
    return tf_Render3D_1ClampHdr(vec3(t_color.r, t_color.g, t_color.b));
}

vec3 tf_Render3D_1BloomBright(vec3 t_radiance, tt_Render3D_1BloomSettings t_settings) {
    vec3 t_light = tf_Render3D_1ClampHdr(t_radiance);
    float t_brightness = tide_max1(tide_max1(t_light.x, t_light.y), t_light.z);
    if ((t_brightness <= 0.0)) {
        return vec3(0.0);
    }
    float t_threshold = tide_clamp1(t_settings.m_threshold, tide_f(0.0), tide_f(65504.0));
    float t_knee = tide_clamp1(t_settings.m_softness, tide_f(0.0), tide_f(65504.0));
    float t_excess = tide_max1((t_brightness - t_threshold), tide_f(0.0));
    if ((t_knee > 0.0)) {
        float t_soft = tide_clamp1(((t_brightness - t_threshold) + t_knee), tide_f(0.0), (tide_f(2.0) * t_knee));
        t_excess = tide_max1(t_excess, ((t_soft * t_soft) / (tide_f(4.0) * t_knee)));
    }
    return (t_light * (tide_min1(t_excess, t_brightness) / t_brightness));
}

void ts_Render3D_1ExtractBloom(tt_Vertex t_vertex, inout vec4 t_color) {
    vec2 t_pixel = (1.0 / vec2(max(tide_v0.zw, ivec2(tide_i(1)))));
    vec3 t_light = ((((tf_Render3D_1HdrRgb(tide_straight(textureLod(pixels0, (t_vertex.m_uv + vec2((-t_pixel.x), (-t_pixel.y))), 0.0))) + tf_Render3D_1HdrRgb(tide_straight(textureLod(pixels0, (t_vertex.m_uv + vec2(t_pixel.x, (-t_pixel.y))), 0.0)))) + tf_Render3D_1HdrRgb(tide_straight(textureLod(pixels0, (t_vertex.m_uv + vec2((-t_pixel.x), t_pixel.y)), 0.0)))) + tf_Render3D_1HdrRgb(tide_straight(textureLod(pixels0, (t_vertex.m_uv + vec2(t_pixel.x, t_pixel.y)), 0.0)))) * 0.25);
    t_color = vec4(tf_Render3D_1BloomBright(t_light, tt_Render3D_1BloomSettings((tide_v0.x != 0), tide_u0.x, tide_u0.y, tide_u0.z, tide_u0.w, tide_v0.y)), 1.0);
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    ts_Render3D_1ExtractBloom(tt_Vertex(vec2(0.0), tide_s1, vec4(0.0)), tide_color);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_pixel = tide_color;
}
