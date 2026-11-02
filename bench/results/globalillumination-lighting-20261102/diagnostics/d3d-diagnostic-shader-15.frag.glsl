layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    vec4 tide_u0;
};
in vec4 tide_s3;
out vec4 tide_pixel;
void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    tide_color = tide_s3;
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_pixel = tide_color;
}
