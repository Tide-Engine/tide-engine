// Kept beside main.c: these controls operate its existing document transaction.
static tide_float3 direction(tide_float4x4 m, tide_float3 axis)
{
    return tide_sub_f3(tide_transform_f4x4(m,axis),tide_transform_f4x4(m,tide_f3(0,0,0)));
}

static float segment_distance(tide_float2 p, tide_float2 a, tide_float2 b)
{
    const tide_float2 d = tide_sub_f2(b,a);
    const float length = tide_dot_f2(d,d);
    const float t = length > 0 ? tide_clamp_f(tide_dot_f2(tide_sub_f2(p,a),d)/length,0,1) : 0;
    return tide_length_f2(tide_sub_f2(p,tide_add_f2(a,tide_mul_f2(d,tide_f2_splat(t)))));
}

static bool gizmo(editor *e, tide_rect r, const tide_float4x4 *matrices, const bool *valid)
{
    if (e->drag.active && (e->drag.node != e->selected || e->drag.revision != e->doc.revision || e->pending != NONE ||
        e->devices.keyboard.escape.down || e->menu || e->picker || e->discard || e->recovery || e->dialog_open || e->value_picker)) {
        e->drag.active = false; return true;
    }
    if (!e->tool || e->selected < 0 || e->selected >= e->doc.nodes.count || !valid[e->selected]) return false;
    const editor_node *node = &e->doc.nodes.items[e->selected];
    tide_float3 pos, scale; tide_quaternion rot;
    if (!editor_can_edit_fields(&e->doc,e->selected) || !editor_component(node,str_from("Transform")) || !local_trs(node,&pos,&rot,&scale)) return false;
    const tide_float4x4 parent = node->parent < 0 ? tide_identity_f4x4() : matrices[node->parent];
    const tide_float3 world = tide_transform_f4x4(matrices[e->selected],tide_f3(0,0,0));
    const tide_float2 center = project(e,r,world);
    const editor_camera camera = {tide_f2(0,0),e->zoom,e->xy};
    const tide_rect content = {r.x,r.y+56,r.width,r.height-80};
    static const tide_float3 axes[] = {{1,0,0},{0,1,0},{0,0,1}};
    static const tide_color colors[] = {{.95f,.35f,.35f,1},{.4f,.87f,.4f,1},{.4f,.6f,.95f,1}};
    int hot = -1; float nearest = 9;
    tide_float3 directions[3], ring_a[3], ring_b[3];
    for (int i = 0; i < 3; i++) {
        directions[i] = direction(parent,e->tool == 1 ? axes[i] : tide_rotate_q(rot,axes[i]));
        const tide_float2 screen = editor_project(camera,directions[i]);
        const float length = tide_length_f2(screen);
        if (e->tool != 2 && (!isfinite(length) || length < 1e-4f)) continue;
        const tide_color color = e->drag.active && e->drag.axis == i ? rgb(1,.85f,.35f) : colors[i];
        if (e->tool == 2) {
            ring_a[i] = direction(parent,tide_rotate_q(rot,axes[(i+1)%3]));
            ring_b[i] = direction(parent,tide_rotate_q(rot,axes[(i+2)%3]));
            float ignored;
            if (!editor_plane_angle(camera,ring_a[i],ring_b[i],tide_sub_f2(e->gui.mouse,center),&ignored)) continue;
            const float radius = 64/fmaxf(tide_length_f2(editor_project(camera,ring_a[i])),tide_length_f2(editor_project(camera,ring_b[i])));
            tide_float2 last = tide_add_f2(center,editor_project(camera,tide_mul_f3(ring_a[i],tide_f3_splat(radius))));
            for (int s = 1; s <= 64; s++) {
                const float angle = (float)s*6.28318530718f/64;
                const tide_float3 point = tide_add_f3(tide_mul_f3(ring_a[i],tide_f3_splat(cosf(angle)*radius)),tide_mul_f3(ring_b[i],tide_f3_splat(sinf(angle)*radius)));
                const tide_float2 next = tide_add_f2(center,editor_project(camera,point));
                tide_draw_thick_line(&e->draw,last,next,2,color);
                const float distance = segment_distance(e->gui.mouse,last,next);
                if (distance < nearest) { nearest = distance; hot = i; }
                last = next;
            }
        } else {
            const tide_float2 end = tide_add_f2(center,tide_mul_f2(screen,tide_f2_splat(72/length)));
            tide_draw_thick_line(&e->draw,center,end,3,color);
            if (e->tool == 3) tide_draw_rect(&e->draw,end,tide_f2(10,10),color);
            else {
                const tide_float2 back = tide_mul_f2(screen,tide_f2_splat(10/length));
                const tide_float2 side = tide_f2(-back.y*.5f,back.x*.5f);
                tide_draw_thick_line(&e->draw,end,tide_add_f2(tide_sub_f2(end,back),side),3,color);
                tide_draw_thick_line(&e->draw,end,tide_sub_f2(tide_sub_f2(end,back),side),3,color);
            }
            const float distance = segment_distance(e->gui.mouse,center,end);
            if (distance < nearest) { nearest = distance; hot = i; }
        }
    }
    if (e->tool != 2) {
        tide_draw_rect(&e->draw,center,tide_f2(10,10),rgb(.95f,.95f,.75f));
        if (tide_length_f2(tide_sub_f2(e->gui.mouse,center)) < 10) hot = 3;
    }
    if (!contains(content,e->gui.mouse) || e->menu || e->picker || e->discard || e->recovery || e->dialog_open || e->value_picker) hot = -1;
    if (hot >= 0 && e->gui.mouse_pressed && !e->drag.active && e->pending == NONE) {
        e->drag.active = true; e->drag.node = e->selected; e->drag.axis = hot; e->drag.tool = e->tool;
        e->drag.revision = e->doc.revision; e->drag.start = e->gui.mouse; e->drag.center = center;
        e->drag.position = e->drag.current_position = pos;
        e->drag.rotation = e->drag.current_rotation = rot;
        e->drag.scale = e->drag.current_scale = scale;
        if (hot < 3) {
            e->drag.direction = directions[hot];
            if (e->tool == 2) {
                e->drag.a = ring_a[hot]; e->drag.b = ring_b[hot];
                editor_plane_angle(camera,e->drag.a,e->drag.b,tide_sub_f2(e->gui.mouse,center),&e->drag.angle);
            }
        } else if (e->tool == 1) {
            // Free movement stays in the editor's world XY plane; map it back
            // to the parent's local coordinates, rejecting a singular parent.
            const tide_float4x4 inverse = tide_inverse_f4x4(parent);
            e->drag.a = direction(inverse,axes[0]); e->drag.b = direction(inverse,axes[1]);
            if (tide_length_f3(e->drag.a) == 0 || tide_length_f3(e->drag.b) == 0) e->drag.active = false;
        }
    }
    const bool used = e->drag.active || hot >= 0;
    if (e->drag.active) {
        const tide_float2 delta = tide_sub_f2(e->gui.mouse,e->drag.start);
        const bool snap = e->devices.keyboard.leftShift.pressed || e->devices.keyboard.rightShift.pressed;
        const int axis = e->drag.axis;
        float amount = 0;
        if (e->drag.tool == 1) {
            tide_float3 change = tide_f3(0,0,0);
            if (axis < 3 && editor_axis_drag(camera,e->drag.direction,delta,&amount)) {
                if (snap) amount = roundf(amount*2)/2;
                change = tide_mul_f3(axes[axis],tide_f3_splat(amount));
            } else if (axis == 3) {
                change = tide_add_f3(tide_mul_f3(e->drag.a,tide_f3_splat(delta.x/e->zoom)),tide_mul_f3(e->drag.b,tide_f3_splat(-delta.y/e->zoom)));
                if (snap) change = tide_f3(roundf(change.x*2)/2,roundf(change.y*2)/2,roundf(change.z*2)/2);
            }
            e->drag.current_position = change.x == 0 && change.y == 0 && change.z == 0 ? e->drag.position : tide_add_f3(e->drag.position,change);
        } else if (e->drag.tool == 2) {
            float angle;
            if (editor_plane_angle(camera,e->drag.a,e->drag.b,tide_sub_f2(e->gui.mouse,e->drag.center),&angle)) {
                amount = angle-e->drag.angle;
                if (snap) amount = roundf(amount/.2617993878f)*.2617993878f;
                const float sine = sinf(amount*.5f);
                const tide_float3 spin = tide_mul_f3(axes[axis],tide_f3_splat(sine));
                e->drag.current_rotation = amount == 0 ? e->drag.rotation : tide_normalize_q(tide_mul_q(e->drag.rotation,tide_q(spin.x,spin.y,spin.z,cosf(amount*.5f))));
            }
        } else {
            float factor = 1+delta.x/72;
            if (axis < 3) {
                const tide_float2 screen = editor_project(camera,e->drag.direction);
                const float length = tide_length_f2(screen);
                if (length > 0 && editor_axis_drag(camera,e->drag.direction,delta,&amount)) factor = 1+amount*length/72;
            }
            if (snap) factor = roundf(factor*10)/10;
            tide_float3 multiplier = axis == 3 ? tide_f3_splat(factor) : tide_add_f3(tide_f3_splat(1),tide_mul_f3(axes[axis],tide_f3_splat(factor-1)));
            e->drag.current_scale = tide_mul_f3(e->drag.scale,multiplier);
        }
        if (e->gui.mouse_released || !e->gui.mouse_held) {
            char value[160]; const char *field;
            const tide_float3 v = e->drag.tool == 1 ? e->drag.current_position : e->drag.current_scale;
            bool changed;
            if (e->drag.tool == 2) {
                const tide_float4 q = e->drag.current_rotation.value;
                field = "localRotation";
                snprintf(value,sizeof value,"quaternion(%.9g, %.9g, %.9g, %.9g)",(double)q.x,(double)q.y,(double)q.z,(double)q.w);
                changed = memcmp(&e->drag.current_rotation,&e->drag.rotation,sizeof(tide_quaternion)) != 0;
            } else {
                field = e->drag.tool == 1 ? "localPosition" : "localScale";
                snprintf(value,sizeof value,"float3(%.9g, %.9g, %.9g)",(double)v.x,(double)v.y,(double)v.z);
                changed = memcmp(&v,e->drag.tool == 1 ? &e->drag.position : &e->drag.scale,sizeof v) != 0;
            }
            if (changed && !strstr(value,"nan") && !strstr(value,"inf")) {
                replace(&e->component,"Transform"); replace(&e->field,field); replace(&e->value,value); queue(e,SET);
            }
            e->drag.active = false;
        }
    }
    return used;
}
