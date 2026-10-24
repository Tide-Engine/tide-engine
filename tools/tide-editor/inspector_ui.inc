// Direct controls keep compiler values read-only until an explicit UI edit.
// Drafts own their identity and bytes; neither widget IDs nor AST pointers own edits.
static void inspector_free(inspector_edit *edit)
{
    if (!edit) return;
    free(edit->file); free(edit->prefab); free(edit->path);
    free(edit->component); free(edit->field); free(edit->initial); free(edit->draft); free(edit);
}

static void inspector_cancel(editor *e)
{
    if (!e->edit) return;
    if (e->edit->kind == INSPECTOR_EXPRESSION && e->component && e->field &&
        !strcmp(e->component,e->edit->component) && !strcmp(e->field,e->edit->field)) replace(&e->value,e->edit->initial);
    if (e->gui.editing == e->edit->id) e->gui.editing = 0;
    if (e->gui.edit_left == e->edit->id) e->gui.edit_left = 0;
    e->text_selection = 0;
    inspector_free(e->edit); e->edit = NULL;
}

static bool inspector_owner(const editor *e, const inspector_edit *edit)
{
    return e->doc.path && !strcmp(e->doc.path, edit->file) && e->doc.active >= 0 &&
        e->doc.active < e->doc.prefabs.count && str_eq_c(e->doc.prefabs.items[e->doc.active]->qualified, edit->prefab);
}

static inspector_edit *inspector_target(editor *e, decl *component, field *f)
{
    inspector_edit *edit = calloc(1, sizeof *edit); if (!edit) abort();
    edit->file = own(e->doc.path);
    edit->prefab = own_slice(e->doc.prefabs.items[e->doc.active]->qualified);
    edit->path = own_slice(e->doc.nodes.items[e->selected].path);
    edit->component = own_slice(component->qualified); edit->field = own_slice(f->name);
    edit->revision = e->doc.revision;
    return edit;
}

static void inspector_enqueue(editor *e, inspector_edit *edit)
{
    if (e->edit_last) e->edit_last->next = edit;
    else e->edit_first = edit;
    e->edit_last = edit;
}

static void inspector_finish(editor *e)
{
    inspector_edit *edit = e->edit;
    if (!edit) return;
    if (edit->revision != e->doc.revision || !inspector_owner(e, edit)) { inspector_cancel(e); return; }
    bool changed = false;
    sb value = {0};
    if (edit->kind == INSPECTOR_STRING) {
        changed = strcmp(edit->initial, edit->draft) != 0;
        sb_put(&value, "\"");
        for (const char *p = edit->draft; *p; p++) {
            if (*p == '\\' || *p == '\"') { sb_put(&value, "\\"); sb_putn(&value, p, 1); }
            else if (*p == '\n') sb_put(&value, "\\n");
            else if (*p == '\r') sb_put(&value, "\\r");
            else if (*p == '\t') sb_put(&value, "\\t");
            else sb_putn(&value, p, 1);
        }
        sb_put(&value, "\"");
    } else if (edit->kind == INSPECTOR_EXPRESSION) {
        changed = strcmp(edit->initial, edit->draft) != 0;
        sb_put(&value, edit->draft);
    } else {
        char *end;
        if (edit->kind == INSPECTOR_INT) {
            const long long parsed = strtoll(edit->draft, &end, 10);
            const int32_t number = parsed < INT32_MIN ? INT32_MIN : parsed > INT32_MAX ? INT32_MAX : (int32_t)parsed;
            changed = end != edit->draft && !*end && number != edit->integer;
            sb_printf(&value, "%d", number);
        } else {
            const float number = strtof(edit->draft, &end);
            changed = end != edit->draft && !*end && isfinite(number) && number != edit->numbers[edit->part];
            edit->numbers[edit->part] = number;
            if (edit->kind == INSPECTOR_FLOAT) sb_printf(&value, "%.9g", (double)number);
            else {
                if (edit->kind == INSPECTOR_EULER) sb_put(&value, "quaternion.Euler(float3(");
                else sb_printf(&value, "float%d(", edit->count);
                for (int i = 0; i < edit->count; i++) sb_printf(&value, "%s%.9g%s", i ? ", " : "", (double)edit->numbers[i], edit->kind == INSPECTOR_EULER ? "deg" : "");
                sb_put(&value, edit->kind == INSPECTOR_EULER ? "))" : ")");
            }
        }
    }
    if (!changed) { inspector_cancel(e); return; }
    replace(&edit->draft, value.data);
    e->edit = NULL;
    if (e->gui.editing == edit->id) e->gui.editing = 0;
    if (e->gui.edit_left == edit->id) e->gui.edit_left = 0;
    e->text_selection = 0;
    inspector_enqueue(e, edit);
}

