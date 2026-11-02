layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
};
uniform sampler2D pixels0;
uniform sampler2D pixels1;
uniform sampler2D pixels2;
uniform sampler2D pixels3;
uniform sampler2D pixels4;
uniform sampler2D pixels5;
uniform sampler2D pixels6;
uniform sampler2D pixels7;
uniform sampler2D pixels8;
uniform sampler2D pixels9;
uniform sampler2D pixels10;
uniform sampler2D pixels11;
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
vec4 tide_straight(vec4 c) {
    if (c.a == 0.0) { return vec4(0.0); }
    return vec4(c.rgb / c.a, c.a);
}
vec3 tf_GlobalIllumination_1FaceDirection(int t_face, vec2 t_uv) {
    vec2 t_p = ((t_uv * 2.0) - vec2(1.0));
    if ((t_face == 0)) {
        return tide_normalize3(vec3(tide_f(1.0), (-t_p.y), (-t_p.x)));
    }
    if ((t_face == 1)) {
        return tide_normalize3(vec3(float((-tide_i(1))), (-t_p.y), t_p.x));
    }
    if ((t_face == 2)) {
        return tide_normalize3(vec3(t_p.x, tide_f(1.0), t_p.y));
    }
    if ((t_face == 3)) {
        return tide_normalize3(vec3(t_p.x, float((-tide_i(1))), (-t_p.y)));
    }
    if ((t_face == 4)) {
        return tide_normalize3(vec3(t_p.x, (-t_p.y), tide_f(1.0)));
    }
    return tide_normalize3(vec3((-t_p.x), (-t_p.y), float((-tide_i(1)))));
}

vec3 tf_Render3D_1ClampHdr(vec3 t_radiance) {
    return tide_clamp3(t_radiance, vec3(tide_f(0.0)), vec3(tide_f(65504.0)));
}

vec3 tf_GlobalIllumination_1CaptureLight_40x1x2x3x4x5(int t_face, vec2 t_uv) {
    vec4 t_light = vec4(0.0, 0.0, 0.0, 1.0);
    if ((t_face == 0)) {
        t_light = tide_straight(textureLod(pixels0, t_uv, 0.0));
    } else {
        if ((t_face == 1)) {
            t_light = tide_straight(textureLod(pixels1, t_uv, 0.0));
        } else {
            if ((t_face == 2)) {
                t_light = tide_straight(textureLod(pixels2, t_uv, 0.0));
            } else {
                if ((t_face == 3)) {
                    t_light = tide_straight(textureLod(pixels3, t_uv, 0.0));
                } else {
                    if ((t_face == 4)) {
                        t_light = tide_straight(textureLod(pixels4, t_uv, 0.0));
                    } else {
                        t_light = tide_straight(textureLod(pixels5, t_uv, 0.0));
                    }
                }
            }
        }
    }
    return tf_Render3D_1ClampHdr(vec3(t_light.r, t_light.g, t_light.b));
}

vec4 tf_GlobalIllumination_1CaptureData_46x7x8x9x10x11(int t_face, vec2 t_uv) {
    vec4 t_data = vec4(0.0, 0.0, 0.0, 1.0);
    if ((t_face == 0)) {
        t_data = tide_straight(textureLod(pixels6, t_uv, 0.0));
    } else {
        if ((t_face == 1)) {
            t_data = tide_straight(textureLod(pixels7, t_uv, 0.0));
        } else {
            if ((t_face == 2)) {
                t_data = tide_straight(textureLod(pixels8, t_uv, 0.0));
            } else {
                if ((t_face == 3)) {
                    t_data = tide_straight(textureLod(pixels9, t_uv, 0.0));
                } else {
                    if ((t_face == 4)) {
                        t_data = tide_straight(textureLod(pixels10, t_uv, 0.0));
                    } else {
                        t_data = tide_straight(textureLod(pixels11, t_uv, 0.0));
                    }
                }
            }
        }
    }
    return t_data;
}

void ts_GlobalIllumination_1PackRays(tt_Vertex t_vertex, inout vec4 t_light, inout vec4 t_data) {
    int t_face = max(tide_i(0), min(tide_i(5), int(floor((t_vertex.m_uv.y * tide_f(6.0))))));
    int t_sample = max(tide_i(0), min(tide_i(63), int(floor((t_vertex.m_uv.x * tide_f(64.0))))));
    int t_y = (t_sample / tide_i(8));
    int t_x = (t_sample % tide_i(8));
    int t_px = ((t_x < 4) ? (2 + (t_x * 4)) : (17 + ((t_x - 4) * 4)));
    int t_py = ((t_y < 4) ? (2 + (t_y * 4)) : (17 + ((t_y - 4) * 4)));
    vec2 t_uv = ((vec2(float(t_px), float(t_py)) + vec2(0.5)) / tide_f(32.0));
    vec2 t_plane = ((t_uv * 2.0) - vec2(1.0));
    float t_lengthSq = (1.0 + dot(t_plane, t_plane));
    float t_solidAngle = (1.0 / (t_lengthSq * sqrt(t_lengthSq)));
    vec3 t_ray = tf_GlobalIllumination_1FaceDirection(t_face, t_uv);
    vec3 t_radiance = tf_GlobalIllumination_1CaptureLight_40x1x2x3x4x5(t_face, t_uv);
    vec4 t_hit = tf_GlobalIllumination_1CaptureData_46x7x8x9x10x11(t_face, t_uv);
    bool t_beyond = ((t_hit.b > 0.5) && (t_hit.r >= 1.0));
    float t_back = (((t_hit.b > 0.5) && (!t_beyond)) ? tide_clamp1(t_hit.g, tide_f(0.0), tide_f(1.0)) : 0.0);
    t_light = vec4((t_beyond ? vec3(0.0) : t_radiance), (1.0 - t_back));
    t_data = vec4(((t_ray * 0.5) + vec3(0.5)), t_solidAngle);
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 tide_color1 = vec4(1.0, 1.0, 1.0, 1.0);
    ts_GlobalIllumination_1PackRays(tt_Vertex(vec2(0.0), tide_s1, vec4(0.0)), tide_color, tide_color1);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_color1 = tide_color1 * tide_tint;
    tide_color1.rgb = tide_color1.rgb * tide_color1.a;
    if (tide_screen.w > 0.0) tide_color1.rgb = tide_color1.rgb + (1.0 - tide_color1.a);
    tide_pixel = tide_color;
    tide_pixel1 = tide_color1;
}
