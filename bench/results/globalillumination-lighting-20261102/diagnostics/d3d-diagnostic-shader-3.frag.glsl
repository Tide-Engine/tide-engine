layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    vec4 tide_u0;
    vec4 tide_u1;
    ivec4 tide_v0;
    ivec4 tide_v1;
};
uniform sampler2D pixels0;
uniform sampler2D pixels1;
in vec3 tide_s1;
in vec2 tide_s2;
in vec4 tide_s3;
in vec4 tide_s4;
in vec3 tide_world;
flat in vec3 tide_normals0;
flat in vec3 tide_normals1;
flat in vec3 tide_normals2;
flat in vec4 tide_f0;
flat in vec4 tide_f1;
flat in vec4 tide_f2;
flat in ivec4 tide_g0;
out vec4 tide_pixel;
struct tt_Render3D_1MeshVertex {
    vec3 m_position;
    vec3 m_normal;
    vec2 m_uv;
    vec4 m_color;
    vec4 m_tangent;
};
struct tt_Render3D_1SurfaceInput {
    vec3 m_world;
    vec3 m_normal;
    vec3 m_tangent;
    vec3 m_bitangent;
    vec2 m_uv;
    vec3 m_viewDirection;
};
struct tt_Render3D_1MaterialMaps {
    bool m_normal;
    bool m_mask;
    bool m_emission;
    bool m_height;
    float m_handedness;
};
struct tt_TideGenerated_1Callable0860b6c3 {
    int m_kind;
};
struct tt_Render3D_1Coverage {
    float m_alpha;
    float m_cutoff;
};
struct tt_TideGenerated_1Callable741b56c4 {
    int m_kind;
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
vec3 tide_normalize3(vec3 v) {
    float l = dot(v, v);
    if (l > 1.17549435e-38) { return v * inversesqrt(l); }
    return vec3(0.0);
}
vec4 tide_straight(vec4 c) {
    if (c.a == 0.0) { return vec4(0.0); }
    return vec4(c.rgb / c.a, c.a);
}
vec3 tf_Render3D_1FallbackTangent(vec3 t_normal) {
    vec3 t_n = tide_normalize3(t_normal);
    if ((dot(t_n, t_n) < 0.5)) {
        return vec3(1.0, 0.0, 0.0);
    }
    vec3 t_absolute = abs(t_n);
    vec3 t_axis = (((t_absolute.x <= t_absolute.y) && (t_absolute.x <= t_absolute.z)) ? vec3(1.0, 0.0, 0.0) : ((t_absolute.y <= t_absolute.z) ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0)));
    return tide_normalize3(cross(t_axis, t_n));
}

tt_Render3D_1SurfaceInput tf_Render3D_1MaterialInput(vec3 t_world, vec3 t_normal, vec4 t_tangent, vec3 t_planeNormal, vec3 t_planeSecond, float t_handedness, vec2 t_uv, vec3 t_viewDirection, bool t_doubleSided) {
    vec3 t_t = (cross(t_planeNormal, t_planeSecond) * t_handedness);
    t_t = tide_normalize3((t_t - (t_normal * dot(t_normal, t_t))));
    if ((dot(t_t, t_t) < 0.5)) {
        t_t = tf_Render3D_1FallbackTangent(t_normal);
    }
    vec3 t_n = ((dot(t_normal, t_normal) >= 0.5) ? t_normal : vec3(0.0, 1.0, 0.0));
    float t_sign = (float(((t_tangent.w < 0.0) ? (-1) : 1)) * t_handedness);
    vec3 t_b = (tide_normalize3(cross(t_n, t_t)) * t_sign);
    bool t_back = (t_doubleSided && (dot(t_normal, t_viewDirection) < 0.0));
    return tt_Render3D_1SurfaceInput(t_world, (t_back ? (-t_normal) : t_normal), t_t, (t_back ? (-t_b) : t_b), t_uv, t_viewDirection);
}

vec3 tf_Render3D_1TangentPlaneNormal(vec3 t_normal, vec4 t_tangent) {
    vec3 t_t = tide_normalize3(t_tangent.xyz);
    if ((dot(t_t, t_t) < 0.5)) {
        t_t = tf_Render3D_1FallbackTangent(t_normal);
    }
    vec3 t_b = tide_normalize3(cross(t_normal, t_t));
    return ((dot(t_b, t_b) >= 0.5) ? t_b : tf_Render3D_1FallbackTangent(t_t));
}

vec3 tf_Render3D_1TangentPlaneSecond(vec3 t_normal, vec4 t_tangent) {
    vec3 t_t = tide_normalize3(t_tangent.xyz);
    if ((dot(t_t, t_t) < 0.5)) {
        t_t = tf_Render3D_1FallbackTangent(t_normal);
    }
    return cross(t_t, tf_Render3D_1TangentPlaneNormal(t_normal, t_tangent));
}

void tf_Render3D_1StandardUV(tt_Render3D_1SurfaceInput t_input, inout vec2 t_uv) {
}

void tf_TideGenerated_1Invoke0860b6c3(tt_TideGenerated_1Callable0860b6c3 t_binding, tt_Render3D_1SurfaceInput t_arg0, inout vec2 t_arg1) {
    switch (t_binding.m_kind) {
        case 81879654: {
            tf_Render3D_1StandardUV(t_arg0, t_arg1);
            break;
        }
        default: {
        } break;
    }
    return;
}

