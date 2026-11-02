layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    ivec4 tide_v0;
};
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
void ts_GIIncoming(tt_Vertex t_vertex, inout vec4 t_light, inout vec4 t_data) {
    bool t_dark = (((tide_v0.x == 3) && (tide_v0.y != 0)) || ((tide_v0.x == 7) && (((tide_v0.y != 0) || (int(floor((t_vertex.m_uv.x * tide_f(32.0)))) != 17)) || (int(floor((t_vertex.m_uv.y * tide_f(32.0)))) != 17))));
    t_light = (t_dark ? vec4(0.0, 0.0, 0.0, 1.0) : ((tide_v0.x == 7) ? vec4(20.0, 10.0, 5.0, 1.0) : vec4(0.2, 0.1, 0.05, 1.0)));
    t_data = ((tide_v0.x == 12) ? vec4(1.0, 1.0, 1.0, 1.0) : ((tide_v0.x == 2) ? vec4(0.001, 0.0, 1.0, 1.0) : ((tide_v0.x == 5) ? vec4(0.1, 1.0, 1.0, 1.0) : vec4(0.0, 0.0, 0.0, 1.0))));
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 tide_color1 = vec4(1.0, 1.0, 1.0, 1.0);
    ts_GIIncoming(tt_Vertex(vec2(0.0), tide_s1, vec4(0.0)), tide_color, tide_color1);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_color1 = tide_color1 * tide_tint;
    tide_color1.rgb = tide_color1.rgb * tide_color1.a;
    if (tide_screen.w > 0.0) tide_color1.rgb = tide_color1.rgb + (1.0 - tide_color1.a);
    tide_pixel = tide_color;
    tide_pixel1 = tide_color1;
}