// Runs before any other control can take the GUI's shared buffer or select a node.
static void inspector_prepare(editor *e)
{
    if (!e->edit) return;
    if (e->devices.keyboard.escape.down || e->edit->revision != e->doc.revision || !inspector_owner(e, e->edit)) {
        inspector_cancel(e); return;
    }
    const bool ctrl = e->devices.keyboard.leftCtrl.pressed || e->devices.keyboard.rightCtrl.pressed;
    const bool pointer_left = e->gui.mouse_pressed && !contains(e->edit->rect, e->gui.mouse);
    const bool focus_left = !e->gui.mouse_pressed && (e->gui.editing != e->edit->id || e->gui.focus != e->edit->id);
    if (pointer_left || focus_left || e->devices.mouse.poll_scroll.y ||
        (ctrl && e->devices.keyboard.s.down) || e->devices.keyboard.f5.down) inspector_finish(e);
}

static void inspector_track(editor *e, decl *component, field *f, uint32_t id, tide_rect r,
                            inspector_kind kind, const char *initial, const float *numbers, int count, int part, int32_t integer)
{
    if (e->gui.editing != id) return;
    if (!e->edit) {
        e->edit = inspector_target(e, component, f);
        e->edit->id = id; e->edit->kind = kind; e->edit->initial = own(initial ? initial : "");
        e->edit->integer = integer; e->edit->count = count; e->edit->part = part;
        if (numbers) memcpy(e->edit->numbers, numbers, (size_t)count * sizeof *numbers);
    }
    e->edit->rect = r;
    replace(&e->edit->draft, e->gui.edit);
}

static bool inspector_apply(editor *e)
{
    const uint64_t revision = e->doc.revision;
    const bool queued = e->edit_first != NULL;
    while (e->edit_first) {
        inspector_edit *edit = e->edit_first; e->edit_first = edit->next;
        if (edit->revision == revision && inspector_owner(e, edit)) {
            for (int i = 0; i < e->doc.nodes.count; i++) if (str_eq_c(e->doc.nodes.items[i].path, edit->path)) {
                const bool applied = editor_set_field(&e->doc, i, str_from(edit->component), str_from(edit->field), edit->draft);
                // Moving between axes commits one field at a time. The new axis
                // must carry forward the axis just committed after this draw.
                if (applied && e->edit && e->edit->kind == edit->kind &&
                    (edit->kind == INSPECTOR_VECTOR || edit->kind == INSPECTOR_EULER) &&
                    !strcmp(e->edit->path, edit->path) && !strcmp(e->edit->component, edit->component) && !strcmp(e->edit->field, edit->field)) {
                    for (int p = 0; p < edit->count; p++) if (p != e->edit->part) e->edit->numbers[p] = edit->numbers[p];
                }
                break;
            }
        }
        inspector_free(edit);
    }
    e->edit_last = NULL;
    // A different field may have acquired focus in the same frame as this blur.
    if (e->edit && e->edit->revision == revision && inspector_owner(e, e->edit)) e->edit->revision = e->doc.revision;
    if (queued && e->value_selected && e->component && e->selected >= 0 && e->selected < e->doc.nodes.count &&
        (!e->edit || e->edit->kind != INSPECTOR_EXPRESSION)) {
        expr *comp = editor_component(&e->doc.nodes.items[e->selected], str_from(e->component));
        for (int f = 0; comp && comp->type_decl && f < comp->type_decl->fields.count; f++) {
            field *item = &comp->type_decl->fields.items[f];
            if (str_eq_c(item->name, e->field)) { replace(&e->value, field_text(e, comp, item)); break; }
        }
    }
    return queued;
}

