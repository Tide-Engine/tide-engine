static bool playing(const editor *e)
{
    return e->play.state==EDITOR_PLAY_BUILDING || e->play.state==EDITOR_PLAY_RUNNING;
}

static void document_changed(editor *e)
{
    e->selected=0; e->tree_scroll=e->inspector_scroll=e->source_scroll=0;
    memset(e->folded,0,sizeof e->folded); clear_field(e);
    e->gui.editing=e->gui.focus=0; e->drag.active=false;
    e->asset_stamp=asset_stamp(&e->doc); e->scene_model_revision=UINT64_MAX;
    e->model_revision=UINT64_MAX; e->recovery=e->doc.recovery!=NULL;
}

static bool switch_document(editor *e, const char *path, const char *name)
{
    if(e->doc.path && editor_dirty(&e->doc) && !editor_save(&e->doc)) return false;
    char *old=e->doc.path ? own(e->doc.path) : NULL;
    char *project=own(e->workspace);
    editor_play_stop(&e->play); editor_preview_close(&e->preview,&e->draw);
    editor_close(&e->doc);
    bool ok=name ? editor_create(&e->doc,path,project,name) : editor_open(&e->doc,path,project);
    if(!ok) {
        char message[1024]; snprintf(message,sizeof message,"%s",e->doc.message);
        editor_close(&e->doc);
        if(old) editor_open(&e->doc,old,project);
        snprintf(e->doc.message,sizeof e->doc.message,"%s",message);
    }
    free(project); free(old); document_changed(e);
    return ok;
}

static void file_dialog(editor *e, tide_float2 size)
{
    const int modal=tide_gui_begin_modal(&e->gui,45000,TIDE_ANCHOR_MIDDLE_CENTER,&e->dialog_open);
    if(modal<0) return;
    static const char *titles[]={"","New Scene","New Prefab","Open authored file","Rename entity","Duplicate entity","Save as Prefab","Add entity"};
    tide_gui_layout_label(&e->gui,titles[e->dialog]);
    bool entered=false;
    if(e->dialog!=DIALOG_OPEN) {
        layout_text_field(e,45001,"Name",&e->dialog_name,&entered);
    }
    const bool file=e->dialog==DIALOG_OPEN || e->dialog==DIALOG_NEW_SCENE || e->dialog==DIALOG_NEW_PREFAB || e->dialog==DIALOG_EXTRACT;
    if(file) {
        layout_text_field(e,45002,"File",&e->dialog_path,NULL);
        tide_gui_layout_label(&e->gui,e->dialog==DIALOG_OPEN ? "Path relative to the project, or an absolute path." : "Leave File empty to use Name.tprefab in the project.");
    }
    if(tide_gui_layout_button(&e->gui,45003,e->dialog==DIALOG_OPEN ? "Open" : e->dialog==DIALOG_RENAME ? "Rename" : e->dialog==DIALOG_DUPLICATE ? "Duplicate" : "Create") || entered) {
        const action a=e->dialog==DIALOG_OPEN ? OPEN_FILE : e->dialog==DIALOG_RENAME ? RENAME_NODE : e->dialog==DIALOG_DUPLICATE ? DUPLICATE_NODE : e->dialog==DIALOG_CHILD ? NAMED_CHILD : e->dialog==DIALOG_EXTRACT ? EXTRACT_NODE : CREATE_FILE;
        queue_name(e,a,str_from(e->dialog_name));
        if(file) {
            sb path={0};
            if(e->dialog_path[0]) sb_put(&path,e->dialog_path);
            else sb_printf(&path,"%s.tprefab",e->dialog_name);
            char *absolute=path_absolute(path.data);
            const bool full=path.data[0]=='/' || path.data[0]=='\\' || (strlen(path.data)>1 && path.data[1]==':');
            free(e->pending_path); e->pending_path=full ? absolute : path_join(e->workspace,path.data);
            if(!full) free(absolute);
        }
        e->dialog_open=false;
    }
    if(e->dialog==DIALOG_OPEN && e->doc.prog) {
        int shown=0; const int limit=(int)((size.y-420)/30);
        for(int i=0;i<e->doc.prog->decls.count && shown<limit;i++) {
            decl *d=e->doc.prog->decls.items[i]; if(d->kind!=DECL_PREFAB || !d->unit) continue;
            if(tide_gui_layout_button(&e->gui,46000u+(uint32_t)i,str_to_cstr(d->qualified))) {
                queue_name(e,OPEN_FILE,d->qualified); replace(&e->pending_path,d->unit->src->path); e->dialog_open=false;
            }
            shown++;
        }
    }
    if(tide_gui_layout_button(&e->gui,45004,"Cancel")) e->dialog_open=false;
    tide_gui_close(&e->gui,modal);
}

