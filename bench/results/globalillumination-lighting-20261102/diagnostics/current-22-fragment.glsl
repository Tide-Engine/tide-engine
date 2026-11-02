layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    vec4 tide_u0;
    ivec4 tide_v0;
};
uniform sampler2D pixels0;
uniform sampler2D pixels1;
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
vec4 tide_straight(vec4 c) {
    if (c.a == 0.0) { return vec4(0.0); }
    return vec4(c.rgb / c.a, c.a);
}
void ts_ReadGIRawCapture(tt_Vertex t_vertex, inout vec4 t_color) {
    int t_at = max(tide_i(0), min(tide_i(3), int(tide_u0.xyz.x)));
    vec2 t_pixel = vec2(float((10 + (t_at % tide_i(2)))), float((20 + (t_at / tide_i(2)))));
    if ((tide_v0.x == 22)) {
        t_color = tide_straight(textureLod(pixels0, ((t_pixel + vec2(0.5)) / tide_f(32.0)), 0.0));
        return;
    }
    vec4 t_data = tide_straight(textureLod(pixels1, ((t_pixel + vec2(0.5)) / tide_f(32.0)), 0.0));
    int t_bits = floatBitsToInt(t_data.r);
    if ((tide_v0.x == 20)) {
        t_color = vec4((vec3(float((t_bits & 255)), float(((t_bits >> tide_i(8)) & 255)), float(((t_bits >> tide_i(16)) & 255))) / tide_f(255.0)), 1.0);
    } else {
        t_color = vec4((float(((t_bits >> tide_i(24)) & 255)) / tide_f(255.0)), t_data.g, t_data.b, 1.0);
    }
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    ts_ReadGIRawCapture(tt_Vertex(vec2(0.0), vec2(0.0), vec4(0.0)), tide_color);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_pixel = tide_color;
}
