static const char *node_name(const editor_node *n)
{
    const char *name = n->path.len ? str_to_cstr(n->path) : "Root";
    const char *last = strrchr(name,'.'); return last ? last+1 : name;
}

static void tree(editor *e, tide_rect r)
{
    const bool blocked=e->dialog_open || e->picker || e->value_picker || e->discard || e->recovery;
    if(blocked) { e->drag_node=-1; e->tree_dragging=false; }
    label(e,r.x+14,r.y+12,"HIERARCHY",dim);
    if (e->doc.prefabs.count) {
        label(e,r.x+14,r.y+43,str_to_cstr(e->doc.prefabs.items[e->doc.active]->name),ink);
        if (e->doc.prefabs.count > 1 && button(e,80,(tide_rect){r.x+r.width-74,r.y+38,60,26},"Next")) {
            queue(e,PREFAB); e->argument = (e->doc.active+1)%e->doc.prefabs.count;
        }
    }
    const tide_rect content = {r.x+8,r.y+76,r.width-16,r.height-256};
    if (contains(content,e->gui.mouse)) e->tree_scroll -= scroll_rows(e->devices.mouse.poll_scroll.y,3);
    if (e->tree_scroll < 0) e->tree_scroll = 0;
    int row = 0, hovered = -1;
    const int visible = (int)(content.height/30);
    for (int i = 0; i < e->doc.nodes.count; i++) {
        editor_node *n = &e->doc.nodes.items[i]; int depth = 0; bool hidden = false;
        for (int p = n->parent; p >= 0 && depth < 128; p = e->doc.nodes.items[p].parent) { depth++; if (p < 1024 && e->folded[p]) hidden = true; }
        if (hidden) continue;
        const int number = row++;
        if (number < e->tree_scroll || number >= e->tree_scroll+visible) continue;
        const float indent = (float)(depth < 6 ? depth : 6)*12;
        tide_rect box = {content.x+indent,content.y+(float)(number-e->tree_scroll)*30,content.width-indent,26};
        bool children = false;
        for (int j = i+1; j < e->doc.nodes.count; j++) if (e->doc.nodes.items[j].parent == i) { children = true; break; }
        if (children && i < 1024 && button(e,22000u+(uint32_t)i,(tide_rect){box.x,box.y,22,26},e->folded[i] ? ">" : "v")) e->folded[i] = !e->folded[i];
        box.x += 24; box.width -= 24;
        const bool inherited = editor_node_inherited(&e->doc,i);
        sb text = {0}; sb_printf(&text,"%s%s",node_name(n),inherited ? "  [prefab]" : n->authored && n->authored->base_decl ? "  [instance]" : "");
        const int theme = tide_gui_begin_theme(&e->gui,(tide_gui_theme){.text=ink,.control=i==e->selected ? rgb(.14f,.32f,.38f) : rgb(.11f,.14f,.17f),.hot=rgb(.19f,.26f,.3f),.pressed=rgb(.14f,.32f,.38f),.accent=rgb(.3f,.83f,.93f),.radius=3});
        if (button(e,2000u+(uint32_t)i,box,text.data) && !e->tree_dragging) { e->selected=i; e->inspector_scroll=0; clear_field(e); }
        tide_gui_close(&e->gui,theme);
        if (contains(box,e->gui.mouse)) {
            hovered=i;
            if (e->gui.mouse_pressed && !blocked) { e->drag_node=i; e->drag_origin=e->gui.mouse; }
            if (e->tree_dragging) tide_draw_thick_line(&e->draw,tide_f2(box.x,box.y+26),tide_f2(box.x+box.width,box.y+26),2,rgb(.3f,.83f,.93f));
        }
    }
    const int maximum = row > visible ? row-visible : 0;
    if (e->tree_scroll > maximum) e->tree_scroll = maximum;
    if (e->gui.mouse_held && e->drag_node > 0 && editor_can_structure(&e->doc,e->drag_node) && tide_length_f2(tide_sub_f2(e->gui.mouse,e->drag_origin)) > 6) e->tree_dragging=true;
    if (e->gui.mouse_released || !e->gui.mouse_held) {
        if (e->tree_dragging && hovered >= 0 && hovered != e->drag_node) { e->selected=e->drag_node; e->argument=hovered; queue(e,REPARENT_NODE); }
        e->tree_dragging=false; e->drag_node=-1;
    }
    const bool editable = editor_can_structure(&e->doc,e->selected);
    const bool child = editable && e->selected > 0;
    int disabled = tide_gui_begin_disabled(&e->gui,!editable);
    if (button(e,81,(tide_rect){r.x+14,r.y+r.height-164,100,26},"+ Entity")) open_dialog(e,DIALOG_CHILD,"Object",NULL);
    if (button(e,82,(tide_rect){r.x+124,r.y+r.height-164,112,26},"+ Prefab")) { e->picker=true; e->nested_picker=true; e->argument=0; replace(&e->search,""); }
    tide_gui_close(&e->gui,disabled);
    disabled = tide_gui_begin_disabled(&e->gui,!child);
    if (button(e,84,(tide_rect){r.x+14,r.y+r.height-132,100,26},"Rename")) open_dialog(e,DIALOG_RENAME,node_name(&e->doc.nodes.items[e->selected]),NULL);
    if (button(e,85,(tide_rect){r.x+124,r.y+r.height-132,112,26},"Duplicate")) {
        sb name={0}; sb_printf(&name,"%sCopy",node_name(&e->doc.nodes.items[e->selected])); open_dialog(e,DIALOG_DUPLICATE,name.data,NULL);
    }
    if (button(e,86,(tide_rect){r.x+14,r.y+r.height-100,222,26},"Save as Prefab...")) open_dialog(e,DIALOG_EXTRACT,node_name(&e->doc.nodes.items[e->selected]),NULL);
    if (button(e,83,(tide_rect){r.x+14,r.y+r.height-68,222,26},"Delete")) queue(e,DELETE_NODE);
    tide_gui_close(&e->gui,disabled);
    const editor_node *selected=e->selected>=0 && e->selected<e->doc.nodes.count ? &e->doc.nodes.items[e->selected] : NULL;
    const decl *base=selected && selected->authored ? selected->authored->base_decl : NULL;
    const char *source=base && base->unit ? base->unit->src->path : editor_node_source(&e->doc,e->selected);
    if(!base && selected && selected->effective) for(int i=0;i<e->doc.prog->decls.count;i++) {
        const decl *p=e->doc.prog->decls.items[i];
        if(p->kind==DECL_PREFAB && p->at.file==selected->effective->at.file && p->at.line<=selected->effective->at.line && p->end.line>=selected->effective->at.line) { base=p; break; }
    }
    disabled = tide_gui_begin_disabled(&e->gui,!source);
    if (button(e,87,(tide_rect){r.x+14,r.y+r.height-36,222,26},"Open source prefab")) { queue_name(e,OPEN_FILE,base ? base->qualified : e->doc.prefabs.items[e->doc.active]->qualified); replace(&e->pending_path,source); }
    tide_gui_close(&e->gui,disabled);
}
