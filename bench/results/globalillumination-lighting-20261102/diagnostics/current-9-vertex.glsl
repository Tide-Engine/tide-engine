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
layout(location = 0) in vec3 tide_a0;
layout(location = 1) in vec3 tide_a1;
layout(location = 2) in vec2 tide_a2;
layout(location = 3) in vec4 tide_a3;
layout(location = 4) in vec4 tide_a4;
layout(location = 5) in vec4 tide_n0;
layout(location = 6) in vec4 tide_n1;
layout(location = 7) in vec4 tide_n2;
layout(location = 8) in vec4 tide_n3;
layout(location = 9) in vec4 tide_n4;
layout(location = 10) in vec4 tide_n5;
layout(location = 11) in vec4 tide_n6;
layout(location = 12) in vec4 tide_n7;
layout(location = 13) in vec4 tide_n8;
layout(location = 14) in ivec4 tide_j0;
layout(location = 15) in ivec4 tide_j1;
out vec3 tide_s1;
out vec2 tide_s2;
out vec4 tide_s3;
out vec4 tide_s4;
out vec3 tide_world;
flat out vec3 tide_normals0;
flat out vec3 tide_normals1;
flat out vec3 tide_normals2;
flat out vec4 tide_f0;
flat out vec4 tide_f1;
flat out vec4 tide_f2;
flat out vec4 tide_f3;
flat out vec4 tide_f4;
flat out ivec4 tide_g0;
flat out ivec4 tide_g1;
struct tt_Render3D_1MeshVertex {
    vec3 m_position;
    vec3 m_normal;
    vec2 m_uv;
    vec4 m_color;
    vec4 m_tangent;
};
struct tt_TideGenerated_1Callable7c82de5a {
    int m_kind;
};
struct tt_Render3D_1VertexInput {
    vec3 m_world;
    vec3 m_eye;
    mat4 m_localToWorld;
};
float tide_f(float x) {
    return x;
}
vec3 tide_normalize3(vec3 v) {
    float l = dot(v, v);
    if (l > 1.17549435e-38) { return v * inversesqrt(l); }
    return vec3(0.0);
}
mat3 tide_normals(mat4 m) {
    vec3 a = cross(m[1].xyz, m[2].xyz);
    vec3 b = cross(m[2].xyz, m[0].xyz);
    vec3 c = cross(m[0].xyz, m[1].xyz);
    if (dot(m[0].xyz, a) < 0.0) {
        return mat3(-a, -b, -c);
    }
    return mat3(a, b, c);
}
void tf_Render3D_1StandardVertex(tt_Render3D_1VertexInput t_input, inout tt_Render3D_1MeshVertex t_vertex) {
}

void tf_TideGenerated_1Invoke7c82de5a(tt_TideGenerated_1Callable7c82de5a t_binding, tt_Render3D_1VertexInput t_arg0, inout tt_Render3D_1MeshVertex t_arg1) {
    switch (t_binding.m_kind) {
        case 316728690: {
            tf_Render3D_1StandardVertex(t_arg0, t_arg1);
            break;
        }
        default: {
        } break;
    }
    return;
}

tt_Render3D_1VertexInput tf_Render3D_1VertexContext(vec3 t_position, mat4 t_transform, vec3 t_eye) {
    return tt_Render3D_1VertexInput((t_transform * vec4(t_position, tide_f(1.0))).xyz, t_eye, t_transform);
}

void ts_Render3D_1Deform(inout tt_Render3D_1MeshVertex t_corner) {
    tf_TideGenerated_1Invoke7c82de5a(tt_TideGenerated_1Callable7c82de5a(tide_v0.x), tf_Render3D_1VertexContext(t_corner.m_position, mat4(tide_n0, tide_n1, tide_n2, tide_n3), tide_u0.xyz), t_corner);
}

