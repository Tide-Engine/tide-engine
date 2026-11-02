layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    vec4 tide_u0;
    vec4 tide_u1;
    vec4 tide_u2;
    ivec4 tide_v0;
    ivec4 tide_v1;
    ivec4 tide_v2;
};
uniform sampler2D pixels0;
uniform sampler2D pixels1;
out vec4 tide_pixel;
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
struct tt_GlobalIllumination_1IrradianceBlend {
    vec3 m_light;
    float m_weight;
    float m_base;
    float m_available;
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
vec3 tide_normalize3(vec3 v) {
    float l = dot(v, v);
    if (l > 1.17549435e-38) { return v * inversesqrt(l); }
    return vec3(0.0);
}
vec4 tide_straight(vec4 c) {
    if (c.a == 0.0) { return vec4(0.0); }
    return vec4(c.rgb / c.a, c.a);
}
int tf_GlobalIllumination_1DecodeLookupCode(vec4 t_packed) {
    return (int(floor(((t_packed.r * tide_f(255.0)) + tide_f(0.5)))) + (int(floor(((t_packed.g * tide_f(255.0)) + tide_f(0.5)))) * 256));
}

int tf_GlobalIllumination_1AtlasHeight(int t_rows) {
    return ((t_rows * 19) + 64);
}

vec4 tf_GlobalIllumination_1LookupData_40(int t_rows, int t_address) {
    if (((t_address < 0) || (t_address >= (tide_i(576) * tide_i(64))))) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    vec2 t_pixel = (vec2(float((t_address % tide_i(576))), float(((t_rows * 19) + (t_address / tide_i(576))))) + vec2(0.5));
    return tide_straight(textureLod(pixels0, (t_pixel / vec2(576.0, float(tf_GlobalIllumination_1AtlasHeight(t_rows)))), 0.0));
}

int tf_GlobalIllumination_1LookupCode_40(int t_rows, int t_address) {
    return tf_GlobalIllumination_1DecodeLookupCode(tf_GlobalIllumination_1LookupData_40(t_rows, t_address));
}

vec4 tf_GlobalIllumination_1ProbeMetadata_40(int t_probe, int t_rows) {
    vec2 t_pixel = ((vec2(float((t_probe % tide_i(32))), float((t_probe / tide_i(32)))) * vec2(18.0, 19.0)) + vec2(0.5, 18.5));
    return tide_straight(textureLod(pixels0, (t_pixel / vec2(576.0, float(tf_GlobalIllumination_1AtlasHeight(t_rows)))), 0.0));
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

vec2 tf_GlobalIllumination_1ReceiverVisibility_40(int t_probe, int t_rows, vec3 t_delta, vec3 t_normal, float t_spacing, float t_tolerance, float t_metricSpacing) {
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
        vec4 t_moments = tide_straight(textureLod(pixels0, ((t_center + t_offset) / t_size), 0.0));
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

vec3 tf_Render3D_1ClampHdr(vec3 t_radiance) {
    return tide_clamp3(t_radiance, vec3(tide_f(0.0)), vec3(tide_f(65504.0)));
}

tt_GlobalIllumination_1IrradianceBlend tf_GlobalIllumination_1GatherSparseIrradiance_41x0(tt_GlobalIllumination_1FieldLayout t_layout, int t_initialized, vec3 t_world, vec3 t_n, vec3 t_visibilityNormal, vec3 t_grid, float t_tolerance, int t_baseProbe, float t_metricSpacing, int t_coordinateStride) {
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
        int t_probe = (tf_GlobalIllumination_1LookupCode_40(t_layout.m_atlasRows, (t_layout.m_nodeCount + t_logical)) - 1);
        if ((((t_probe < 0) || (t_probe >= t_layout.m_total)) || (t_initialized <= 0))) {
            continue;
        }
        vec3 t_axis = vec3(((t_x == 0) ? (1.0 - t_fraction.x) : t_fraction.x), ((t_y == 0) ? (1.0 - t_fraction.y) : t_fraction.y), ((t_z == 0) ? (1.0 - t_fraction.z) : t_fraction.z));
        float t_spatial = ((t_axis.x * t_axis.y) * t_axis.z);
        if ((t_spatial <= 0.0)) {
            continue;
        }
        vec4 t_metadata = tf_GlobalIllumination_1ProbeMetadata_40(t_probe, t_layout.m_atlasRows);
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
        vec2 t_visibility = tf_GlobalIllumination_1ReceiverVisibility_40(t_probe, t_layout.m_atlasRows, t_delta, t_visibilityNormal, t_layout.m_spacing, t_tolerance, t_metric);
        if ((t_visibility.y >= 0.25)) {
            continue;
        }
        float t_weight = ((t_baseWeight * t_frontSupport) * t_visibility.x);
        vec4 t_sampled = tide_straight(textureLod(pixels1, tf_GlobalIllumination_1AtlasUV(t_probe, t_layout.m_atlasRows, t_n), 0.0));
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

void ts_ReadGISharedFacing(tt_Vertex t_vertex, inout vec4 t_color) {
    bool t_coarse = (tide_u2.yzw.x > 0.0);
    tt_GlobalIllumination_1FieldLayout t_field = tt_GlobalIllumination_1FieldLayout((t_coarse ? vec3(float((-8)), 0.0, 0.0) : vec3(0.0)), float((t_coarse ? 4 : 2)), ivec3(3), tt_GlobalIllumination_1FieldLayout(tide_u0.xyz, tide_u0.w, tide_v0.xyz, tide_v0.w, tide_v1.x, tide_u1.x, (tide_v1.y != 0), tide_u1.y, tide_v1.z, tide_v1.w).m_total, tt_GlobalIllumination_1FieldLayout(tide_u0.xyz, tide_u0.w, tide_v0.xyz, tide_v0.w, tide_v1.x, tide_u1.x, (tide_v1.y != 0), tide_u1.y, tide_v1.z, tide_v1.w).m_atlasRows, 0.0, true, 0.0, tt_GlobalIllumination_1FieldLayout(tide_u0.xyz, tide_u0.w, tide_v0.xyz, tide_v0.w, tide_v1.x, tide_u1.x, (tide_v1.y != 0), tide_u1.y, tide_v1.z, tide_v1.w).m_nodeCount, 0);
    vec3 t_grid = tide_clamp3(((vec3(tide_u1.z, tide_u1.w, tide_u2.x) - t_field.m_minimum) / t_field.m_spacing), vec3(tide_f(0.0)), vec3(tide_f(2.0)));
    t_grid.x = float((t_coarse ? 2 : 0));
    tt_GlobalIllumination_1IrradianceBlend t_blend = tf_GlobalIllumination_1GatherSparseIrradiance_41x0(t_field, tide_v2.x, vec3(tide_u1.z, tide_u1.w, tide_u2.x), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), t_grid, 0.01, (t_coarse ? 0 : 28), tt_GlobalIllumination_1FieldLayout(tide_u0.xyz, tide_u0.w, tide_v0.xyz, tide_v0.w, tide_v1.x, tide_u1.x, (tide_v1.y != 0), tide_u1.y, tide_v1.z, tide_v1.w).m_spacing, 1);
    t_color = vec4((tf_GlobalIllumination_1ResolveIrradiance(t_blend) * (tide_f(8.0) / tide_f(3.14159265))), 1.0);
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    ts_ReadGISharedFacing(tt_Vertex(vec2(0.0), vec2(0.0), vec4(0.0)), tide_color);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_pixel = tide_color;
}