static void select_field(editor *e, decl *component, field *f, const char *value)
{
    char *copy = own(value);
    replace(&e->component, str_to_cstr(component->qualified));
    replace(&e->field, str_to_cstr(f->name));
    free(e->value); e->value = copy; e->value_selected = false;
}

static void field_changed(editor *e, decl *component, field *f, const char *value)
{
    if(e->pending==SAVE || e->pending==PLAY || e->pending==SET_STARTUP) { e->following=e->pending; e->pending=NONE; }
    if (e->pending != NONE) return;
    expr *comp = editor_component(&e->doc.nodes.items[e->selected], component->qualified);
    if (comp && !strcmp(field_text(e, comp, f), value)) return;
    inspector_edit *edit = inspector_target(e, component, f); edit->draft = own(value); inspector_enqueue(e, edit);
    select_field(e, component, f, value); queue(e, SET);
}

static bool integer_value(const expr *v, int32_t *out, int depth)
{
    if (!v || v->kind == E_DEFAULT) { *out = 0; return true; }
    if (depth > 64) return false;
    if (v->bind == BIND_CONST) return integer_value(v->constant->value, out, depth + 1);
    int64_t n;
    if (v->kind == E_INT) n = v->int_value;
    else if (v->kind == E_UNARY && v->op == T_MINUS && v->lhs && v->lhs->kind == E_INT && v->lhs->int_value!=INT64_MIN) n = -v->lhs->int_value;
    else return false;
    if (n < INT32_MIN || n > INT32_MAX) return false;
    *out = (int32_t)n; return true;
}

static bool bool_value(const expr *v, bool *out, int depth)
{
    if (!v || v->kind == E_DEFAULT) { *out = false; return true; }
    if (depth > 64) return false;
    if (v->bind == BIND_CONST) return bool_value(v->constant->value, out, depth + 1);
    if (v->kind != E_BOOL) return false;
    *out = v->bool_value; return true;
}

static field *selected_field(editor *e)
{
    if (!e->component || e->selected < 0 || e->selected >= e->doc.nodes.count) return NULL;
    expr *comp = editor_component(&e->doc.nodes.items[e->selected], str_from(e->component));
    if (comp && comp->type_decl) for (int i = 0; i < comp->type_decl->fields.count; i++)
        if (str_eq_c(comp->type_decl->fields.items[i].name, e->field)) return &comp->type_decl->fields.items[i];
    return NULL;
}

static bool inspector_float(editor *e, decl *component, field *f, uint32_t id, tide_rect r,
                            float *values, int count, int part, inspector_kind kind)
{
    const uint32_t typing_id = tide_gui_seed(id, 1);
    const bool was_typing = e->edit && e->edit->id == typing_id;
    const bool changed = tide_gui_float_field(&e->gui, id, r, "", &values[part]);
    inspector_track(e, component, f, typing_id, r, kind, NULL, values, count, part, 0);
    if (was_typing && e->gui.editing != typing_id) {
        replace(&e->edit->draft, e->gui.edit); inspector_finish(e); return false;
    }
    return changed;
}

