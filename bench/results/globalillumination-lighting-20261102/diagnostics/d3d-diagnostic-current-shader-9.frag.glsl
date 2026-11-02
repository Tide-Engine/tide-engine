layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    vec4 tide_u0;
    vec4 tide_u1;
    vec4 tide_u2;
    vec4 tide_u3;
    vec4 tide_u4;
    vec4 tide_u5;
    vec4 tide_u6;
    ivec4 tide_v0;
    ivec4 tide_v1;
    ivec4 tide_v2;
    ivec4 tide_v3;
    ivec4 tide_v4;
    ivec4 tide_v5;
};
uniform highp isampler2D pixels0;
uniform highp isampler2D pixels1;
uniform sampler2D pixels2;
uniform sampler2D pixels3;
uniform sampler2D pixels4;
uniform sampler2D pixels5;
uniform sampler2D pixels6;
uniform sampler2D pixels7;
uniform sampler2D pixels8;
uniform highp sampler2DShadow pixels9;
uniform highp isampler2D pixels10;
uniform highp isampler2D pixels11;
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
flat in vec4 tide_f3;
flat in vec4 tide_f4;
flat in ivec4 tide_g0;
flat in ivec4 tide_g1;
layout(location = 0) out vec4 tide_pixel;
layout(location = 1) out vec4 tide_pixel1;
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
struct tt_Render3D_1Surface {
    vec3 m_albedo;
    vec3 m_normal;
    float m_roughness;
    float m_metallic;
    vec3 m_emission;
    float m_occlusion;
};
struct tt_TideGenerated_1Callable30dff975 {
    int m_kind;
    vec3 m_capture55c3790a_0emission;
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
struct tt_TideGenerated_1Callable3bedd4bb {
    int m_kind;
    tt_GlobalIllumination_1FieldLayout m_capture74159d47_0layout;
    float m_capture74159d47_0intensity;
    int m_capture74159d47_0initialized;
};
struct tt_GlobalIllumination_1SamplingFrame {
    bool m_valid;
    vec3 m_normal;
    vec3 m_visibilityNormal;
    vec3 m_position;
};
struct tt_GlobalIllumination_1SampleBrick {
    int m_slot;
    vec3 m_minimum;
    float m_size;
    int m_transitions;
};
struct tt_GlobalIllumination_1IrradianceBlend {
    vec3 m_light;
    float m_weight;
    float m_base;
    float m_available;
};
struct tt_Render3D_1LightData {
    int m_kind;
    vec3 m_position;
    vec3 m_direction;
    vec3 m_intensity;
    float m_range;
    float m_innerCosine;
    float m_outerCosine;
    float m_outerAngle;
    bool m_castShadows;
    int m_priority;
    int m_shadowIndex;
    int m_cascadeCount;
};
struct tt_Render3D_1ShadowView {
    mat4 m_camera;
    mat4 m_projection;
    mat4 m_worldToClip;
    vec4 m_uvRect;
    float m_cascadeEnd;
    float m_cascadeBlend;
    float m_biasScale;
};
struct tt_Render3D_1ShadowKernel {
    vec2 m_offset;
    vec2 m_weight;
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
vec3 tide_min3(vec3 a, vec3 b) {
    return vec3(tide_min1(a.x, b.x), tide_min1(a.y, b.y), tide_min1(a.z, b.z));
}
float tide_max1(float a, float b) {
    if (tide_nan(b)) { return a; }
    if (tide_nan(a)) { return b; }
    if (a > b) { return a; }
    return b;
}
vec3 tide_max3(vec3 a, vec3 b) {
    return vec3(tide_max1(a.x, b.x), tide_max1(a.y, b.y), tide_max1(a.z, b.z));
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
float tide_saturate1(float a) {
    if (tide_nan(a)) { return 0.0; }
    return clamp(a, 0.0, 1.0);
}
float tide_smoothstep1(float a, float b, float c) {
    float numerator = c - a;
    float denominator = b - a;
    float divisor = denominator;
    if (divisor == 0.0) { divisor = 1.0; }
    float t = tide_saturate1(numerator / divisor);
    if (denominator == 0.0) {
        t = 0.0;
        if (floatBitsToInt(denominator) < 0) {
            if (numerator < 0.0) { t = 1.0; }
        } else if (numerator > 0.0) { t = 1.0; }
    }
    return t * t * (3.0 - 2.0 * t);
}
float tide_pow1(float a, float b) {
    if (b == 0.0) { return 1.0; }
    bool w = a < 0.0 && floor(b) == b;
    float x = a;
    if (w) { x = -a; }
    float p = pow(x, b);
    if (w && floor(b * 0.5) * 2.0 != b) { return -p; }
    return p;
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

vec2 tf_Render3D_1MaterialParallax_42(tt_Render3D_1SurfaceInput t_input, vec2 t_uv, bool t_enabled, float t_scale) {
    if (((!t_enabled) || (t_scale == 0.0))) {
        return t_uv;
    }
    vec4 t_height = textureLod(pixels2, t_uv, 0.0);
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

float tf_Render3D_1SrgbToLinear_3_0float(float t_value) {
    float t_channel = tide_max1(t_value, tide_f(0.0));
    if ((t_channel <= 0.04045)) {
        return (t_channel / tide_f(12.92));
    }
    return tide_pow1(((t_channel + tide_f(0.055)) / tide_f(1.055)), tide_f(2.4));
}

vec3 tf_Render3D_1SrgbToLinear_3_0float3(vec3 t_value) {
    return vec3(tf_Render3D_1SrgbToLinear_3_0float(t_value.x), tf_Render3D_1SrgbToLinear_3_0float(t_value.y), tf_Render3D_1SrgbToLinear_3_0float(t_value.z));
}

vec3 tf_Render3D_1MaterialNormal(tt_Render3D_1SurfaceInput t_input, vec3 t_sampled, float t_scale) {
    vec3 t_decoded = ((t_sampled * 2.0) - vec3(1.0));
    vec3 t_tangentNormal = tide_normalize3(vec3((t_decoded.xy * t_scale), t_decoded.z));
    vec3 t_n = ((dot(t_input.m_normal, t_input.m_normal) >= 0.5) ? t_input.m_normal : vec3(0.0, 1.0, 0.0));
    vec3 t_mapped = tide_normalize3((((t_input.m_tangent * t_tangentNormal.x) + (t_input.m_bitangent * t_tangentNormal.y)) + (t_n * t_tangentNormal.z)));
    return ((dot(t_mapped, t_mapped) >= 0.5) ? t_mapped : t_n);
}

void tf_Render3D_1StandardSurface(tt_Render3D_1SurfaceInput t_input, inout tt_Render3D_1Surface t_surface) {
}

void tf_GIEmission(tt_Render3D_1SurfaceInput t_input, inout tt_Render3D_1Surface t_surface, vec3 t_emission) {
    t_surface.m_emission = t_emission;
}

void tf_TideGenerated_1Lambda6511619f(tt_Render3D_1SurfaceInput t_input, inout tt_Render3D_1Surface t_surface, vec3 t_emission) {
    tf_GIEmission(t_input, t_surface, t_emission);
}

void tf_TideGenerated_1Invoke30dff975(tt_TideGenerated_1Callable30dff975 t_binding, tt_Render3D_1SurfaceInput t_arg0, inout tt_Render3D_1Surface t_arg1) {
    switch (t_binding.m_kind) {
        case 1716142310: {
            tf_Render3D_1StandardSurface(t_arg0, t_arg1);
            break;
        }
        case 1438873866: {
            tf_TideGenerated_1Lambda6511619f(t_arg0, t_arg1, t_binding.m_capture55c3790a_0emission);
            break;
        }
        default: {
        } break;
    }
    return;
}

vec3 tf_Render3D_1EvaluateIndirectPbr(tt_Render3D_1Surface t_surface, vec3 t_irradiance) {
    return ((((tide_clamp3(t_surface.m_albedo, vec3(tide_f(0.0)), vec3(tide_f(1.0))) * (1.0 - tide_clamp1(t_surface.m_metallic, tide_f(0.0), tide_f(1.0)))) * tide_clamp1(t_surface.m_occlusion, tide_f(0.0), tide_f(1.0))) * tide_max3(t_irradiance, vec3(tide_f(0.0)))) / tide_f(3.14159265));
}

vec3 tf_Render3D_1NoIndirect_48x7(tt_Render3D_1SurfaceInput t_input, tt_Render3D_1Surface t_surface) {
    return vec3(0.0);
}

tt_GlobalIllumination_1SamplingFrame tf_GlobalIllumination_1PrepareSamplingFrame(tt_GlobalIllumination_1FieldLayout t_layout, float t_intensity, int t_initialized, vec3 t_world, vec3 t_normal, vec3 t_visibilityNormal) {
    tt_GlobalIllumination_1SamplingFrame t_frame = tt_GlobalIllumination_1SamplingFrame(false, vec3(0.0), vec3(0.0), vec3(0.0));
    if ((!(((t_initialized <= 0) || (t_intensity <= 0.0)) || (t_layout.m_total <= 0)))) {
        vec3 t_n = tide_normalize3(t_normal);
        if ((!(dot(t_n, t_n) < 0.5))) {
            vec3 t_planeNormal = tide_normalize3(t_visibilityNormal);
            if ((dot(t_planeNormal, t_planeNormal) < 0.5)) {
                t_planeNormal = t_n;
            }
            t_frame.m_valid = true;
            t_frame.m_normal = t_n;
            t_frame.m_visibilityNormal = t_planeNormal;
            t_frame.m_position = (t_world + (t_n * (t_layout.m_spacing * 0.04)));
        }
    }
    return t_frame;
}

vec3 tf_Render3D_1ClampHdr(vec3 t_radiance) {
    return tide_clamp3(t_radiance, vec3(tide_f(0.0)), vec3(tide_f(65504.0)));
}

int tf_GlobalIllumination_1AtlasHeight(int t_rows) {
    return ((t_rows * 19) + 64);
}

vec4 tf_GlobalIllumination_1LookupData_47(int t_rows, int t_address) {
    if (((t_address < 0) || (t_address >= (tide_i(576) * tide_i(64))))) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    vec2 t_pixel = (vec2(float((t_address % tide_i(576))), float(((t_rows * 19) + (t_address / tide_i(576))))) + vec2(0.5));
    return tide_straight(textureLod(pixels7, (t_pixel / vec2(576.0, float(tf_GlobalIllumination_1AtlasHeight(t_rows)))), 0.0));
}

int tf_GlobalIllumination_1DecodeLookupCode(vec4 t_packed) {
    return (int(floor(((t_packed.r * tide_f(255.0)) + tide_f(0.5)))) + (int(floor(((t_packed.g * tide_f(255.0)) + tide_f(0.5)))) * 256));
}

tt_GlobalIllumination_1SampleBrick tf_GlobalIllumination_1LookupBrick_47(tt_GlobalIllumination_1FieldLayout t_layout, vec3 t_position) {
    tt_GlobalIllumination_1SampleBrick t_missing = tt_GlobalIllumination_1SampleBrick((-1), vec3(0.0), 0.0, 63);
    if (((((((((t_layout.m_nodeCount <= 0) || (t_layout.m_size <= 0.0)) || (t_position.x < t_layout.m_minimum.x)) || (t_position.y < t_layout.m_minimum.y)) || (t_position.z < t_layout.m_minimum.z)) || (t_position.x > (t_layout.m_minimum.x + t_layout.m_size))) || (t_position.y > (t_layout.m_minimum.y + t_layout.m_size))) || (t_position.z > (t_layout.m_minimum.z + t_layout.m_size)))) {
        return t_missing;
    }
    vec3 t_minimum = t_layout.m_minimum;
    float t_size = t_layout.m_size;
    int t_address = 0;
    int t_end = max(tide_i(0), min(tide_i(24), t_layout.m_depthLimit));
    for (int t_depth = 0; (t_depth <= t_end); t_depth = (t_depth + 1)) {
        if ((((t_address < 0) || (t_address >= t_layout.m_nodeCount)) || (t_address >= (tide_i(576) * tide_i(64))))) {
            return t_missing;
        }
        vec4 t_packed = tf_GlobalIllumination_1LookupData_47(t_layout.m_atlasRows, t_address);
        int t_code = tf_GlobalIllumination_1DecodeLookupCode(t_packed);
        if ((t_code <= 0)) {
            if ((t_code == 0)) {
                return tt_GlobalIllumination_1SampleBrick((-1), t_minimum, t_size, 63);
            }
            return t_missing;
        }
        if ((t_code <= 128)) {
            int t_slot = (t_code - 1);
            if ((((t_layout.m_nodeCount + (t_slot * 28)) + 27) >= (tide_i(576) * tide_i(64)))) {
                return t_missing;
            }
            return tt_GlobalIllumination_1SampleBrick(t_slot, t_minimum, t_size, int(floor(((t_packed.b * tide_f(255.0)) + tide_f(0.5)))));
        }
        if (((t_code < 256) || (t_depth == t_end))) {
            return t_missing;
        }
        int t_first = (t_code - 256);
        if (((t_first < 0) || (t_first > (t_layout.m_nodeCount - 8)))) {
            return t_missing;
        }
        t_size = (t_size * 0.5);
        vec3 t_middle = (t_minimum + vec3(t_size));
        int t_x = ((t_position.x >= t_middle.x) ? 1 : 0);
        int t_y = ((t_position.y >= t_middle.y) ? 1 : 0);
        int t_z = ((t_position.z >= t_middle.z) ? 1 : 0);
        t_minimum = (t_minimum + (vec3(float(t_x), float(t_y), float(t_z)) * t_size));
        t_address = (((t_first + t_x) + (t_y * 2)) + (t_z * 4));
    }
    return t_missing;
}

int tf_GlobalIllumination_1DecodeLookupMask(vec4 t_packed) {
    return (tf_GlobalIllumination_1DecodeLookupCode(t_packed) + (int(floor(((t_packed.b * tide_f(255.0)) + tide_f(0.5)))) * 65536));
}

int tf_GlobalIllumination_1LookupCode_47(int t_rows, int t_address) {
    return tf_GlobalIllumination_1DecodeLookupCode(tf_GlobalIllumination_1LookupData_47(t_rows, t_address));
}

vec4 tf_GlobalIllumination_1ProbeMetadata_47(int t_probe, int t_rows) {
    vec2 t_pixel = ((vec2(float((t_probe % tide_i(32))), float((t_probe / tide_i(32)))) * vec2(18.0, 19.0)) + vec2(0.5, 18.5));
    return tide_straight(textureLod(pixels7, (t_pixel / vec2(576.0, float(tf_GlobalIllumination_1AtlasHeight(t_rows)))), 0.0));
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

vec2 tf_GlobalIllumination_1AtlasUV(int t_probe, int t_rows, vec3 t_direction) {
    vec2 t_tile = (vec2(float((t_probe % tide_i(32))), float((t_probe / tide_i(32)))) * vec2(18.0, 19.0));
    return (((t_tile + vec2(1.0)) + (tf_GlobalIllumination_1OctUV(t_direction) * 16.0)) / vec2(576.0, float(tf_GlobalIllumination_1AtlasHeight(t_rows))));
}

float tf_GlobalIllumination_1ProbeVisibility(float t_mean, float t_standardDeviation, float t_inverse, float t_tolerance) {
    float t_difference = tide_max1(((t_mean - t_inverse) - tide_max1((t_mean * t_tolerance), tide_f(1e-05))), tide_f(0.0));
    if ((t_difference <= 0.0)) {
        return 1.0;
    }
    float t_sigma = tide_max1(t_standardDeviation, tide_max1((t_mean * tide_f(0.001)), tide_f(1e-05)));
    float t_variance = (t_sigma * t_sigma);
    float t_probability = (t_variance / (t_variance + (t_difference * t_difference)));
    return t_probability;
}

vec2 tf_GlobalIllumination_1ReceiverTapVisibility(vec4 t_moments, vec3 t_ray, vec3 t_normal, float t_planeDistance, float t_pointInverse, float t_spacing, float t_tolerance, vec3 t_delta, float t_metricSpacing) {
    float t_metric = ((t_metricSpacing > 0.0) ? t_metricSpacing : t_spacing);
    float t_planeWeight = tide_smoothstep1((t_metric * tide_f(0.0001)), (t_metric * tide_f(0.01)), abs(t_planeDistance));
    float t_denominator = ((abs(t_planeDistance) >= (t_metric * 1e-07)) ? t_planeDistance : ((t_planeDistance < 0.0) ? ((-t_metric) * 1e-07) : (t_metric * 1e-07)));
    float t_expected = ((t_metric * dot(t_normal, t_ray)) / t_denominator);
    float t_support = mix(tide_f(1.0), float(((t_expected > tide_f(0.0)) ? tide_i(1) : tide_i(0))), t_planeWeight);
    float t_rayNormal = dot(t_normal, t_ray);
    float t_rayDenominator = ((abs(t_rayNormal) >= 1e-06) ? t_rayNormal : ((t_rayNormal < 0.0) ? (-1e-06) : 1e-06));
    vec3 t_projected = (t_ray * ((t_planeDistance / t_metric) / t_rayDenominator));
    float t_footprint = tide_min1((dot((t_projected - (t_delta / t_metric)), (t_projected - (t_delta / t_metric))) * tide_f(4.0)), tide_f(1000000.0));
    t_support = (t_support * mix(tide_f(1.0), (tide_f(1.0) / (tide_f(1.0) + (t_footprint * t_footprint))), t_planeWeight));
    float t_inverse = mix(t_pointInverse, tide_clamp1(t_expected, tide_f(0.0), tide_f(1000.0)), t_planeWeight);
    return vec2((tf_GlobalIllumination_1ProbeVisibility(t_moments.r, t_moments.g, t_inverse, t_tolerance) * t_support), t_support);
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

vec2 tf_GlobalIllumination_1ReceiverVisibility_47(int t_probe, int t_rows, vec3 t_delta, vec3 t_normal, float t_spacing, float t_tolerance, float t_metricSpacing) {
    float t_metric = ((t_metricSpacing > 0.0) ? t_metricSpacing : t_spacing);
    vec2 t_size = vec2(576.0, float(tf_GlobalIllumination_1AtlasHeight(t_rows)));
    vec2 t_tile = (vec2(float((t_probe % tide_i(32))), float((t_probe / tide_i(32)))) * vec2(18.0, 19.0));
    vec2 t_pixel = ((tf_GlobalIllumination_1AtlasUV(t_probe, t_rows, t_delta) * t_size) - vec2(0.5));
    vec2 t_lower = floor(t_pixel);
    vec2 t_fraction = (t_pixel - t_lower);
    vec2 t_center = (t_lower + vec2(0.5));
    float t_inverse = (t_metric / tide_max1(((abs(t_delta.x) + abs(t_delta.y)) + abs(t_delta.z)), (t_metric * tide_f(0.001))));
    float t_planeDistance = dot(t_normal, t_delta);
    vec2 t_previous = vec2(0.0);
    vec2 t_top = vec2(0.0);
    vec2 t_filtered = vec2(0.0);
    float t_back = 0.0;
    int t_end = min((t_rows + tide_i(3)), tide_i(4));
    for (int t_sample = 0; (t_sample < t_end); t_sample = (t_sample + 1)) {
        int t_x = (t_sample % tide_i(2));
        int t_y = (t_sample / tide_i(2));
        vec2 t_offset = vec2(float(t_x), float(t_y));
        vec4 t_moments = tide_straight(textureLod(pixels7, ((t_center + t_offset) / t_size), 0.0));
        vec2 t_uv = ((((t_center + t_offset) - t_tile) - vec2(1.0)) / tide_f(16.0));
        vec2 t_visibility = tf_GlobalIllumination_1ReceiverTapVisibility(t_moments, tf_GlobalIllumination_1OctVector(t_uv), t_normal, t_planeDistance, t_inverse, t_spacing, t_tolerance, t_delta, t_metric);
        if ((t_sample == 0)) {
            t_back = t_moments.b;
        }
        if ((t_x == 0)) {
            t_previous = t_visibility;
        } else {
            vec2 t_row = mix(t_previous, t_visibility, vec2(t_fraction.x));
            if ((t_y == 0)) {
                t_top = t_row;
            } else {
                t_filtered = mix(t_top, t_row, vec2(t_fraction.y));
            }
        }
    }
    float t_probability = ((t_filtered.y > 1e-07) ? (t_filtered.x / t_filtered.y) : 0.0);
    return vec2(((t_probability * t_probability) * t_probability), t_back);
}

tt_GlobalIllumination_1IrradianceBlend tf_GlobalIllumination_1GatherSparseIrradiance_48x7(tt_GlobalIllumination_1FieldLayout t_layout, int t_initialized, vec3 t_world, vec3 t_n, vec3 t_visibilityNormal, vec3 t_grid, float t_tolerance, int t_baseProbe, float t_metricSpacing, int t_coordinateStride) {
    float t_metric = ((t_metricSpacing > 0.0) ? t_metricSpacing : t_layout.m_spacing);
    ivec3 t_lower = min(ivec3(floor(t_grid)), (t_layout.m_counts - ivec3(tide_i(2))));
    vec3 t_fraction = (t_grid - vec3(t_lower));
    tt_GlobalIllumination_1IrradianceBlend t_result = tt_GlobalIllumination_1IrradianceBlend(vec3(0.0), 0.0, 0.0, 0.0);
    int t_end = min(t_layout.m_total, tide_i(8));
    for (int t_sample = 0; (t_sample < t_end); t_sample = (t_sample + 1)) {
        int t_x = (t_sample % tide_i(2));
        int t_y = ((t_sample / tide_i(2)) % tide_i(2));
        int t_z = (t_sample / tide_i(4));
        ivec3 t_coordinate = (t_lower + ivec3(t_x, t_y, t_z));
        ivec3 t_lookupCoordinate = (t_coordinate * t_coordinateStride);
        int t_logical = ((t_baseProbe + t_lookupCoordinate.x) + (3 * (t_lookupCoordinate.y + (3 * t_lookupCoordinate.z))));
        int t_probe = (tf_GlobalIllumination_1LookupCode_47(t_layout.m_atlasRows, (t_layout.m_nodeCount + t_logical)) - 1);
        if ((((t_probe < 0) || (t_probe >= t_layout.m_total)) || (t_initialized <= 0))) {
            continue;
        }
        vec3 t_axis = vec3(((t_x == 0) ? (1.0 - t_fraction.x) : t_fraction.x), ((t_y == 0) ? (1.0 - t_fraction.y) : t_fraction.y), ((t_z == 0) ? (1.0 - t_fraction.z) : t_fraction.z));
        float t_spatial = ((t_axis.x * t_axis.y) * t_axis.z);
        if ((t_spatial <= 0.0)) {
            continue;
        }
        vec4 t_metadata = tf_GlobalIllumination_1ProbeMetadata_47(t_probe, t_layout.m_atlasRows);
        if ((t_metadata.a < 0.5)) {
            continue;
        }
        t_result.m_available = (t_result.m_available + t_spatial);
        vec3 t_offset = (vec3(t_metadata.r, t_metadata.g, t_metadata.b) - vec3((tide_f(128.0) / tide_f(255.0))));
        vec3 t_eye = ((t_layout.m_minimum + (vec3(t_coordinate) * t_layout.m_spacing)) + (t_offset * t_metric));
        vec3 t_delta = (t_world - t_eye);
        float t_receiverDistance = length(t_delta);
        float t_alignment = tide_clamp1((dot(t_n, (-t_delta)) / tide_max1(t_receiverDistance, tide_f(1e-06))), float((-tide_i(1))), tide_f(1.0));
        float t_radius = tide_saturate1((t_receiverDistance / (t_metric * tide_f(0.04))));
        float t_forward = tide_max1(t_alignment, tide_f(0.0));
        float t_facing = mix(tide_f(1.0), ((t_forward * t_forward) + tide_f(0.01)), t_radius);
        float t_baseWeight = (t_spatial * t_facing);
        t_result.m_base = (t_result.m_base + t_baseWeight);
        float t_side = dot(t_visibilityNormal, (-t_delta));
        float t_frontSupport = tide_smoothstep1(((-t_metric) * tide_f(0.001)), tide_f(0.0), t_side);
        if ((t_frontSupport <= 0.0)) {
            continue;
        }
        vec2 t_visibility = tf_GlobalIllumination_1ReceiverVisibility_47(t_probe, t_layout.m_atlasRows, t_delta, t_visibilityNormal, t_layout.m_spacing, t_tolerance, t_metric);
        if ((t_visibility.y >= 0.25)) {
            continue;
        }
        float t_weight = ((t_baseWeight * t_frontSupport) * t_visibility.x);
        vec4 t_sampled = tide_straight(textureLod(pixels8, tf_GlobalIllumination_1AtlasUV(t_probe, t_layout.m_atlasRows, t_n), 0.0));
        t_result.m_light = (t_result.m_light + (tf_Render3D_1ClampHdr(vec3(t_sampled.r, t_sampled.g, t_sampled.b)) * t_weight));
        t_result.m_weight = (t_result.m_weight + t_weight);
    }
    return t_result;
}

vec3 tf_GlobalIllumination_1ResolveIrradiance(tt_GlobalIllumination_1IrradianceBlend t_blend) {
    float t_confidence = (t_blend.m_weight / tide_max1(t_blend.m_base, tide_f(1e-07)));
    float t_trusted = tide_smoothstep1(tide_f(1e-05), tide_f(0.001), t_confidence);
    return ((t_blend.m_weight > 1e-07) ? tf_Render3D_1ClampHdr(((t_blend.m_light / t_blend.m_weight) * t_trusted)) : vec3(0.0));
}

vec3 tf_GlobalIllumination_1SampleBrickIrradiance_48x7(tt_GlobalIllumination_1FieldLayout t_layout, int t_initialized, tt_GlobalIllumination_1SampleBrick t_brick, vec3 t_world, vec3 t_n, vec3 t_visibilityNormal, vec3 t_position, float t_tolerance) {
    tt_GlobalIllumination_1FieldLayout t_local = tt_GlobalIllumination_1FieldLayout(t_brick.m_minimum, (t_brick.m_size * 0.5), ivec3(3), t_layout.m_total, t_layout.m_atlasRows, 0.0, true, 0.0, t_layout.m_nodeCount, 0);
    vec3 t_grid = tide_clamp3(((t_position - t_brick.m_minimum) / t_local.m_spacing), vec3(tide_f(0.0)), vec3(tide_f(2.0)));
    tt_GlobalIllumination_1IrradianceBlend t_blend = tf_GlobalIllumination_1GatherSparseIrradiance_48x7(t_local, t_initialized, t_world, t_n, t_visibilityNormal, t_grid, t_tolerance, (t_brick.m_slot * 28), t_layout.m_spacing, 1);
    vec3 t_fine = tf_GlobalIllumination_1ResolveIrradiance(t_blend);
    vec3 t_result = t_fine;
    if ((t_blend.m_available < 0.99999)) {
        tt_GlobalIllumination_1FieldLayout t_coarse = tt_GlobalIllumination_1FieldLayout(t_brick.m_minimum, t_brick.m_size, ivec3(2), t_layout.m_total, t_layout.m_atlasRows, 0.0, true, 0.0, t_layout.m_nodeCount, 0);
        vec3 t_coarseGrid = tide_clamp3(((t_position - t_brick.m_minimum) / t_brick.m_size), vec3(tide_f(0.0)), vec3(tide_f(1.0)));
        tt_GlobalIllumination_1IrradianceBlend t_corners = tf_GlobalIllumination_1GatherSparseIrradiance_48x7(t_coarse, t_initialized, t_world, t_n, t_visibilityNormal, t_coarseGrid, t_tolerance, (t_brick.m_slot * 28), t_layout.m_spacing, 2);
        t_result = mix(tf_GlobalIllumination_1ResolveIrradiance(t_corners), t_fine, vec3(tide_saturate1(t_blend.m_available)));
    }
    return t_result;
}

vec3 tf_GlobalIllumination_1SampleSparse_48x7(tt_GlobalIllumination_1FieldLayout t_layout, int t_initialized, vec3 t_world, vec3 t_n, vec3 t_visibilityNormal, vec3 t_position, float t_tolerance) {
    float t_radius = (t_layout.m_spacing * 0.25);
    tt_GlobalIllumination_1SampleBrick t_own = tf_GlobalIllumination_1LookupBrick_47(t_layout, t_position);
    vec3 t_result = vec3(0.0);
    int t_nx = ((t_position.x < (t_own.m_minimum.x + t_radius)) ? (-1) : ((t_position.x > ((t_own.m_minimum.x + t_own.m_size) - t_radius)) ? 1 : 0));
    int t_ny = ((t_position.y < (t_own.m_minimum.y + t_radius)) ? (-1) : ((t_position.y > ((t_own.m_minimum.y + t_own.m_size) - t_radius)) ? 1 : 0));
    int t_nz = ((t_position.z < (t_own.m_minimum.z + t_radius)) ? (-1) : ((t_position.z > ((t_own.m_minimum.z + t_own.m_size) - t_radius)) ? 1 : 0));
    int t_sx = ((t_nx > 0) ? 1 : 0);
    int t_sy = ((t_ny > 0) ? 1 : 0);
    int t_sz = ((t_nz > 0) ? 1 : 0);
    bool t_transition = ((((t_nx != 0) && ((t_own.m_transitions & (1 << t_sx)) != 0)) || ((t_ny != 0) && ((t_own.m_transitions & (1 << (2 + t_sy))) != 0))) || ((t_nz != 0) && ((t_own.m_transitions & (1 << (4 + t_sz))) != 0)));
    if ((((t_own.m_slot >= 0) && (!t_transition)) && ((((t_nx != 0) && (t_ny != 0)) || ((t_nx != 0) && (t_nz != 0))) || ((t_ny != 0) && (t_nz != 0))))) {
        int t_mask = tf_GlobalIllumination_1DecodeLookupMask(tf_GlobalIllumination_1LookupData_47(t_layout.m_atlasRows, ((t_layout.m_nodeCount + (t_own.m_slot * 28)) + 27)));
        t_transition = ((((((t_nx != 0) && (t_ny != 0)) && ((t_mask & (1 << (t_sx + (t_sy * 2)))) != 0)) || (((t_nx != 0) && (t_nz != 0)) && ((t_mask & (1 << ((4 + t_sx) + (t_sz * 2)))) != 0))) || (((t_ny != 0) && (t_nz != 0)) && ((t_mask & (1 << ((8 + t_sy) + (t_sz * 2)))) != 0))) || ((((t_nx != 0) && (t_ny != 0)) && (t_nz != 0)) && ((t_mask & (1 << (((12 + t_sx) + (t_sy * 2)) + (t_sz * 4)))) != 0)));
    }
    bool t_single = ((t_own.m_slot >= 0) && (!t_transition));
    if (t_single) {
        t_result = tf_GlobalIllumination_1SampleBrickIrradiance_48x7(t_layout, t_initialized, t_own, t_world, t_n, t_visibilityNormal, t_position, t_tolerance);
    } else {
        vec3 t_lower = (t_position - vec3(t_radius));
        vec3 t_upper = (t_position + vec3(t_radius));
        ivec4 t_first = ivec4((-1));
        ivec4 t_second = ivec4((-1));
        tt_GlobalIllumination_1SampleBrick t_cached = t_own;
        int t_end = min(t_layout.m_total, tide_i(8));
        for (int t_corner = 0; (t_corner < t_end); t_corner = (t_corner + 1)) {
            vec3 t_point = vec3((((t_corner & 1) == 0) ? t_lower.x : t_upper.x), (((t_corner & 2) == 0) ? t_lower.y : t_upper.y), (((t_corner & 4) == 0) ? t_lower.z : t_upper.z));
            tt_GlobalIllumination_1SampleBrick t_brick = t_own;
            if ((((((((t_own.m_size <= 0.0) || (t_point.x < t_own.m_minimum.x)) || (t_point.y < t_own.m_minimum.y)) || (t_point.z < t_own.m_minimum.z)) || (t_point.x >= (t_own.m_minimum.x + t_own.m_size))) || (t_point.y >= (t_own.m_minimum.y + t_own.m_size))) || (t_point.z >= (t_own.m_minimum.z + t_own.m_size)))) {
                if ((((((((t_cached.m_size > 0.0) && (t_point.x >= t_cached.m_minimum.x)) && (t_point.y >= t_cached.m_minimum.y)) && (t_point.z >= t_cached.m_minimum.z)) && (t_point.x < (t_cached.m_minimum.x + t_cached.m_size))) && (t_point.y < (t_cached.m_minimum.y + t_cached.m_size))) && (t_point.z < (t_cached.m_minimum.z + t_cached.m_size)))) {
                    t_brick = t_cached;
                } else {
                    t_brick = tf_GlobalIllumination_1LookupBrick_47(t_layout, t_point);
                    t_cached = t_brick;
                }
            }
            int t_slot = t_brick.m_slot;
            if ((((((((((t_slot < 0) || (t_slot == t_first.x)) || (t_slot == t_first.y)) || (t_slot == t_first.z)) || (t_slot == t_first.w)) || (t_slot == t_second.x)) || (t_slot == t_second.y)) || (t_slot == t_second.z)) || (t_slot == t_second.w))) {
                continue;
            }
            if ((t_corner == 0)) {
                t_first.x = t_slot;
            } else {
                if ((t_corner == 1)) {
                    t_first.y = t_slot;
                } else {
                    if ((t_corner == 2)) {
                        t_first.z = t_slot;
                    } else {
                        if ((t_corner == 3)) {
                            t_first.w = t_slot;
                        } else {
                            if ((t_corner == 4)) {
                                t_second.x = t_slot;
                            } else {
                                if ((t_corner == 5)) {
                                    t_second.y = t_slot;
                                } else {
                                    if ((t_corner == 6)) {
                                        t_second.z = t_slot;
                                    } else {
                                        t_second.w = t_slot;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            vec3 t_overlap = (tide_max3((tide_min3(t_upper, (t_brick.m_minimum + vec3(t_brick.m_size))) - tide_max3(t_lower, t_brick.m_minimum)), vec3(tide_f(0.0))) / (2.0 * t_radius));
            float t_weight = ((t_overlap.x * t_overlap.y) * t_overlap.z);
            if ((t_weight <= 0.0)) {
                continue;
            }
            t_result = (t_result + (tf_GlobalIllumination_1SampleBrickIrradiance_48x7(t_layout, t_initialized, t_brick, t_world, t_n, t_visibilityNormal, t_position, t_tolerance) * t_weight));
        }
    }
    return t_result;
}

vec3 tf_GlobalIllumination_1SampleWorld_3_0Texture_0Texture_0GlobalIllumination_0FieldLayout_0float_0int_0float3_0float3_0float3_0float_48x7(tt_GlobalIllumination_1FieldLayout t_layout, float t_intensity, int t_initialized, vec3 t_world, vec3 t_normal, vec3 t_visibilityNormal, float t_tolerance) {
    tt_GlobalIllumination_1SamplingFrame t_frame = tf_GlobalIllumination_1PrepareSamplingFrame(t_layout, t_intensity, t_initialized, t_world, t_normal, t_visibilityNormal);
    if ((!t_frame.m_valid)) {
        return vec3(0.0);
    }
    return tf_Render3D_1ClampHdr((tf_GlobalIllumination_1SampleSparse_48x7(t_layout, t_initialized, t_world, t_frame.m_normal, t_frame.m_visibilityNormal, t_frame.m_position, t_tolerance) * t_intensity));
}

vec3 tf_GlobalIllumination_1Indirect_48x7(tt_Render3D_1SurfaceInput t_input, tt_Render3D_1Surface t_surface, tt_GlobalIllumination_1FieldLayout t_layout, float t_intensity, int t_initialized) {
    return tf_GlobalIllumination_1SampleWorld_3_0Texture_0Texture_0GlobalIllumination_0FieldLayout_0float_0int_0float3_0float3_0float3_0float_48x7(t_layout, t_intensity, t_initialized, t_input.m_world, t_surface.m_normal, t_input.m_normal, 0.01);
}

vec3 tf_TideGenerated_1Lambda29d9b69c_48x7(tt_Render3D_1SurfaceInput t_input, tt_Render3D_1Surface t_surface, tt_GlobalIllumination_1FieldLayout t_layout, float t_intensity, int t_initialized) {
    return tf_GlobalIllumination_1Indirect_48x7(t_input, t_surface, t_layout, t_intensity, t_initialized);
}

vec3 tf_TideGenerated_1Invoke3bedd4bb_48x7(tt_TideGenerated_1Callable3bedd4bb t_binding, tt_Render3D_1SurfaceInput t_arg0, tt_Render3D_1Surface t_arg1) {
    vec3 t_result = vec3(0.0);
    switch (t_binding.m_kind) {
        case 1300728831: {
            t_result = tf_Render3D_1NoIndirect_48x7(t_arg0, t_arg1);
            break;
        }
        case 1947573575: {
            t_result = tf_TideGenerated_1Lambda29d9b69c_48x7(t_arg0, t_arg1, t_binding.m_capture74159d47_0layout, t_binding.m_capture74159d47_0intensity, t_binding.m_capture74159d47_0initialized);
            break;
        }
        default: {
        } break;
    }
    return t_result;
}

tt_Render3D_1LightData tide_list0(int tide_i) {
    tt_Render3D_1LightData tide_result = tt_Render3D_1LightData(0, vec3(0.0), vec3(0.0), vec3(0.0), 0.0, 0.0, 0.0, 0.0, false, 0, 0, 0);
    if (!(tide_i < 0 || tide_i >= tide_v4.z)) {
    int tide_word = tide_i * 20;
    int tide_first = tide_word >> 2;
    ivec4 tide_q0 = texelFetch(pixels0, ivec2((tide_first + 0) & 1023, (tide_first + 0) >> 10), 0);
    vec4 tide_p0 = intBitsToFloat(tide_q0);
    ivec4 tide_q1 = texelFetch(pixels0, ivec2((tide_first + 1) & 1023, (tide_first + 1) >> 10), 0);
    vec4 tide_p1 = intBitsToFloat(tide_q1);
    ivec4 tide_q2 = texelFetch(pixels0, ivec2((tide_first + 2) & 1023, (tide_first + 2) >> 10), 0);
    vec4 tide_p2 = intBitsToFloat(tide_q2);
    ivec4 tide_q3 = texelFetch(pixels0, ivec2((tide_first + 3) & 1023, (tide_first + 3) >> 10), 0);
    vec4 tide_p3 = intBitsToFloat(tide_q3);
    ivec4 tide_q4 = texelFetch(pixels0, ivec2((tide_first + 4) & 1023, (tide_first + 4) >> 10), 0);
    tide_result = tt_Render3D_1LightData(tide_q3.y, tide_p0.xyz, vec3(tide_p0.w, tide_p1.x, tide_p1.y), vec3(tide_p1.z, tide_p1.w, tide_p2.x), tide_p2.y, tide_p2.z, tide_p2.w, tide_p3.x, (tide_q3.z != 0), tide_q3.w, tide_q4.x, tide_q4.y);
    }
    return tide_result;
}

float tf_Render3D_1PointAttenuation(float t_distance, float t_range) {
    if ((t_range <= 0.0)) {
        return 0.0;
    }
    float t_d = tide_max1(t_distance, tide_f(0.0));
    if ((t_d >= t_range)) {
        return 0.0;
    }
    float t_ratio = (t_d / t_range);
    float t_square = (t_ratio * t_ratio);
    float t_window = tide_clamp1((tide_f(1.0) - (t_square * t_square)), tide_f(0.0), tide_f(1.0));
    if ((t_d > 1e+19)) {
        return (((t_window * t_window) / t_d) / t_d);
    }
    return ((t_window * t_window) / tide_max1((t_d * t_d), tide_f(0.0001)));
}

float tf_Render3D_1SpotAttenuation(float t_cosine, float t_innerCosine, float t_outerCosine) {
    float t_outer = tide_clamp1(t_outerCosine, tide_f(0.0), tide_f(1.0));
    float t_inner = tide_clamp1(t_innerCosine, t_outer, tide_f(1.0));
    float t_angle = tide_clamp1(t_cosine, float((-tide_i(1))), tide_f(1.0));
    if ((t_inner <= t_outer)) {
        return float(((t_angle >= t_outer) ? 1 : 0));
    }
    float t_t = tide_clamp1(((t_angle - t_outer) / (t_inner - t_outer)), tide_f(0.0), tide_f(1.0));
    return ((t_t * t_t) * (3.0 - (2.0 * t_t)));
}

int tf_Render3D_1PointFace(vec3 t_direction) {
    vec3 t_absolute = abs(t_direction);
    if (((t_absolute.x >= t_absolute.y) && (t_absolute.x >= t_absolute.z))) {
        return ((t_direction.x >= 0.0) ? 0 : 1);
    }
    if ((t_absolute.y >= t_absolute.z)) {
        return ((t_direction.y >= 0.0) ? 2 : 3);
    }
    return ((t_direction.z >= 0.0) ? 4 : 5);
}

tt_Render3D_1ShadowView tide_list1(int tide_i) {
    tt_Render3D_1ShadowView tide_result = tt_Render3D_1ShadowView(mat4(0.0), mat4(0.0), mat4(0.0), vec4(0.0), 0.0, 0.0, 0.0);
    if (!(tide_i < 0 || tide_i >= tide_v4.w)) {
    int tide_word = tide_i * 56;
    int tide_first = tide_word >> 2;
    ivec4 tide_q0 = texelFetch(pixels1, ivec2((tide_first + 0) & 1023, (tide_first + 0) >> 10), 0);
    vec4 tide_p0 = intBitsToFloat(tide_q0);
    ivec4 tide_q1 = texelFetch(pixels1, ivec2((tide_first + 1) & 1023, (tide_first + 1) >> 10), 0);
    vec4 tide_p1 = intBitsToFloat(tide_q1);
    ivec4 tide_q2 = texelFetch(pixels1, ivec2((tide_first + 2) & 1023, (tide_first + 2) >> 10), 0);
    vec4 tide_p2 = intBitsToFloat(tide_q2);
    ivec4 tide_q3 = texelFetch(pixels1, ivec2((tide_first + 3) & 1023, (tide_first + 3) >> 10), 0);
    vec4 tide_p3 = intBitsToFloat(tide_q3);
    ivec4 tide_q4 = texelFetch(pixels1, ivec2((tide_first + 4) & 1023, (tide_first + 4) >> 10), 0);
    vec4 tide_p4 = intBitsToFloat(tide_q4);
    ivec4 tide_q5 = texelFetch(pixels1, ivec2((tide_first + 5) & 1023, (tide_first + 5) >> 10), 0);
    vec4 tide_p5 = intBitsToFloat(tide_q5);
    ivec4 tide_q6 = texelFetch(pixels1, ivec2((tide_first + 6) & 1023, (tide_first + 6) >> 10), 0);
    vec4 tide_p6 = intBitsToFloat(tide_q6);
    ivec4 tide_q7 = texelFetch(pixels1, ivec2((tide_first + 7) & 1023, (tide_first + 7) >> 10), 0);
    vec4 tide_p7 = intBitsToFloat(tide_q7);
    ivec4 tide_q8 = texelFetch(pixels1, ivec2((tide_first + 8) & 1023, (tide_first + 8) >> 10), 0);
    vec4 tide_p8 = intBitsToFloat(tide_q8);
    ivec4 tide_q9 = texelFetch(pixels1, ivec2((tide_first + 9) & 1023, (tide_first + 9) >> 10), 0);
    vec4 tide_p9 = intBitsToFloat(tide_q9);
    ivec4 tide_q10 = texelFetch(pixels1, ivec2((tide_first + 10) & 1023, (tide_first + 10) >> 10), 0);
    vec4 tide_p10 = intBitsToFloat(tide_q10);
    ivec4 tide_q11 = texelFetch(pixels1, ivec2((tide_first + 11) & 1023, (tide_first + 11) >> 10), 0);
    vec4 tide_p11 = intBitsToFloat(tide_q11);
    ivec4 tide_q12 = texelFetch(pixels1, ivec2((tide_first + 12) & 1023, (tide_first + 12) >> 10), 0);
    vec4 tide_p12 = intBitsToFloat(tide_q12);
    ivec4 tide_q13 = texelFetch(pixels1, ivec2((tide_first + 13) & 1023, (tide_first + 13) >> 10), 0);
    vec4 tide_p13 = intBitsToFloat(tide_q13);
    tide_result = tt_Render3D_1ShadowView(mat4(tide_p0, tide_p1, tide_p2, tide_p3), mat4(tide_p4, tide_p5, tide_p6, tide_p7), mat4(tide_p8, tide_p9, tide_p10, tide_p11), tide_p12, tide_p13.x, tide_p13.y, tide_p13.z);
    }
    return tide_result;
}

tt_Render3D_1ShadowKernel tf_Render3D_1ShadowAxis(float t_coordinate) {
    float t_base = floor(t_coordinate);
    float t_phase = (t_coordinate - t_base);
    vec2 t_weight = vec2((3.0 - (2.0 * t_phase)), (1.0 + (2.0 * t_phase)));
    return tt_Render3D_1ShadowKernel(vec2(((t_base - 1.0) + ((2.0 - t_phase) / t_weight.x)), ((t_base + 1.0) + (t_phase / t_weight.y))), t_weight);
}

float tf_Render3D_1ShadowVisibility_49(tt_Render3D_1ShadowView t_shadow, vec3 t_world, float t_bias, float t_texel, bool t_pointFace) {
    vec4 t_projected = (t_shadow.m_worldToClip * vec4(t_world, tide_f(1.0)));
    if ((t_projected.w <= 0.0)) {
        return 1.0;
    }
    vec3 t_position = (t_projected.xyz / t_projected.w);
    if (((t_position.z < float((-1))) || (t_position.z > 1.0))) {
        return 1.0;
    }
    if (((!t_pointFace) && ((((t_position.x < float((-1))) || (t_position.x > 1.0)) || (t_position.y < float((-1)))) || (t_position.y > 1.0)))) {
        return 1.0;
    }
    vec2 t_uv = (t_shadow.m_uvRect.xy + (vec2(((t_position.x * 0.5) + 0.5), (0.5 - (t_position.y * 0.5))) * t_shadow.m_uvRect.zw));
    float t_depth = (((t_position.z * 0.5) + 0.5) - (t_bias * t_shadow.m_biasScale));
    vec2 t_minimum = (t_shadow.m_uvRect.xy + vec2((t_texel * 0.5)));
    vec2 t_maximum = ((t_shadow.m_uvRect.xy + t_shadow.m_uvRect.zw) - vec2((t_texel * 0.5)));
    tt_Render3D_1ShadowKernel t_x = tf_Render3D_1ShadowAxis(((t_uv.x / t_texel) - 0.5));
    tt_Render3D_1ShadowKernel t_y = tf_Render3D_1ShadowAxis(((t_uv.y / t_texel) - 0.5));
    vec2 t_u = ((t_x.m_offset + vec2(0.5)) * t_texel);
    vec2 t_v = ((t_y.m_offset + vec2(0.5)) * t_texel);
    return ((((((textureGrad(pixels9, vec3(tide_clamp2(vec2(t_u.x, t_v.x), t_minimum, t_maximum), clamp(t_depth, 0.0, 1.0)), vec2(0.0), vec2(0.0)) * t_x.m_weight.x) * t_y.m_weight.x) + ((textureGrad(pixels9, vec3(tide_clamp2(vec2(t_u.y, t_v.x), t_minimum, t_maximum), clamp(t_depth, 0.0, 1.0)), vec2(0.0), vec2(0.0)) * t_x.m_weight.y) * t_y.m_weight.x)) + ((textureGrad(pixels9, vec3(tide_clamp2(vec2(t_u.x, t_v.y), t_minimum, t_maximum), clamp(t_depth, 0.0, 1.0)), vec2(0.0), vec2(0.0)) * t_x.m_weight.x) * t_y.m_weight.y)) + ((textureGrad(pixels9, vec3(tide_clamp2(vec2(t_u.y, t_v.y), t_minimum, t_maximum), clamp(t_depth, 0.0, 1.0)), vec2(0.0), vec2(0.0)) * t_x.m_weight.y) * t_y.m_weight.y)) * (tide_f(1.0) / tide_f(16.0)));
}

float tf_Render3D_1DirectionalVisibility_49x1(tt_Render3D_1LightData t_light, vec3 t_world, float t_viewDepth, float t_bias, float t_texel) {
    if ((t_viewDepth < 0.0)) {
        return 1.0;
    }
    for (int t_cascade = 0; (t_cascade < t_light.m_cascadeCount); t_cascade = (t_cascade + 1)) {
        tt_Render3D_1ShadowView t_shadow = tide_list1((t_light.m_shadowIndex + t_cascade));
        if ((t_viewDepth > t_shadow.m_cascadeEnd)) {
            continue;
        }
        float t_visibility = tf_Render3D_1ShadowVisibility_49(t_shadow, t_world, t_bias, t_texel, false);
        if ((t_viewDepth <= t_shadow.m_cascadeBlend)) {
            return t_visibility;
        }
        float t_next = (((t_cascade + 1) < t_light.m_cascadeCount) ? tf_Render3D_1ShadowVisibility_49(tide_list1(((t_light.m_shadowIndex + t_cascade) + 1)), t_world, t_bias, t_texel, false) : 1.0);
        float t_blend = tide_saturate1(((t_viewDepth - t_shadow.m_cascadeBlend) / tide_max1((t_shadow.m_cascadeEnd - t_shadow.m_cascadeBlend), tide_f(1e-06))));
        return mix(t_visibility, t_next, ((t_blend * t_blend) * (tide_f(3.0) - (tide_f(2.0) * t_blend))));
    }
    return 1.0;
}

vec3 tf_Render3D_1EvaluateDirectPbr(tt_Render3D_1Surface t_surface, vec3 t_viewDirection, vec3 t_lightDirection, vec3 t_radiance, float t_visibility) {
    vec3 t_n = tide_normalize3(t_surface.m_normal);
    vec3 t_v = tide_normalize3(t_viewDirection);
    vec3 t_l = tide_normalize3(t_lightDirection);
    float t_noL = tide_clamp1(dot(t_n, t_l), tide_f(0.0), tide_f(1.0));
    float t_noV = tide_clamp1(dot(t_n, t_v), tide_f(0.0), tide_f(1.0));
    if (((t_noL <= 0.0) || (t_noV <= 0.0))) {
        return vec3(0.0);
    }
    vec3 t_h = tide_normalize3((t_v + t_l));
    float t_noH = tide_clamp1(dot(t_n, t_h), tide_f(0.0), tide_f(1.0));
    float t_voH = tide_clamp1(dot(t_v, t_h), tide_f(0.0), tide_f(1.0));
    float t_roughness = tide_clamp1(t_surface.m_roughness, tide_f(0.05), tide_f(1.0));
    float t_metallic = tide_clamp1(t_surface.m_metallic, tide_f(0.0), tide_f(1.0));
    vec3 t_albedo = tide_clamp3(t_surface.m_albedo, vec3(tide_f(0.0)), vec3(tide_f(1.0)));
    float t_alpha = (t_roughness * t_roughness);
    float t_a2 = (t_alpha * t_alpha);
    float t_noH2 = (t_noH * t_noH);
    float t_denominator = ((1.0 - t_noH2) + (t_a2 * t_noH2));
    float t_distribution = (t_a2 / ((3.14159265 * t_denominator) * t_denominator));
    float t_ggxV = (t_noL * sqrt((((t_noV * t_noV) * (tide_f(1.0) - t_a2)) + t_a2)));
    float t_ggxL = (t_noV * sqrt((((t_noL * t_noL) * (tide_f(1.0) - t_a2)) + t_a2)));
    float t_smith = (0.5 / tide_max1((t_ggxV + t_ggxL), tide_f(1e-12)));
    vec3 t_f0 = mix(vec3(tide_f(0.04)), t_albedo, vec3(t_metallic));
    float t_oneMinusVoH = (1.0 - t_voH);
    float t_square = (t_oneMinusVoH * t_oneMinusVoH);
    float t_fifth = ((t_square * t_square) * t_oneMinusVoH);
    vec3 t_fresnel = (t_f0 + ((vec3(1.0) - t_f0) * t_fifth));
    vec3 t_specular = ((t_distribution * t_smith) * t_fresnel);
    vec3 t_diffuse = ((((vec3(1.0) - t_fresnel) * (1.0 - t_metallic)) * t_albedo) / tide_f(3.14159265));
    return (((t_diffuse + t_specular) * tide_max3(t_radiance, vec3(tide_f(0.0)))) * (t_noL * tide_clamp1(t_visibility, tide_f(0.0), tide_f(1.0))));
}

void ts_Render3D_1Lit(tt_Render3D_1MeshVertex t_vertex, inout vec4 t_color, inout vec4 t_captureData) {
    vec3 t_normal = tide_normalize3(mat3(tide_normals0, tide_normals1, tide_normals2) * (t_vertex.m_normal));
    vec3 t_viewDirection = tide_normalize3((((tide_v2.w != 0) ? tide_u3.yzw : tide_u0.xyz) - tide_world));
    tt_Render3D_1SurfaceInput t_frame = tf_Render3D_1MaterialInput(tide_world, t_normal, t_vertex.m_tangent, tide_normalize3(mat3(tide_normals0, tide_normals1, tide_normals2) * (tf_Render3D_1TangentPlaneNormal(t_vertex.m_normal, t_vertex.m_tangent))), tide_normalize3(mat3(tide_normals0, tide_normals1, tide_normals2) * (tf_Render3D_1TangentPlaneSecond(t_vertex.m_normal, t_vertex.m_tangent))), tide_u5.w, t_vertex.m_uv, t_viewDirection, (tide_g0.z != 0));
    vec2 t_hookedUV = tf_Render3D_1MaterialUV(t_frame, tide_f3.yz, vec2(tide_f3.w, tide_f4.x), tt_TideGenerated_1Callable0860b6c3(tide_g0.w));
    vec2 t_uv = tf_Render3D_1MaterialParallax_42(t_frame, t_hookedUV, (tide_v4.x != 0), tide_f3.x);
    tt_Render3D_1SurfaceInput t_input = tf_Render3D_1MaterialUVInput(t_frame, t_uv);
    vec4 t_sampled = tide_straight(textureLod(pixels3, t_uv, 0.0));
    tt_Render3D_1Coverage t_coverage = tf_Render3D_1MaterialCoverage(t_input, ((t_sampled.a * tide_f0.a) * t_vertex.m_color.a), tide_f2.y, tt_TideGenerated_1Callable741b56c4(tide_g1.x));
    if (((tide_g0.x == 1) && (t_coverage.m_alpha < t_coverage.m_cutoff))) {
        discard;
    }
    vec3 t_texel = tf_Render3D_1SrgbToLinear_3_0float3(vec3(t_sampled.r, t_sampled.g, t_sampled.b));
    vec3 t_tint = tf_Render3D_1SrgbToLinear_3_0float3(vec3(tide_f0.r, tide_f0.g, tide_f0.b));
    vec3 t_vertexColor = tf_Render3D_1SrgbToLinear_3_0float3(vec3(t_vertex.m_color.r, t_vertex.m_color.g, t_vertex.m_color.b));
    tt_Render3D_1Surface t_result = tt_Render3D_1Surface(((t_texel * t_tint) * t_vertexColor), t_input.m_normal, tide_f1.y, tide_f1.x, vec3(tide_f1.z, tide_f1.w, tide_f2.x), 1.0);
    if ((tide_v3.y != 0)) {
        vec4 t_sampledNormal = textureLod(pixels4, t_uv, 0.0);
        t_result.m_normal = tf_Render3D_1MaterialNormal(t_input, vec3(t_sampledNormal.r, t_sampledNormal.g, t_sampledNormal.b), tide_f2.z);
    }
    if ((tide_v3.z != 0)) {
        vec4 t_mask = textureLod(pixels5, t_uv, 0.0);
        t_result.m_occlusion = mix(tide_f(1.0), tide_clamp1(t_mask.r, tide_f(0.0), tide_f(1.0)), tide_clamp1(tide_f2.w, tide_f(0.0), tide_f(1.0)));
        t_result.m_roughness = (t_result.m_roughness * t_mask.g);
        t_result.m_metallic = (t_result.m_metallic * t_mask.b);
    }
    if ((tide_v3.w != 0)) {
        vec4 t_emission = tide_straight(textureLod(pixels6, t_uv, 0.0));
        t_result.m_emission = (t_result.m_emission * tf_Render3D_1SrgbToLinear_3_0float3(vec3(t_emission.r, t_emission.g, t_emission.b)));
    }
    tf_TideGenerated_1Invoke30dff975(tt_TideGenerated_1Callable30dff975(tide_v4.y, tide_u6.xyz), t_input, t_result);
    vec3 t_radiance = tide_max3(t_result.m_emission, vec3(tide_f(0.0)));
    if (((!(tide_v2.w != 0)) && (((vec3(tide_u0.w, tide_u1.x, tide_u1.y).x > 0.0) || (vec3(tide_u0.w, tide_u1.x, tide_u1.y).y > 0.0)) || (vec3(tide_u0.w, tide_u1.x, tide_u1.y).z > 0.0)))) {
        t_radiance = (t_radiance + tf_Render3D_1EvaluateIndirectPbr(t_result, vec3(tide_u0.w, tide_u1.x, tide_u1.y)));
    }
    if ((!(tide_v2.w != 0))) {
        vec3 t_irradiance = tf_TideGenerated_1Invoke3bedd4bb_48x7(tt_TideGenerated_1Callable3bedd4bb(tide_v0.y, tt_GlobalIllumination_1FieldLayout(vec3(tide_u1.z, tide_u1.w, tide_u2.x), tide_u2.y, ivec3(tide_v0.z, tide_v0.w, tide_v1.x), tide_v1.y, tide_v1.z, tide_u2.z, (tide_v1.w != 0), tide_u2.w, tide_v2.x, tide_v2.y), tide_u3.x, tide_v2.z), t_input, t_result);
        if ((((t_irradiance.x > 0.0) || (t_irradiance.y > 0.0)) || (t_irradiance.z > 0.0))) {
            t_radiance = (t_radiance + tf_Render3D_1EvaluateIndirectPbr(t_result, t_irradiance));
        }
    }
    for (int tide_k0 = 0; tide_k0 < tide_v4.z; tide_k0++) {
        tt_Render3D_1LightData t_light = tide_list0(tide_k0);
        vec3 t_toward = ((t_light.m_kind == 0) ? (-t_light.m_direction) : tide_normalize3((t_light.m_position - tide_world)));
        float t_attenuation = 1.0;
        if ((t_light.m_kind != 0)) {
            t_attenuation = tf_Render3D_1PointAttenuation(length((t_light.m_position - tide_world)), t_light.m_range);
            if ((t_light.m_kind == 2)) {
                t_attenuation = (t_attenuation * tf_Render3D_1SpotAttenuation(dot(t_light.m_direction, (-t_toward)), t_light.m_innerCosine, t_light.m_outerCosine));
            }
        }
        if ((t_attenuation <= 0.0)) {
            continue;
        }
        float t_visibility = 1.0;
        if ((((tide_v3.x != 0) && (tide_g0.y != 0)) && (t_light.m_shadowIndex >= 0))) {
            int t_face = ((t_light.m_kind == 1) ? tf_Render3D_1PointFace((tide_world - t_light.m_position)) : 0);
            float t_facing = tide_saturate1(dot(t_input.m_normal, t_toward));
            float t_bias = (tide_max1(tide_u4.y, tide_f(0.0)) + (tide_max1(tide_u4.z, tide_f(0.0)) * (1.0 - t_facing)));
            if ((t_light.m_cascadeCount > 0)) {
                t_visibility = tf_Render3D_1DirectionalVisibility_49x1(t_light, tide_world, dot((tide_world - tide_u0.xyz), vec3(tide_u4.w, tide_u5.x, tide_u5.y)), t_bias, tide_u5.z);
            } else {
                t_visibility = tf_Render3D_1ShadowVisibility_49(tide_list1((t_light.m_shadowIndex + t_face)), tide_world, t_bias, tide_u5.z, (t_light.m_kind == 1));
            }
        }
        if ((tide_v2.w != 0)) {
            vec3 t_diffuse = ((tide_clamp3(t_result.m_albedo, vec3(tide_f(0.0)), vec3(tide_f(1.0))) * (1.0 - tide_clamp1(t_result.m_metallic, tide_f(0.0), tide_f(1.0)))) / tide_f(3.14159265));
            t_radiance = (t_radiance + ((t_diffuse * t_light.m_intensity) * ((t_attenuation * t_visibility) * tide_saturate1(dot(tide_normalize3(t_result.m_normal), t_toward)))));
        } else {
            t_radiance = (t_radiance + tf_Render3D_1EvaluateDirectPbr(t_result, t_viewDirection, t_toward, (t_light.m_intensity * t_attenuation), t_visibility));
        }
    }
    float t_alpha = ((tide_g0.x == 2) ? t_coverage.m_alpha : 1.0);
    t_color = vec4(t_radiance.x, t_radiance.y, t_radiance.z, t_alpha);
    t_captureData = vec4(0.0, 0.0, 0.0, 1.0);
    if ((tide_v2.w != 0)) {
        float t_distance = tide_clamp1((length((tide_u3.yzw - tide_world)) / tide_max1(tide_u4.x, tide_f(0.001))), tide_f(0.0), tide_f(1.0));
        float t_backface = float((((!(tide_g0.z != 0)) && (dot(t_normal, t_viewDirection) < 0.0)) ? 1 : 0));
        t_captureData = vec4(t_distance, t_backface, 1.0, 1.0);
    }
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 tide_color1 = vec4(1.0, 1.0, 1.0, 1.0);
    ts_Render3D_1Lit(tt_Render3D_1MeshVertex(vec3(0.0), tide_s1, tide_s2, tide_s3, tide_s4), tide_color, tide_color1);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_color1 = tide_color1 * tide_tint;
    tide_color1.rgb = tide_color1.rgb * tide_color1.a;
    if (tide_screen.w > 0.0) tide_color1.rgb = tide_color1.rgb + (1.0 - tide_color1.a);
    tide_pixel = tide_color;
    tide_pixel1 = tide_color1;
}