static void value_picker(editor *e, tide_float2 size)
{
    const int modal=tide_gui_begin_modal(&e->gui,47000,TIDE_ANCHOR_MIDDLE_CENTER,&e->value_picker);
    if(modal<0) return;
    field *f=selected_field(e);
    tide_gui_layout_label(&e->gui,f && f->type.kind==TY_ENUM ? "Choose a value" : "Choose an entity");
    layout_text_field(e,47001,"Search",&e->search,NULL);
    if(e->devices.mouse.poll_scroll.y) e->argument-=scroll_rows(e->devices.mouse.poll_scroll.y,3);
    if(e->argument<0) e->argument=0;
    int row=0,shown=0; const int visible=(int)((size.y-300)/30);
    if(f && f->type.kind==TY_ENUM && f->type.decl) {
        decl *enumeration=f->type.decl;
        for(int i=0;i<enumeration->members.count;i++) {
            sb value={0}; sb_printf(&value,STR_FMT "." STR_FMT,STR_ARG(enumeration->qualified),STR_ARG(enumeration->members.items[i].name));
            if(e->search && e->search[0] && !strstr(value.data,e->search)) continue;
            if(row++<e->argument || shown++>=visible) continue;
            if(tide_gui_layout_button(&e->gui,47100u+(uint32_t)i,value.data)) { replace(&e->value,value.data); queue(e,SET); e->value_picker=false; }
        }
    } else {
        if(f && f->type.kind==TY_OPTIONAL && tide_gui_layout_button(&e->gui,47002,"None")) { replace(&e->value,"null"); queue(e,SET); e->value_picker=false; }
        for(int i=0;i<e->doc.nodes.count;i++) {
            const char *path=i==e->selected ? "this" : str_to_cstr(e->doc.nodes.items[i].path);
            if(!path[0] || (e->search && e->search[0] && !strstr(path,e->search))) continue;
            if(row++<e->argument || shown++>=visible) continue;
            if(tide_gui_layout_button(&e->gui,48000u+(uint32_t)i,path)) { replace(&e->value,path); queue(e,SET); e->value_picker=false; }
        }
    }
    if(tide_gui_layout_button(&e->gui,47003,"Cancel")) e->value_picker=false;
    tide_gui_close(&e->gui,modal);
}

static void apply_workspace_action(editor *e, const int selected)
{
    switch(e->pending) {
    case CREATE_FILE:
        if(switch_document(e,e->pending_path,e->pending_name) && e->dialog==DIALOG_NEW_SCENE && !e->doc.prog->main && !e->doc.prog->startup) editor_set_startup(&e->doc);
        break;
    case OPEN_FILE:
        if(e->pending_path && switch_document(e,e->pending_path,NULL) && e->pending_name) {
            for(int i=0;i<e->doc.prefabs.count;i++) if(str_eq_c(e->doc.prefabs.items[i]->qualified,e->pending_name)) { editor_select_prefab(&e->doc,i); document_changed(e); break; }
        }
        break;
    case RENAME_NODE: editor_rename_node(&e->doc,selected,e->pending_name); clear_field(e); break;
    case DUPLICATE_NODE: editor_duplicate_node(&e->doc,selected,e->pending_name); break;
    case REPARENT_NODE: editor_reparent_node(&e->doc,selected,e->argument); clear_field(e); break;
    case EXTRACT_NODE: editor_extract_prefab(&e->doc,selected,e->pending_path,e->pending_name); clear_field(e); break;
    case NAMED_CHILD: editor_add_named_child(&e->doc,selected,e->pending_name,NULL); break;
    case PLACE_MODEL: {
        char name[64]; int suffix=1; bool exists;
        do {
            snprintf(name,sizeof name,"Model%d",suffix++); exists=false;
            for(int i=0;i<e->doc.nodes.count;i++) if(e->doc.nodes.items[i].parent==selected && strcmp(node_name(&e->doc.nodes.items[i]),name)==0) { exists=true; break; }
        } while(exists && suffix<2000);
        if(editor_add_model(&e->doc,selected,name,str_from(e->component),str_from(e->field),e->value)) e->models=false;
        break;
    }
    case SET_STARTUP:
        if(!editor_dirty(&e->doc) || editor_save(&e->doc)) editor_set_startup(&e->doc);
        break;
    case PLAY: {
        const editor_play_options options={0};
        editor_play_start(&e->play,&e->doc,&options);
        break;
    }
    case STOP: editor_play_stop(&e->play); break;
    default: break;
    }
}
