layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    vec4 tide_u0;
    vec4 tide_u1;
    ivec4 tide_v0;
};
uniform sampler2D pixels0;
uniform sampler2D pixels1;
uniform sampler2D pixels2;
uniform sampler2D pixels3;
uniform sampler2D pixels4;
uniform sampler2D pixels5;
uniform sampler2D pixels6;
uniform sampler2D pixels7;
in vec2 tide_s1;
layout(location = 0) out vec4 tide_pixel;
layout(location = 1) out vec4 tide_pixel1;
struct tt_Vertex {
    vec2 m_position;
    vec2 m_uv;
    vec4 m_color;
};
struct tt_GlobalIllumination_1FaceCoordinate {
    int m_face;
    vec2 m_uv;
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
vec2 tide_clamp2(vec2 a, vec2 b, vec2 c) {
    return vec2(tide_clamp1(a.x, b.x, c.x), tide_clamp1(a.y, b.y, c.y));
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
vec2 tf_GlobalIllumination_1WrapOct(vec2 t_uv) {
    vec2 t_result = t_uv;
    if ((t_result.x < 0.0)) {
        t_result = vec2((-t_result.x), (1.0 - t_result.y));
    } else {
        if ((t_result.x > 1.0)) {
            t_result = vec2((2.0 - t_result.x), (1.0 - t_result.y));
        }
    }
    if ((t_result.y < 0.0)) {
        t_result = vec2((1.0 - t_result.x), (-t_result.y));
    } else {
        if ((t_result.y > 1.0)) {
            t_result = vec2((1.0 - t_result.x), (2.0 - t_result.y));
        }
    }
    return t_result;
}

vec3 tf_GlobalIllumination_1OctVector(vec2 t_uv) {
    vec2 t_p = ((tf_GlobalIllumination_1WrapOct(t_uv) * 2.0) - vec2(1.0));
    vec3 t_direction = vec3(t_p, ((1.0 - abs(t_p.x)) - abs(t_p.y)));
    if ((t_direction.z < 0.0)) {
        t_direction = vec3(((1.0 - abs(t_p.y)) * float(((t_p.x >= 0.0) ? 1 : (-1)))), ((1.0 - abs(t_p.x)) * float(((t_p.y >= 0.0) ? 1 : (-1)))), t_direction.z);
    }
    return t_direction;
}

vec3 tf_GlobalIllumination_1OctDirection(vec2 t_uv) {
    return tide_normalize3(tf_GlobalIllumination_1OctVector(t_uv));
}

tt_GlobalIllumination_1FaceCoordinate tf_GlobalIllumination_1FaceUV(vec3 t_direction) {
    vec3 t_magnitude = abs(t_direction);
    int t_face = 0;
    vec2 t_plane = vec2(0.0);
    if (((t_magnitude.x >= t_magnitude.y) && (t_magnitude.x >= t_magnitude.z))) {
        float t_inverse = (1.0 / tide_max1(t_magnitude.x, tide_f(1e-05)));
        t_face = ((t_direction.x >= 0.0) ? 0 : 1);
        t_plane = (vec2(((t_direction.x >= 0.0) ? (-t_direction.z) : t_direction.z), (-t_direction.y)) * t_inverse);
    } else {
        if ((t_magnitude.y >= t_magnitude.z)) {
            float t_inverse = (1.0 / tide_max1(t_magnitude.y, tide_f(1e-05)));
            t_face = ((t_direction.y >= 0.0) ? 2 : 3);
            t_plane = (vec2(t_direction.x, ((t_direction.y >= 0.0) ? t_direction.z : (-t_direction.z))) * t_inverse);
        } else {
            float t_inverse = (1.0 / tide_max1(t_magnitude.z, tide_f(1e-05)));
            t_face = ((t_direction.z >= 0.0) ? 4 : 5);
            t_plane = (vec2(((t_direction.z >= 0.0) ? t_direction.x : (-t_direction.x)), (-t_direction.y)) * t_inverse);
        }
    }
    return tt_GlobalIllumination_1FaceCoordinate(t_face, ((t_plane * 0.5) + vec2(0.5)));
}

vec4 tf_GlobalIllumination_1CaptureData_42x3x4x5x6x7(int t_face, vec2 t_uv) {
    vec4 t_data = vec4(0.0, 0.0, 0.0, 1.0);
    if ((t_face == 0)) {
        t_data = tide_straight(textureLod(pixels2, t_uv, 0.0));
    } else {
        if ((t_face == 1)) {
            t_data = tide_straight(textureLod(pixels3, t_uv, 0.0));
        } else {
            if ((t_face == 2)) {
                t_data = tide_straight(textureLod(pixels4, t_uv, 0.0));
            } else {
                if ((t_face == 3)) {
                    t_data = tide_straight(textureLod(pixels5, t_uv, 0.0));
                } else {
                    if ((t_face == 4)) {
                        t_data = tide_straight(textureLod(pixels6, t_uv, 0.0));
                    } else {
                        t_data = tide_straight(textureLod(pixels7, t_uv, 0.0));
                    }
                }
            }
        }
    }
    return t_data;
}

float tf_GlobalIllumination_1CaptureInverseDepth_42x3x4x5x6x7(int t_face, vec2 t_pixel, float t_spacing, float t_far) {
    vec2 t_uv = ((t_pixel + vec2(0.5)) / tide_f(32.0));
    vec4 t_data = tf_GlobalIllumination_1CaptureData_42x3x4x5x6x7(t_face, t_uv);
    vec2 t_plane = ((t_uv * 2.0) - vec2(1.0));
    float t_radial = tide_max1((t_data.r * t_far), (t_spacing * tide_f(0.001)));
    return (((t_data.b > 0.5) && (t_data.r < 1.0)) ? ((t_spacing * sqrt((tide_f(1.0) + dot(t_plane, t_plane)))) / t_radial) : 0.0);
}

vec3 tf_Render3D_1ClampHdr(vec3 t_radiance) {
    return tide_clamp3(t_radiance, vec3(tide_f(0.0)), vec3(tide_f(65504.0)));
}

void ts_GlobalIllumination_1Convolve(tt_Vertex t_vertex, inout vec4 t_irradiance, inout vec4 t_moments) {
    vec2 t_pixel = (t_vertex.m_uv * vec2(18.0, 19.0));
    if ((t_pixel.y >= 18.0)) {
        t_irradiance = vec4(0.0, 0.0, 0.0, 1.0);
        t_moments = vec4((vec3(tide_u0.z, tide_u0.w, tide_u1.x) + vec3((tide_f(128.0) / tide_f(255.0)))), 1.0);
        return;
    }
    vec3 t_direction = tf_GlobalIllumination_1OctDirection(((t_pixel - vec2(1.0)) / tide_f(16.0)));
    vec3 t_light = vec3(0.0);
    float t_cosineWeight = 0.0;
    float t_sphereWeight = 0.0;
    float t_backfaces = 0.0;
    int t_rayCount = max(tide_i(1), min(tide_i(384), tide_v0.x));
    for (int t_sample = 0; (t_sample < t_rayCount); t_sample = (t_sample + 1)) {
        vec2 t_uv = ((vec2(float((t_sample % tide_i(64))), float((t_sample / tide_i(64)))) + vec2(0.5)) / vec2(tide_f(64.0), tide_f(6.0)));
        vec4 t_radiance = tide_straight(textureLod(pixels0, t_uv, 0.0));
        vec4 t_data = tide_straight(textureLod(pixels1, t_uv, 0.0));
        vec3 t_weightedRay = (((vec3(t_data.r, t_data.g, t_data.b) * 2.0) - vec3(1.0)) * t_data.a);
        float t_weight = tide_max1(dot(t_direction, t_weightedRay), tide_f(0.0));
        t_light = (t_light + (vec3(t_radiance.r, t_radiance.g, t_radiance.b) * (t_weight * t_radiance.a)));
        t_cosineWeight = (t_cosineWeight + t_weight);
        t_sphereWeight = (t_sphereWeight + t_data.a);
        t_backfaces = (t_backfaces + ((1.0 - t_radiance.a) * t_data.a));
    }
    t_light = (t_light * (3.14159265 / tide_max1(t_cosineWeight, tide_f(1e-06))));
    float t_backfaceFraction = (t_backfaces / tide_max1(t_sphereWeight, tide_f(1e-06)));
    tt_GlobalIllumination_1FaceCoordinate t_tap = tf_GlobalIllumination_1FaceUV(t_direction);
    vec2 t_capturePixel = ((t_tap.m_uv * 32.0) - vec2(0.5));
    vec2 t_lower = tide_clamp2(floor(t_capturePixel), vec2(tide_f(0.0)), vec2(tide_f(30.0)));
    vec2 t_fraction = (t_capturePixel - t_lower);
    float t_a = tf_GlobalIllumination_1CaptureInverseDepth_42x3x4x5x6x7(t_tap.m_face, t_lower, tide_u0.x, tide_u0.y);
    float t_b = tf_GlobalIllumination_1CaptureInverseDepth_42x3x4x5x6x7(t_tap.m_face, (t_lower + vec2(1.0, 0.0)), tide_u0.x, tide_u0.y);
    float t_c = tf_GlobalIllumination_1CaptureInverseDepth_42x3x4x5x6x7(t_tap.m_face, (t_lower + vec2(0.0, 1.0)), tide_u0.x, tide_u0.y);
    float t_d = tf_GlobalIllumination_1CaptureInverseDepth_42x3x4x5x6x7(t_tap.m_face, (t_lower + vec2(1.0, 1.0)), tide_u0.x, tide_u0.y);
    float t_axial = tide_max1(mix(mix(t_a, t_b, t_fraction.x), mix(t_c, t_d, t_fraction.x), t_fraction.y), tide_f(0.0));
    vec3 t_absolute = abs(t_direction);
    float t_inverse = ((t_axial * tide_max1(tide_max1(t_absolute.x, t_absolute.y), t_absolute.z)) / ((t_absolute.x + t_absolute.y) + t_absolute.z));
    t_irradiance = vec4(tf_Render3D_1ClampHdr(t_light), 1.0);
    t_moments = vec4(t_inverse, 0.0, tide_clamp1(t_backfaceFraction, tide_f(0.0), tide_f(1.0)), 1.0);
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 tide_color1 = vec4(1.0, 1.0, 1.0, 1.0);
    ts_GlobalIllumination_1Convolve(tt_Vertex(vec2(0.0), tide_s1, vec4(0.0)), tide_color, tide_color1);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_color1 = tide_color1 * tide_tint;
    tide_color1.rgb = tide_color1.rgb * tide_color1.a;
    if (tide_screen.w > 0.0) tide_color1.rgb = tide_color1.rgb + (1.0 - tide_color1.a);
    tide_pixel = tide_color;
    tide_pixel1 = tide_color1;
}
