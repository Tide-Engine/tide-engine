layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    vec4 tide_u0;
    vec4 tide_u1;
    ivec4 tide_v0;
    ivec4 tide_v1;
    ivec4 tide_v2;
};
uniform highp isampler2D pixels0;
uniform sampler2D pixels1;
uniform sampler2D pixels2;
in vec3 tide_s0;
out vec4 tide_pixel;
struct tt_Vertex3 {
    vec3 m_position;
    vec2 m_uv;
    vec4 m_color;
};
struct tt_GlobalIllumination_1FieldLayout {
    vec3 m_minimum;
    float m_spacing;
    ivec3 m_counts;
    int m_total;
    int m_atlasRows;
    float m_far;
    bool m_sparse;
    float m_size;
    int m_nodeCount;
    int m_depthLimit;
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
vec3 tide_normalize3(vec3 v) {
    float l = dot(v, v);
    if (l > 1.17549435e-38) { return v * inversesqrt(l); }
    return vec3(0.0);
}
int tide_int4_lane(ivec4 value, int index) {
    return index == 0 ? value.x : index == 1 ? value.y : index == 2 ? value.z : value.w;
}
vec4 tide_straight(vec4 c) {
    if (c.a == 0.0) { return vec4(0.0); }
    return vec4(c.rgb / c.a, c.a);
}
bool tide_list0(int tide_i) {
    bool tide_result = false;
    if (!(tide_i < 0 || tide_i >= tide_v2.y)) {
    int tide_word = tide_i * 1;
    int tide_first = tide_word >> 2;
    ivec4 tide_r = texelFetch(pixels0, ivec2((tide_first + 0) & 1023, (tide_first + 0) >> 10), 0);
    ivec4 tide_q0 = ivec4(tide_int4_lane(tide_r, tide_word & 3), 0, 0, 0);
    tide_result = (tide_q0.x != 0);
    }
    return tide_result;
}

vec2 tf_GlobalIllumination_1OctUV(vec3 t_direction) {
    vec3 t_n = tide_normalize3(t_direction);
    float t_scale = ((abs(t_n.x) + abs(t_n.y)) + abs(t_n.z));
    if ((t_scale <= 0.0)) {
        return vec2(0.5);
    }
    vec3 t_p = (t_n / t_scale);
    vec2 t_folded = ((t_p.z >= 0.0) ? t_p.xy : vec2(((1.0 - abs(t_p.y)) * float(((t_p.x >= 0.0) ? 1 : (-1)))), ((1.0 - abs(t_p.x)) * float(((t_p.y >= 0.0) ? 1 : (-1))))));
    return ((t_folded * 0.5) + vec2(0.5));
}

int tf_GlobalIllumination_1AtlasHeight(int t_rows) {
    return ((t_rows * 19) + 64);
}

vec2 tf_GlobalIllumination_1AtlasUV(int t_probe, int t_rows, vec3 t_direction) {
    vec2 t_tile = (vec2(float((t_probe % tide_i(32))), float((t_probe / tide_i(32)))) * vec2(18.0, 19.0));
    return (((t_tile + vec2(1.0)) + (tf_GlobalIllumination_1OctUV(t_direction) * 16.0)) / vec2(576.0, float(tf_GlobalIllumination_1AtlasHeight(t_rows))));
}

vec3 tf_Render3D_1ClampHdr(vec3 t_radiance) {
    return tide_clamp3(t_radiance, vec3(tide_f(0.0)), vec3(tide_f(65504.0)));
}

void ts_GlobalIllumination_1DebugProbe(tt_Vertex3 t_vertex, inout vec4 t_color) {
    if ((!tide_list0(tide_v2.x))) {
        t_color = vec4(0.2, 0.0, 0.2, 1.0);
        return;
    }
    vec2 t_uv = tf_GlobalIllumination_1AtlasUV(tide_v2.x, tt_GlobalIllumination_1FieldLayout(tide_u0.xyz, tide_u0.w, tide_v0.xyz, tide_v0.w, tide_v1.x, tide_u1.x, (tide_v1.y != 0), tide_u1.y, tide_v1.z, tide_v1.w).m_atlasRows, t_vertex.m_position);
    vec4 t_validity = tide_straight(textureLod(pixels1, t_uv, 0.0));
    if ((t_validity.b >= 0.25)) {
        t_color = vec4(1.0, 0.05, 0.0, 1.0);
        return;
    }
    vec4 t_light = tide_straight(textureLod(pixels2, t_uv, 0.0));
    t_color = vec4(tf_Render3D_1ClampHdr((vec3(t_light.r, t_light.g, t_light.b) / tide_f(3.14159265))), 1.0);
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    ts_GlobalIllumination_1DebugProbe(tt_Vertex3(tide_s0, vec2(0.0), vec4(0.0)), tide_color);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_pixel = tide_color;
}
