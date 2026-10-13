#pragma once
#include "tide/draw.h"

// Authoring-camera coordinates. Picking deliberately uses undeformed source
// triangles, independent of game shaders, cameras and local animation state.
typedef struct editor_camera {
    tide_float2 origin;
    float zoom;
    bool xy;
} editor_camera;
tide_float2 editor_project(editor_camera camera, tide_float3 point);
bool editor_pick_mesh(const tide_mesh *mesh, tide_float4x4 matrix,
    editor_camera camera, tide_float2 pointer, double *depth);
// Screen drag along an authored local axis; false for an edge-on/invalid axis.
bool editor_axis_drag(editor_camera camera, tide_float3 axis, tide_float2 delta, float *amount);
// Angle in a projected rotation plane, rejecting edge-on/singular planes.
bool editor_plane_angle(editor_camera camera, tide_float3 a, tide_float3 b,
    tide_float2 offset, float *angle);
