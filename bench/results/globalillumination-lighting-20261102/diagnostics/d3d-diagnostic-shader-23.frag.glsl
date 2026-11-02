layout(std140) uniform uniforms {
    mat4 tide_camera;
    vec4 tide_screen;
    vec4 tide_tint;
    vec4 tide_u0;
};
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

void ts_ReadGIPhysicalVisibility(tt_Vertex t_vertex, inout vec4 t_color) {
    float t_spacing = tide_u0.xyz.x;
    vec3 t_delta = vec3(0.0, 0.0, 1.0);
    vec2 t_a = tf_GlobalIllumination_1ReceiverTapVisibility(vec4(0.0, 0.0, 0.0, 1.0), vec3((tide_f(1.0) / tide_f(3.0)), 0.0, 1.0), vec3(0.0, 0.0, float((-1))), float((-1)), 1.0, t_spacing, 0.01, t_delta, 1.0);
    vec2 t_b = tf_GlobalIllumination_1ReceiverTapVisibility(vec4(2.0, 0.0, 0.0, 1.0), vec3(0.5, 0.0, 1.0), vec3(0.0, 0.0, float((-1))), float((-1)), 1.0, t_spacing, 0.01, t_delta, 1.0);
    float t_probability = ((t_a.x + t_b.x) / (t_a.y + t_b.y));
    t_color = vec4(vec3(((t_probability * t_probability) * t_probability)), 1.0);
}

void main() {
    vec4 tide_color = vec4(1.0, 1.0, 1.0, 1.0);
    ts_ReadGIPhysicalVisibility(tt_Vertex(vec2(0.0), vec2(0.0), vec4(0.0)), tide_color);
    tide_color = tide_color * tide_tint;
    tide_color.rgb = tide_color.rgb * tide_color.a;
    if (tide_screen.w > 0.0) tide_color.rgb = tide_color.rgb + (1.0 - tide_color.a);
    tide_pixel = tide_color;
}
