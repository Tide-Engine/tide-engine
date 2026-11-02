layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    vec4 tide_u0;
    ivec4 tide_v0;
};
uniform sampler2D pixels0;
uniform sampler2D pixels1;
layout(location = 0) in vec2 tide_a0;
layout(location = 1) in vec2 tide_a1;
layout(location = 2) in vec4 tide_a2;
struct tt_Vertex {
    vec2 m_position;
    vec2 m_uv;
    vec4 m_color;
};
void main() {
    tt_Vertex tide_vertex = tt_Vertex(tide_a0, tide_a1, tide_a2);
    vec4 tide_place = vec4(tide_vertex.m_position, 0.0, 1.0);
    gl_Position = tide_camera * tide_place;
}