ivec4 tide_skin_words0(int p) {
    return texelFetch(pixels10, ivec2(p & 1023, p >> 10), 0);
}
ivec4 tide_skin_words1(int p) {
    return texelFetch(pixels11, ivec2(p & 1023, p >> 10), 0);
}
mat4 tide_skin_matrix(int i) {
    if (i < 0 || i >= tide_v5.y) return mat4(1.0);
    int p = (tide_j1.y + i) * 4;
    return mat4(intBitsToFloat(tide_skin_words0(p + 0)), intBitsToFloat(tide_skin_words0(p + 1)), intBitsToFloat(tide_skin_words0(p + 2)), intBitsToFloat(tide_skin_words0(p + 3)));
}
mat4 tide_skinning(int vertex) {
    if (vertex < 0 || vertex >= tide_v5.z) return mat4(1.0);
    ivec2 range = tide_skin_words1(vertex).xy;
    int entries = tide_v5.w;
    if (range.x < 0 || range.y < 0 || range.x > entries || range.y > entries - range.x) return mat4(1.0);
    vec4 c0 = vec4(0.0);
    vec4 c1 = vec4(0.0);
    vec4 c2 = vec4(0.0);
    vec4 c3 = vec4(0.0);
    float total = 0.0;
    for (int n = 0; n < range.y; n++) {
        int i = range.x + n;
        ivec4 pair = tide_skin_words1(tide_v5.z + (i >> 1));
        int joint = (i & 1) == 0 ? pair.x : pair.z;
        float weight = intBitsToFloat((i & 1) == 0 ? pair.y : pair.w);
        mat4 matrix = tide_skin_matrix(joint);
        c0 = c0 + matrix[0] * weight;
        c1 = c1 + matrix[1] * weight;
        c2 = c2 + matrix[2] * weight;
        c3 = c3 + matrix[3] * weight;
        total = total + weight;
    }
    if (!(total > 0.0)) return mat4(1.0);
    return mat4(c0 / total, c1 / total, c2 / total, c3 / total);
}
void main() {
    tt_Render3D_1MeshVertex tide_vertex = tt_Render3D_1MeshVertex(tide_a0, tide_a1, tide_a2, tide_a3, tide_a4);
    mat4 tide_skin = tide_skinning(gl_VertexID);
    tide_vertex.m_position = (tide_skin * vec4(tide_vertex.m_position, 1.0)).xyz;
    tide_vertex.m_normal = tide_normalize3(tide_normals(tide_skin) * tide_vertex.m_normal);
    vec3 tide_tangent = (tide_skin * vec4(tide_vertex.m_tangent.xyz, 0.0)).xyz;
    tide_tangent = tide_tangent - tide_vertex.m_normal * dot(tide_vertex.m_normal, tide_tangent);
    float tide_side = dot(tide_skin[0].xyz, cross(tide_skin[1].xyz, tide_skin[2].xyz)) < 0.0 ? -1.0 : 1.0;
    tide_vertex.m_tangent = vec4(tide_normalize3(tide_tangent), tide_vertex.m_tangent.w * tide_side);
    ts_Render3D_1Deform(tide_vertex);
    vec4 tide_place = mat4(tide_n0, tide_n1, tide_n2, tide_n3) * vec4(tide_vertex.m_position, 1.0);
    gl_Position = tide_camera * tide_place;
    tide_s1 = tide_vertex.m_normal;
    tide_s2 = tide_vertex.m_uv;
    tide_s3 = tide_vertex.m_color;
    tide_s4 = tide_vertex.m_tangent;
    tide_world = tide_place.xyz;
    mat3 tide_turned = tide_normals(mat4(tide_n0, tide_n1, tide_n2, tide_n3));
    tide_normals0 = tide_turned[0];
    tide_normals1 = tide_turned[1];
    tide_normals2 = tide_turned[2];
    tide_f0 = vec4(tide_n4.x, tide_n4.y, tide_n4.z, tide_n4.w);
    tide_f1 = vec4(tide_n5.x, tide_n5.y, tide_n5.z, tide_n5.w);
    tide_f2 = vec4(tide_n6.x, tide_n6.y, tide_n6.z, tide_n6.w);
    tide_f3 = vec4(tide_n7.x, tide_n7.y, tide_n7.z, tide_n7.w);
    tide_f4 = vec4(tide_n8.x, 0.0, 0.0, 0.0);
    tide_g0 = ivec4(tide_j0.x, tide_j0.y, tide_j0.z, tide_j0.w);
    tide_g1 = ivec4(tide_j1.x, 0, 0, 0);
}
