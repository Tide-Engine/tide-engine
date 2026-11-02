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
in vec2 tide_s1;
layout(location = 0) out vec4 tide_pixel;
layout(location = 1) out vec4 tide_pixel1;
struct tt_Vertex {
    vec2 m_position;
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
void ts_SparseControlledField(tt_Vertex t_vertex, inout vec4 t_light, inout vec4 t_moments) {
    vec2 t_pixel = (t_vertex.m_uv * vec2(576.0, float((tt_GlobalIllumination_1FieldLayout(tide_u0.xyz, tide_u0.w, tide_v0.xyz, tide_v0.w, tide_v1.x, tide_u1.x, (tide_v1.y != 0), tide_u1.y, tide_v1.z, tide_v1.w).m_atlasRows * 19))));
    int t_index = (int(floor((t_pixel.x / tide_f(18.0)))) + (int(floor((t_pixel.y / tide_f(19.0)))) * 32));
    int t_slot = (t_index / tide_i(27));
    int t_local = (t_index % tide_i(27));
    bool t_outer = ((((t_local % tide_i(3)) != 1) && (((t_local / tide_i(3)) % tide_i(3)) != 1)) && ((t_local / tide_i(9)) != 1));
    bool t_captured = ((tide_v2.x == 11) ? ((t_slot == 2) && t_outer) : ((tide_v2.x != 10) || (t_slot == 2)));
    if (((int(floor(t_pixel.y)) % tide_i(19)) == 18)) {
        t_light = vec4(0.0, 0.0, 0.0, 1.0);
        t_moments = (t_captured ? vec4(vec3((tide_f(128.0) / tide_f(255.0))), 1.0) : vec4(0.0, 0.0, 0.0, 0.0));
        return;
    }
    vec3 t_radiance = ((tide_v2.x == 9) ? vec3(((t_slot == 0) ? 0.2 : ((t_slot == 1) ? 0.8 : 0.4))) : vec3(0.2, 0.1, 0.05));
    if ((tide_v2.x == 15)) {
        vec3 t_coordinate = vec3(float((t_local % tide_i(3))), float(((t_local / tide_i(3)) % tide_i(3))), float((t_local / tide_i(9))));
        vec3 t_anchor = ((t_slot == 0) ? (vec3(float((-8)), 0.0, 0.0) + (t_coordinate * 4.0)) : (t_coordinate * 2.0));
        t_radiance = vec3(((t_anchor.y + t_anchor.z) / tide_f(8.0)));
    }
    if ((tide_v2.x == 17)) {
        t_radiance = (((t_local / tide_i(9)) == 2) ? vec3(0.2, 0.1, 0.05) : vec3(0.0));
    }
    t_light = vec4((t_radiance * 3.14159265), 1.0);
    t_moments = vec4(0.0, 0.0, 0.0, 1.0);
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 tide_color1 = vec4(1.0, 1.0, 1.0, 1.0);
    ts_SparseControlledField(tt_Vertex(vec2(0.0), tide_s1, vec4(0.0)), tide_color, tide_color1);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_color1 = tide_color1 * tide_tint;
    tide_color1.rgb = tide_color1.rgb * tide_color1.a;
    if (tide_screen.w > 0.0) tide_color1.rgb = tide_color1.rgb + (1.0 - tide_color1.a);
    tide_pixel = tide_color;
    tide_pixel1 = tide_color1;
}
