static const char *node_name(const editor_node *n)
{
    const char *name = n->path.len ? str_to_cstr(n->path) : "Root";
    const char *last = strrchr(name,'.'); return last ? last+1 : name;
}

static void open_node_source(editor *e)
{
    const editor_node *selected=e->selected>=0 && e->selected<e->doc.nodes.count ? &e->doc.nodes.items[e->selected] : NULL;
    const decl *base=selected && selected->authored ? selected->authored->base_decl : NULL;
    const char *source=base && base->unit ? base->unit->src->path : editor_node_source(&e->doc,e->selected);
    if(!base && selected && selected->effective) for(int i=0;i<e->doc.prog->decls.count;i++) {
        const decl *p=e->doc.prog->decls.items[i];
        if(p->kind==DECL_PREFAB && p->at.file==selected->effective->at.file && p->at.line<=selected->effective->at.line && p->end.line>=selected->effective->at.line) { base=p; break; }
    }
    if(source) { queue_name(e,OPEN_FILE,base ? base->qualified : e->doc.prefabs.items[e->doc.active]->qualified); replace(&e->pending_path,source); }
}

static void tree(editor *e, tide_rect r)
{
    const bool blocked=e->menu || e->dialog_open || e->picker || e->value_picker || e->discard || e->recovery;
    if(blocked) { e->drag_node=-1; e->tree_dragging=false; }
    panel(e,(tide_rect){r.x,r.y,r.width,26},rgb(.18f,.18f,.18f));
    label(e,r.x+12,r.y+6,"Hierarchy",ink);
    if(e->doc.prefabs.count>1 && button(e,80,(tide_rect){r.x+r.width-58,r.y+2,54,22},"Next")) {
        queue(e,PREFAB); e->argument=(e->doc.active+1)%e->doc.prefabs.count;
    }
    if(button(e,81,(tide_rect){r.x+8,r.y+30,110,24},"+ Create")) e->menu=3;
    if(button(e,84,(tide_rect){r.x+126,r.y+30,r.width-134,24},"Actions ...")) e->menu=4;
    search_field(e,0x48495253u,(tide_rect){r.x+8,r.y+58,r.width-16,22},&e->hierarchy_search);
    const tide_rect content={r.x+4,r.y+80,r.width-8,r.height-80};
    if(!e->menu && contains(content,e->gui.mouse)) e->tree_scroll-=scroll_rows(e->devices.mouse.poll_scroll.y,3);
    if(e->tree_scroll<0) e->tree_scroll=0;
    const bool searching=e->hierarchy_search && e->hierarchy_search[0];
    int row=0,hovered=-1;
    const int visible=(int)(content.height/22);
    tide_draw_set_clip(&e->gui.list,content);
    for(int i=0;i<e->doc.nodes.count;i++) {
        editor_node *n=&e->doc.nodes.items[i]; int depth=0; bool hidden=false;
        for(int p=n->parent;p>=0 && depth<128;p=e->doc.nodes.items[p].parent) { depth++; if(p<1024 && e->folded[p] && !searching) hidden=true; }
        const char *name=n->path.len ? node_name(n) : str_to_cstr(e->doc.prefabs.items[e->doc.active]->name);
        if(hidden || (searching && !strstr(n->path.len ? str_to_cstr(n->path) : name,e->hierarchy_search))) continue;
        const int number=row++;
        if(number<e->tree_scroll || number>=e->tree_scroll+visible) continue;
        const float indent=(float)(depth<8 ? depth : 8)*14;
        tide_rect box={content.x,content.y+(float)(number-e->tree_scroll)*22,content.width,22};
        bool children=false;
        for(int j=i+1;j<e->doc.nodes.count;j++) if(e->doc.nodes.items[j].parent==i) { children=true; break; }
        const bool inherited=editor_node_inherited(&e->doc,i);
        const bool instance=inherited || (n->authored && n->authored->base_decl);
        const tide_control control=tide_gui_control(&e->gui,2000u+(uint32_t)i,box);
        if(i==e->selected || control.hovered) tide_draw_rect(&e->gui.list,tide_f2(box.x+box.width/2,box.y+11),tide_f2(box.width,22),i==e->selected ? rgb(.24f,.39f,.56f) : rgb(.28f,.28f,.28f));
        if(control.clicked && !e->tree_dragging) { e->selected=i; e->inspector_scroll=0; clear_field(e); }
        if(children && i<1024) {
            const tide_rect fold={box.x+indent,box.y,18,22};
            if(tide_gui_control(&e->gui,22000u+(uint32_t)i,fold).clicked) e->folded[i]=!e->folded[i];
            tide_draw_text(&e->gui.list,e->folded[i] ? ">" : "v",tide_f2(fold.x+4,fold.y+4),12,dim);
        }
        const float x=box.x+indent+22;
        const tide_color icon=instance ? rgb(.40f,.67f,.92f) : dim;
        tide_draw_rect(&e->gui.list,tide_f2(x+5,box.y+11),tide_f2(9,9),icon);
        tide_draw_text(&e->gui.list,name,tide_f2(x+16,box.y+4),14,instance ? rgb(.62f,.78f,.94f) : ink);
        if(contains(box,e->gui.mouse)) {
            hovered=i;
            if(e->gui.mouse_pressed && !blocked) { e->drag_node=i; e->drag_origin=e->gui.mouse; }
            if(e->tree_dragging) tide_draw_thick_line(&e->gui.list,tide_f2(box.x,box.y+22),tide_f2(box.x+box.width,box.y+22),2,rgb(.3f,.6f,.9f));
        }
    }
    tide_draw_unset_clip(&e->gui.list);
    const int maximum=row>visible ? row-visible : 0;
    if(e->tree_scroll>maximum) e->tree_scroll=maximum;
    if(!blocked && e->gui.mouse_held && e->drag_node>0 && editor_can_structure(&e->doc,e->drag_node) && tide_length_f2(tide_sub_f2(e->gui.mouse,e->drag_origin))>6) e->tree_dragging=true;
    if(e->gui.mouse_released || !e->gui.mouse_held) {
        if(!blocked && e->tree_dragging && hovered>=0 && hovered!=e->drag_node) { e->selected=e->drag_node; e->argument=hovered; queue(e,REPARENT_NODE); }
        e->tree_dragging=false; e->drag_node=-1;
    }
}