vec2 tf_Render3D_1MaterialUV(tt_Render3D_1SurfaceInput t_input, vec2 t_scale, vec2 t_offset, tt_TideGenerated_1Callable0860b6c3 t_hook) {
    tt_Render3D_1SurfaceInput t_scaled = t_input;
    t_scaled.m_uv = ((t_input.m_uv * t_scale) + t_offset);
    vec2 t_uv = t_scaled.m_uv;
    tf_TideGenerated_1Invoke0860b6c3(t_hook, t_scaled, t_uv);
    return t_uv;
}

vec2 tf_Render3D_1ParallaxUV(tt_Render3D_1SurfaceInput t_input, vec2 t_uv, float t_height, float t_scale) {
    vec2 t_along = vec2(dot(t_input.m_viewDirection, t_input.m_tangent), dot(t_input.m_viewDirection, t_input.m_bitangent));
    float t_facing = tide_max1(dot(t_input.m_viewDirection, t_input.m_normal), tide_f(0.05));
    return (t_uv + (t_along * (((tide_clamp1(t_height, tide_f(0.0), tide_f(1.0)) - 0.5) * t_scale) / t_facing)));
}

vec2 tf_Render3D_1MaterialParallax_40(tt_Render3D_1SurfaceInput t_input, vec2 t_uv, bool t_enabled, float t_scale) {
    if (((!t_enabled) || (t_scale == 0.0))) {
        return t_uv;
    }
    vec4 t_height = textureLod(pixels0, t_uv, 0.0);
    return tf_Render3D_1ParallaxUV(t_input, t_uv, t_height.r, t_scale);
}

tt_Render3D_1SurfaceInput tf_Render3D_1MaterialUVInput(tt_Render3D_1SurfaceInput t_input, vec2 t_uv) {
    tt_Render3D_1SurfaceInput t_result = t_input;
    t_result.m_uv = t_uv;
    return t_result;
}

void tf_Render3D_1StandardCoverage(tt_Render3D_1SurfaceInput t_input, inout tt_Render3D_1Coverage t_coverage) {
}

void tf_TideGenerated_1Invoke741b56c4(tt_TideGenerated_1Callable741b56c4 t_binding, tt_Render3D_1SurfaceInput t_arg0, inout tt_Render3D_1Coverage t_arg1) {
    switch (t_binding.m_kind) {
        case 1327248862: {
            tf_Render3D_1StandardCoverage(t_arg0, t_arg1);
            break;
        }
        default: {
        } break;
    }
    return;
}

tt_Render3D_1Coverage tf_Render3D_1MaterialCoverage(tt_Render3D_1SurfaceInput t_input, float t_alpha, float t_cutoff, tt_TideGenerated_1Callable741b56c4 t_hook) {
    tt_Render3D_1Coverage t_coverage = tt_Render3D_1Coverage(t_alpha, t_cutoff);
    tf_TideGenerated_1Invoke741b56c4(t_hook, t_input, t_coverage);
    t_coverage.m_alpha = tide_clamp1(t_coverage.m_alpha, tide_f(0.0), tide_f(1.0));
    t_coverage.m_cutoff = tide_clamp1(t_coverage.m_cutoff, tide_f(0.0), tide_f(1.0));
    return t_coverage;
}

void ts_Render3D_1ShadowCutout(tt_Render3D_1MeshVertex t_vertex, inout vec4 t_color) {
    vec3 t_normal = tide_normalize3(mat3(tide_normals0, tide_normals1, tide_normals2) * (t_vertex.m_normal));
    vec3 t_viewDirection = tide_normalize3((tide_u0.xyz - tide_world));
    tt_Render3D_1SurfaceInput t_frame = tf_Render3D_1MaterialInput(tide_world, t_normal, t_vertex.m_tangent, tide_normalize3(mat3(tide_normals0, tide_normals1, tide_normals2) * (tf_Render3D_1TangentPlaneNormal(t_vertex.m_normal, t_vertex.m_tangent))), tide_normalize3(mat3(tide_normals0, tide_normals1, tide_normals2) * (tf_Render3D_1TangentPlaneSecond(t_vertex.m_normal, t_vertex.m_tangent))), tt_Render3D_1MaterialMaps((tide_v0.y != 0), (tide_v0.z != 0), (tide_v0.w != 0), (tide_v1.x != 0), tide_u1.w).m_handedness, t_vertex.m_uv, t_viewDirection, (tide_g0.x != 0));
    vec2 t_hookedUV = tf_Render3D_1MaterialUV(t_frame, tide_f1.zw, tide_f2.xy, tt_TideGenerated_1Callable0860b6c3(tide_g0.y));
    vec2 t_uv = tf_Render3D_1MaterialParallax_40(t_frame, t_hookedUV, tt_Render3D_1MaterialMaps((tide_v0.y != 0), (tide_v0.z != 0), (tide_v0.w != 0), (tide_v1.x != 0), tide_u1.w).m_height, tide_f1.y);
    tt_Render3D_1SurfaceInput t_input = tf_Render3D_1MaterialUVInput(t_frame, t_uv);
    vec4 t_sampled = tide_straight(textureLod(pixels1, t_uv, 0.0));
    tt_Render3D_1Coverage t_coverage = tf_Render3D_1MaterialCoverage(t_input, ((t_sampled.a * tide_f0.a) * t_vertex.m_color.a), tide_f1.x, tt_TideGenerated_1Callable741b56c4(tide_g0.z));
    if ((t_coverage.m_alpha < t_coverage.m_cutoff)) {
        discard;
    }
    t_color = vec4(1.0, 1.0, 1.0, 1.0);
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    ts_Render3D_1ShadowCutout(tt_Render3D_1MeshVertex(vec3(0.0), tide_s1, tide_s2, tide_s3, tide_s4), tide_color);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_pixel = tide_color;
}
