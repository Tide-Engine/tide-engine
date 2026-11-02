layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    ivec4 tide_v0;
};
uniform highp isampler2D pixels0;
in vec2 tide_s1;
layout(location = 0) out vec4 tide_pixel;
layout(location = 1) out vec4 tide_pixel1;
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
int tide_int4_lane(ivec4 value, int index) {
    return index == 0 ? value.x : index == 1 ? value.y : index == 2 ? value.z : value.w;
}
int tide_list0(int tide_i) {
    int tide_result = 0;
    if (!(tide_i < 0 || tide_i >= tide_v0.x)) {
    int tide_word = tide_i * 1;
    int tide_first = tide_word >> 2;
    ivec4 tide_r = texelFetch(pixels0, ivec2((tide_first + 0) & 1023, (tide_first + 0) >> 10), 0);
    ivec4 tide_q0 = ivec4(tide_int4_lane(tide_r, tide_word & 3), 0, 0, 0);
    tide_result = tide_q0.x;
    }
    return tide_result;
}

void ts_GlobalIllumination_1WriteLookup(tt_Vertex t_vertex, inout vec4 t_light, inout vec4 t_data) {
    int t_address = (int(floor((t_vertex.m_uv.x * tide_f(576.0)))) + (int(floor((t_vertex.m_uv.y * tide_f(64.0)))) * 576));
    int t_code = ((t_address < tide_v0.x) ? tide_list0(t_address) : 0);
    t_light = vec4(0.0, 0.0, 0.0, 0.0);
    t_data = vec4((float((t_code & 255)) / tide_f(255.0)), (float(((t_code >> tide_i(8)) & 255)) / tide_f(255.0)), (float(((t_code >> tide_i(16)) & 255)) / tide_f(255.0)), 1.0);
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 tide_color1 = vec4(1.0, 1.0, 1.0, 1.0);
    ts_GlobalIllumination_1WriteLookup(tt_Vertex(vec2(0.0), tide_s1, vec4(0.0)), tide_color, tide_color1);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_color1 = tide_color1 * tide_tint;
    tide_color1.rgb = tide_color1.rgb * tide_color1.a;
    if (tide_screen.w > 0.0) tide_color1.rgb = tide_color1.rgb + (1.0 - tide_color1.a);
    tide_pixel = tide_color;
    tide_pixel1 = tide_color1;
}