static bool value_controls(editor *e, decl *component, field *f, expr *v, uint32_t id, tide_rect r)
{
    char text[256];
    if(f->type.kind==TY_STRING && (!v || v->kind==E_DEFAULT || (v->kind==E_STRING && (size_t)v->text.len<sizeof e->gui.edit && !memchr(v->text.ptr,'\\',(size_t)v->text.len)))) {
        const bool was_typing=e->edit && e->edit->id==id;
        const char *initial=v && v->kind==E_STRING ? str_to_cstr(v->text) : "";
        char *draft=own(was_typing ? e->edit->draft : initial);
        bool entered=false;
        text_field(e,id,r,"",&draft,&entered);
        inspector_track(e,component,f,id,r,INSPECTOR_STRING,initial,NULL,0,0,0);
        if (was_typing) {
            replace(&e->edit->draft,draft);
            if(entered || e->gui.editing!=id) inspector_finish(e);
        }
        free(draft);
        return true;
    }
    if (f->type.kind == TY_BOOL) {
        bool value;
        if (!bool_value(v, &value, 0)) return false;
        if (tide_gui_toggle(&e->gui, id, r, value ? "True" : "False", &value)) field_changed(e, component, f, value ? "true" : "false");
        return true;
    }
    if (f->type.kind == TY_INT || f->type.kind == TY_BYTE) {
        int32_t value;
        if (!integer_value(v, &value, 0)) return false;
        const bool was_typing=e->edit && e->edit->id==id;
        const bool changed=tide_gui_int_field(&e->gui, id, r, "", &value);
        inspector_track(e,component,f,id,r,INSPECTOR_INT,NULL,NULL,0,0,value);
        if (was_typing && e->gui.editing!=id) { replace(&e->edit->draft,e->gui.edit); inspector_finish(e); }
        else if (changed) {
            snprintf(text, sizeof text, "%d", value); field_changed(e, component, f, text);
        }
        return true;
    }
    if (f->type.kind == TY_FLOAT) {
        float value = 0;
        if (v && v->kind != E_DEFAULT && !scalar(v, &value, 0)) return false;
        if (inspector_float(e,component,f,id,r,&value,1,0,INSPECTOR_FLOAT)) {
            snprintf(text, sizeof text, "%.9g", (double)value); field_changed(e, component, f, text);
        }
        return true;
    }
    const int count = f->type.kind == TY_FLOAT2 ? 2 : f->type.kind == TY_FLOAT3 ? 3 : f->type.kind == TY_FLOAT4 ? 4 : 0;
    if (count) {
        float values[4] = {0};
        if (v && !vector(v, values, count, 0)) return false;
        bool changed = false;
        const float width = (r.width - (float)(count - 1) * 6) / (float)count;
        for (int i = 0; i < count; i++) {
            const tide_rect box = {r.x + (float)i * (width + 6), r.y, width, r.height};
            if (inspector_float(e,component,f,tide_gui_seed(id,(uint32_t)i+1),box,values,count,i,INSPECTOR_VECTOR)) changed = true;
        }
        if (changed) {
            sb b = {0}; sb_printf(&b, "float%d(", count);
            for (int i = 0; i < count; i++) sb_printf(&b, "%s%.9g", i ? ", " : "", (double)values[i]);
            sb_put(&b, ")"); field_changed(e, component, f, b.data);
        }
        return true;
    }
    if (f->type.kind == TY_QUATERNION) {
        float q[4] = {0,0,0,1};
        if (v && !vector(v, q, 4, 0)) return false;
        const tide_quaternion rotation = tide_normalize_q(tide_q(q[0],q[1],q[2],q[3]));
        const tide_float3 x = tide_rotate_q(rotation, tide_f3(1,0,0));
        const tide_float3 y = tide_rotate_q(rotation, tide_f3(0,1,0));
        const tide_float3 z = tide_rotate_q(rotation, tide_f3(0,0,1));
        float angles[3] = {asinf(tide_clamp_f(-z.y,-1,1)), atan2f(z.x,z.z), atan2f(x.y,y.y)};
        if (fabsf(z.y) > .99999f) { angles[1] = atan2f(-x.z,x.x); angles[2] = 0; }
        for (int i = 0; i < 3; i++) angles[i] *= 57.2957795131f;
        bool changed = false; const float width = (r.width - 12) / 3;
        for (int i = 0; i < 3; i++) {
            if (inspector_float(e,component,f,tide_gui_seed(id,(uint32_t)i+1),(tide_rect){r.x+(float)i*(width+6),r.y,width,r.height},angles,3,i,INSPECTOR_EULER)) changed = true;
        }
        if (changed) {
            snprintf(text, sizeof text, "quaternion.Euler(float3(%.9gdeg, %.9gdeg, %.9gdeg))", (double)angles[0],(double)angles[1],(double)angles[2]);
            field_changed(e, component, f, text);
        }
        return true;
    }
    return false;
}

