layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    vec4 tide_u0;
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
float tide_pow1(float a, float b) {
    if (b == 0.0) { return 1.0; }
    bool w = a < 0.0 && floor(b) == b;
    float x = a;
    if (w) { x = -a; }
    float p = pow(x, b);
    if (w && floor(b * 0.5) * 2.0 != b) { return -p; }
    return p;
}
vec4 tide_straight(vec4 c) {
    if (c.a == 0.0) { return vec4(0.0); }
    return vec4(c.rgb / c.a, c.a);
}
float tf_Render3D_1LinearToSrgb_3_0float(float t_value) {
    float t_channel = tide_max1(t_value, tide_f(0.0));
    if ((t_channel <= 0.0031308)) {
        return (12.92 * t_channel);
    }
    return ((1.055 * tide_pow1(t_channel, (tide_f(1.0) / tide_f(2.4)))) - 0.055);
}

vec3 tf_Render3D_1LinearToSrgb_3_0float3(vec3 t_value) {
    return vec3(tf_Render3D_1LinearToSrgb_3_0float(t_value.x), tf_Render3D_1LinearToSrgb_3_0float(t_value.y), tf_Render3D_1LinearToSrgb_3_0float(t_value.z));
}

vec3 tf_Render3D_1Tonemap(vec3 t_radiance, float t_exposure) {
    vec3 t_light = tide_clamp3(t_radiance, vec3(tide_f(0.0)), vec3(tide_f(65504.0)));
    float t_scale = tide_clamp1(t_exposure, tide_f(0.0), tide_f(1000000.0));
    vec3 t_exposed = (t_light * t_scale);
    return (t_exposed / (vec3(1.0) + t_exposed));
}

void ts_Render3D_1TonemapScene(tt_Vertex t_vertex, inout vec4 t_color) {
    vec4 t_hdr = tide_straight(textureLod(pixels0, t_vertex.m_uv, 0.0));
    vec3 t_display = tf_Render3D_1LinearToSrgb_3_0float3(tf_Render3D_1Tonemap(vec3(t_hdr.r, t_hdr.g, t_hdr.b), tide_u0.x));
    t_color = vec4(t_display.x, t_display.y, t_display.z, 1.0);
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    ts_Render3D_1TonemapScene(tt_Vertex(vec2(0.0), tide_s1, vec4(0.0)), tide_color);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_pixel = tide_color;
}
