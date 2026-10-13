#include "interaction.h"
#include <math.h>

tide_float2 editor_project(editor_camera camera, tide_float3 p)
{
    return tide_f2(camera.origin.x + (p.x + (camera.xy ? 0 : .6f*p.z))*camera.zoom,
        camera.origin.y + (-p.y + (camera.xy ? 0 : .35f*p.z))*camera.zoom);
}

bool editor_pick_mesh(const tide_mesh *mesh, tide_float4x4 matrix,
    editor_camera camera, tide_float2 pointer, double *depth)
{
    bool hit = false;
    if (!mesh || !mesh->vertices || !mesh->indices || mesh->index_count < 3) return false;
    for (uint32_t i = 0; i <= mesh->index_count - 3u; i += 3u) {
        tide_float3 world[3]; tide_float2 p[3]; bool valid = true;
        for (int j = 0; j < 3; j++) {
            const uint32_t index = mesh->indices[i+(uint32_t)j];
            if (index >= mesh->vertex_count) { valid = false; break; }
            world[j] = tide_transform_f4x4(matrix, mesh->vertices[index].position);
            p[j] = editor_project(camera, world[j]);
            if (!isfinite(p[j].x) || !isfinite(p[j].y) || !isfinite(world[j].z)) valid = false;
        }
        if (!valid) continue;
        const double ax = (double)p[1].x-p[0].x, ay = (double)p[1].y-p[0].y;
        const double bx = (double)p[2].x-p[0].x, by = (double)p[2].y-p[0].y;
        const double x = (double)pointer.x-p[0].x, y = (double)pointer.y-p[0].y;
        const double det = ax*by-ay*bx;
        if (!isfinite(det) || fabs(det) < 1e-8) continue;
        const double u = (x*by-y*bx)/det, v = (ax*y-ay*x)/det;
        if (!(u >= 0 && v >= 0 && u+v <= 1)) continue;
        // Same depth as the editor's matrix camera: smaller is nearer. Picking
        // both sides also permits selecting mirrored/inside-out authored meshes.
        double z[3];
        for (int j = 0; j < 3; j++) z[j] = (double)world[j].z-world[j].x-world[j].y;
        const double at = z[0]+u*(z[1]-z[0])+v*(z[2]-z[0]);
        // The editor's clip z is .5 + .0001 * depth, in OpenGL [-1, 1].
        if (isfinite(at) && at >= -15000 && at <= 5000 && at < *depth) { *depth = at; hit = true; }
    }
    return hit;
}

bool editor_axis_drag(editor_camera camera, tide_float3 axis, tide_float2 delta, float *amount)
{
    camera.origin = tide_f2(0,0);
    const tide_float2 p = editor_project(camera, axis);
    const double length = (double)p.x*p.x+(double)p.y*p.y;
    if (!isfinite(length) || length < 1e-8) return false;
    const double value = ((double)delta.x*p.x+(double)delta.y*p.y)/length;
    if (!isfinite(value) || fabs(value) > 1e30) return false;
    *amount = (float)value; return true;
}

bool editor_plane_angle(editor_camera camera, tide_float3 a, tide_float3 b,
    tide_float2 offset, float *angle)
{
    camera.origin = tide_f2(0,0);
    const tide_float2 x = editor_project(camera,a), y = editor_project(camera,b);
    const double det = (double)x.x*y.y-(double)x.y*y.x;
    const double lengths = hypot(x.x,x.y)*hypot(y.x,y.y);
    if (!isfinite(det) || lengths < 1e-8 || fabs(det) < lengths*.03) return false;
    const double u = ((double)offset.x*y.y-(double)offset.y*y.x)/det;
    const double v = ((double)x.x*offset.y-(double)x.y*offset.x)/det;
    if (!isfinite(u) || !isfinite(v) || u*u+v*v < 1e-12) return false;
    *angle = (float)atan2(v,u); return true;
}