static void inspector(editor *e, tide_rect r)
{
    label(e, r.x+14, r.y+12, "INSPECTOR", dim);
    if (e->selected < 0 || e->selected >= e->doc.nodes.count) return;
    const editor_node *n = &e->doc.nodes.items[e->selected];
    label(e, r.x+14, r.y+44, n->path.len ? str_to_cstr(n->path) : "Root entity", ink);
    const bool inherited = editor_node_inherited(&e->doc,e->selected);
    label(e, r.x+14, r.y+70, inherited ? "Prefab child: values override this instance." : "Edit values directly. * marks an override.", dim);
    const tide_rect content = {r.x+14,r.y+106,r.width-28,r.height-170};
    if (contains(content,e->gui.mouse)) e->inspector_scroll -= scroll_rows(e->devices.mouse.poll_scroll.y, 3);
    if (e->inspector_scroll < 0) e->inspector_scroll = 0;
    float cursor = 0;
    tide_draw_set_clip(&e->draw,content); tide_draw_set_clip(&e->gui.list,content);
    if (n->effective) for (int c = 0; c < n->effective->args.count; c++) {
        expr *comp = n->effective->args.items[c]; decl *type = comp->type_decl;
        if (!type || type->is_scene) continue;
        float y = content.y + cursor - (float)e->inspector_scroll*30; cursor += 38;
        if (y+28 >= content.y && y < content.y+content.height) {
            label(e,content.x,y+3,str_to_cstr(type->qualified),rgb(.3f,.83f,.93f));
            if (editor_can_structure(&e->doc,e->selected) && button(e,3000u+(uint32_t)c,(tide_rect){content.x+content.width-64,y,64,24},"Remove")) queue_name(e,REMOVE,type->qualified);
        }
        for (int f = 0; f < type->fields.count; f++) {
            field *item = &type->fields.items[f]; if (item->hidden || item->is_local) continue;
            const bool parts = item->type.kind == TY_FLOAT2 || item->type.kind == TY_FLOAT3 || item->type.kind == TY_FLOAT4 || item->type.kind == TY_QUATERNION;
            y = content.y+cursor-(float)e->inspector_scroll*30;
            const float height = parts ? 60 : 34;
            cursor += height;
            const bool expanded = e->component && str_eq_c(type->qualified,e->component) && str_eq_c(item->name,e->field) && e->value_selected;
            if (expanded) cursor += 64;
            if (y < content.y || y+height > content.y+content.height) continue;
            const uint32_t id = tide_gui_seed(100000u+(uint32_t)c*4096u+(uint32_t)f*8u,(uint32_t)e->selected+1);
            const bool overridden = editor_field_overridden(&e->doc,e->selected,type->qualified,item->name);
            sb title = {0}; sb_printf(&title, STR_FMT "%s%s",STR_ARG(item->name), item->type.kind == TY_QUATERNION ? " (degrees)" : "",overridden ? " *" : "");
            label(e,content.x,y+5,title.data,overridden ? ink : dim);
            const tide_rect value = parts ? (tide_rect){content.x,y+26,content.width-76,28} : (tide_rect){content.x+118,y,content.width-194,28};
            const int disabled = tide_gui_begin_disabled(&e->gui,!editor_can_edit_fields(&e->doc,e->selected));
            if (!value_controls(e,type,item,editor_value(comp,item),id,value)) {
                if (button(e,id,value,field_text(e,comp,item))) {
                    select_field(e,type,item,field_text(e,comp,item));
                    if (item->type.kind == TY_ENTITY || item->type.kind == TY_LOCAL_ENTITY || item->type.kind == TY_ENUM ||
                        (item->type.kind == TY_OPTIONAL && item->type.decl && item->type.decl->fields.count &&
                         (item->type.decl->fields.items[0].type.kind == TY_ENTITY || item->type.decl->fields.items[0].type.kind == TY_LOCAL_ENTITY))) {
                        e->value_picker = true; replace(&e->search,"");
                    } else if (selected_asset_type(e)) { e->models = true; e->source = false; }
                    else e->value_selected = true;
                }
            }
            if (button(e,id+1,(tide_rect){content.x+content.width-70,y,24,26},"=")) {
                select_field(e,type,item,field_text(e,comp,item)); e->value_selected = true;
            }
            if (overridden && button(e,id+2,(tide_rect){content.x+content.width-42,y,42,26},"Reset")) { select_field(e,type,item,field_text(e,comp,item)); queue(e,RESET); }
            if (expanded) {
                bool entered = false;
                if(strlen(e->value)<sizeof e->gui.edit) {
                    const bool was_typing=e->edit && e->edit->id==id+3;
                    char *initial=own(e->value);
                    const tide_rect expression_box={content.x,y+height,content.width,28};
                    text_field(e,id+3,expression_box,"",&e->value,&entered);
                    inspector_track(e,type,item,id+3,expression_box,INSPECTOR_EXPRESSION,initial,NULL,0,0,0);
                    free(initial);
                    if (was_typing) {
                        replace(&e->edit->draft,e->value);
                        if (entered || e->gui.editing!=id+3) inspector_finish(e);
                    }
                    if (button(e,id+4,(tide_rect){content.x,y+height+32,76,24},"Apply")) field_changed(e,type,item,e->value);
                } else label(e,content.x,y+height+5,"Long expression: open it in your code editor.",dim);
                if (button(e,id+5,(tide_rect){content.x+84,y+height+32,76,24},"Close")) e->value_selected = false;
            }
            tide_gui_close(&e->gui,disabled);
        }
        cursor += 12;
    }
    if (n->authored) for (int c = 0; c < n->authored->components.count; c++) {
        expr *raw = n->authored->components.items[c]; if (raw->type_decl) continue;
        const float y = content.y + cursor - (float)e->inspector_scroll * 30; cursor += 34;
        if (y < content.y || y + 28 > content.y + content.height) continue;
        sb title = {0}; sb_printf(&title, STR_FMT " (unresolved)", STR_ARG(raw->name));
        label(e, content.x, y + 3, title.data, rgb(.98f,.53f,.4f));
        if (editor_can_structure(&e->doc,e->selected) && button(e,200000u+(uint32_t)c,(tide_rect){content.x+content.width-64,y,64,24},"Remove")) queue_name(e,REMOVE,raw->name);
    }
    tide_draw_unset_clip(&e->draw); tide_draw_unset_clip(&e->gui.list);
    const int maximum = cursor > content.height ? (int)ceilf((cursor-content.height)/30) : 0;
    if (e->inspector_scroll > maximum) e->inspector_scroll = maximum;
    const int disabled = tide_gui_begin_disabled(&e->gui,!editor_can_structure(&e->doc,e->selected));
    if (button(e,93,(tide_rect){r.x+14,r.y+r.height-48,r.width-28,30},"+ Add component")) { e->picker = true; e->nested_picker = false; e->argument = 0; replace(&e->search,""); }
    tide_gui_close(&e->gui,disabled);
}
