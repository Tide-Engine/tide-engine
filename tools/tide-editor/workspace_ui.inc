static bool playing(const editor *e)
{
    return e->play.state==EDITOR_PLAY_BUILDING || e->play.state==EDITOR_PLAY_RUNNING;
}
static bool preview_failed(const editor *e)
{
    return e->preview.enabled && e->preview.attempted_revision==e->doc.revision && !e->preview.builder && !e->doc.errors && e->preview.revision!=e->doc.revision;
}

static void document_changed(editor *e)
{
    e->menu=0; replace(&e->hierarchy_search,"");
    free(e->root_name); e->root_name=NULL;
    e->selected=0; e->tree_scroll=e->inspector_scroll=e->source_scroll=0;
    memset(e->folded,0,sizeof e->folded); clear_field(e);
    e->gui.editing=e->gui.focus=0; e->drag.active=false;
    e->asset_stamp=asset_stamp(&e->doc); e->scene_model_revision=UINT64_MAX;
    e->model_revision=UINT64_MAX; e->recovery=e->doc.recovery!=NULL;
}

static void empty_document(editor *e, tide_rect r)
{
    const float x=r.x+70, y=r.y+74;
    if(!e->root_name) {
        const char *file=path_base(e->doc.path), *dot=strrchr(file,'.');
        const str name={file,(int)(dot ? dot-file : (ptrdiff_t)strlen(file))};
        e->root_name=own(lex_identifier_ok(name) ? str_to_cstr(name) : "Level");
    }
    label(e,x,y,"This file has no scene or prefab root yet.",ink);
    if(e->doc.syntax_errors) {
        label(e,x,y+36,"Fix this file's syntax in your code editor, then Reload.",dim);
        return;
    }
    label(e,x,y+36,"Create its root here to start adding objects and components.",dim);
    const int disabled=tide_gui_begin_disabled(&e->gui,e->doc.recovery || e->doc.conflict);
    bool entered=false;
    text_field(e,45010,(tide_rect){x,y+82,360,32},"Name",&e->root_name,&entered);
    if(button(e,45011,(tide_rect){x,y+130,180,34},"Create root") || entered) queue_name(e,INITIALIZE_ROOT,str_from(e->root_name));
    tide_gui_close(&e->gui,disabled);
    label(e,x,y+184,"The file stays unchanged until Save. Undo removes the new root.",dim);
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
    case INITIALIZE_ROOT:
        if(editor_initialize_prefab(&e->doc,e->pending_name)) { document_changed(e); e->source=e->models=false; }
        break;
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

// The bottom dock browses checked project declarations, using the same open,
// asset assignment and preview actions as the rest of the editor.
static void project_dock(editor *e, tide_rect r)
{
    panel(e,r,rgb(.22f,.22f,.22f));
    panel(e,(tide_rect){r.x,r.y,r.width,26},rgb(.18f,.18f,.18f));
    if(tab(e,46000,(tide_rect){r.x,r.y,92,26},"Project",e->project_tab==0)) e->project_tab=0;
    char console[64]; snprintf(console,sizeof console,"Console%s",e->doc.errors || e->doc.warnings || preview_failed(e) || e->play.state==EDITOR_PLAY_FAILED ? " !" : "");
    if(tab(e,46001,(tide_rect){r.x+92,r.y,100,26},console,e->project_tab==1)) e->project_tab=1;
    char counts[96]; snprintf(counts,sizeof counts,"%d errors    %d warnings",e->doc.errors,e->doc.warnings);
    label(e,r.x+r.width-184,r.y+6,counts,e->doc.errors ? rgb(.95f,.55f,.45f) : dim);
    if(e->project_tab==1) {
        const tide_rect rows={r.x+10,r.y+34,r.width-20,r.height-40};
        bool empty_entry=false;
        if(e->doc.path && !e->doc.prefabs.count && !e->doc.syntax_errors && e->doc.errors==1)
            for(int i=0;i<e->doc.diagnostics.count;i++) if(strstr(e->doc.diagnostics.items[i],"error: the program has no entry point")) empty_entry=true;
        const bool failed=e->play.state==EDITOR_PLAY_FAILED || preview_failed(e);
        const char *message=empty_entry ? "Create a root to edit and play this file. Choose project startup when ready." : e->play.state==EDITOR_PLAY_FAILED ? e->play.message : preview_failed(e) ? e->preview.message : !e->doc.diagnostics.count ? "No compiler messages." : NULL;
        int lines=e->doc.diagnostics.count;
        if(message) { lines=1; for(const char *p=message;*p;p++) if(*p=='\n') lines++; }
        if(!e->menu && contains(rows,e->gui.mouse)) e->diagnostic_scroll-=scroll_rows(e->devices.mouse.poll_scroll.y,2);
        const int visible=(int)(rows.height/22),maximum=lines>visible ? lines-visible : 0;
        e->diagnostic_scroll=e->diagnostic_scroll<0 ? 0 : e->diagnostic_scroll>maximum ? maximum : e->diagnostic_scroll;
        tide_draw_set_clip(&e->draw,rows);
        if(message) {
            int number=0;
            for(const char *p=message;*p;) {
                const char *end=strchr(p,'\n'); if(!end) end=p+strlen(p);
                if(number>=e->diagnostic_scroll && number<e->diagnostic_scroll+visible) {
                    sb line={0}; sb_putn(&line,p,(size_t)(end-p));
                    label(e,rows.x,rows.y+(float)(number-e->diagnostic_scroll)*22,line.data ? line.data : "",failed ? rgb(.95f,.55f,.45f) : dim);
                }
                number++; p=*end ? end+1 : end;
            }
        } else for(int i=e->diagnostic_scroll;i<e->doc.diagnostics.count && i<e->diagnostic_scroll+visible;i++) label(e,rows.x,rows.y+(float)(i-e->diagnostic_scroll)*22,e->doc.diagnostics.items[i],ink);
        tide_draw_unset_clip(&e->draw);
        return;
    }
    panel(e,(tide_rect){r.x,r.y+26,166,r.height-26},rgb(.195f,.195f,.195f));
    static const char *folders[]={"All Assets","Prefabs","Models"};
    for(int i=0;i<3;i++) if(list_item(e,46010u+(uint32_t)i,(tide_rect){r.x+6,r.y+32+(float)i*24,154,24},folders[i],e->project_filter==i,ink)) { e->project_filter=i; e->project_scroll=0; }
    const float left=r.x+178;
    label(e,left,r.y+34,path_base(e->workspace),dim);
    search_field(e,46020,(tide_rect){r.x+r.width-280,r.y+30,270,22},&e->project_search);
    const tide_rect rows={left,r.y+58,r.width-188,r.height-62};
    if(!e->menu && contains(rows,e->gui.mouse)) e->project_scroll-=scroll_rows(e->devices.mouse.poll_scroll.y,2);
    if(e->project_scroll<0) e->project_scroll=0;
    const int columns=(int)fmaxf(1,floorf(rows.width/180)),visible=(int)(rows.height/24);
    int offered=0;
    tide_draw_set_clip(&e->gui.list,rows);
    if(e->doc.prog) for(int i=0;i<e->doc.prog->decls.count;i++) {
        const decl *d=e->doc.prog->decls.items[i];
        const bool prefab=d->kind==DECL_PREFAB;
        const bool mesh=d->kind==DECL_ASSET && d->asset_kind==ASSET_MESH && d->asset;
        if((!prefab && !mesh) || (e->project_filter==1 && !prefab) || (e->project_filter==2 && !mesh)) continue;
        const char *name=str_to_cstr(d->qualified);
        if(e->project_search && e->project_search[0] && !strstr(name,e->project_search)) continue;
        const int items=mesh ? d->asset->count : 1;
        for(int m=0;m<items;m++) {
            const int number=offered++,row=number/columns-e->project_scroll;
            if(row<0 || row>=visible) continue;
            const tide_rect box={rows.x+(float)(number%columns)*(rows.width/(float)columns),rows.y+(float)row*24,rows.width/(float)columns-4,22};
            sb title={0}; sb_printf(&title,"%s  %s",prefab ? "P" : "M",name);
            if(mesh && d->asset_many) sb_printf(&title," / %s",path_base(d->asset->items[m].key));
            const bool active=prefab ? e->doc.prefabs.count && d==e->doc.prefabs.items[e->doc.active] : e->model_asset && e->model_key && strcmp(e->model_asset,name)==0 && strcmp(e->model_key,d->asset->items[m].key)==0;
            if(!list_item(e,0x50520000u+(uint32_t)number,box,title.data,active,prefab ? rgb(.64f,.8f,.96f) : ink)) continue;
            if(prefab) {
                e->models=e->source=false;
                bool local=false;
                for(int p=0;p<e->doc.prefabs.count;p++) if(e->doc.prefabs.items[p]==d) { queue(e,PREFAB); e->argument=p; local=true; break; }
                if(!local) { queue_name(e,OPEN_FILE,d->qualified); replace(&e->pending_path,d->unit->src->path); }
            } else {
                replace(&e->model_asset,name); replace(&e->model_key,d->asset->items[m].key);
                e->model_revision=UINT64_MAX; e->model_zoom=1; e->model_pitch=.3f; e->model_yaw=-.6f;
                e->models=true; e->source=false;
            }
        }
    }
    tide_draw_unset_clip(&e->gui.list);
    const int maximum=(offered+columns-1)/columns-visible;
    if(e->project_scroll>maximum) e->project_scroll=maximum>0 ? maximum : 0;
    if(!offered) label(e,rows.x,rows.y,e->doc.path ? "No matching assets in this project." : "Open a scene or prefab to browse its project.",dim);
}

static void workspace_menu(editor *e)
{
    if(!e->menu) return;
    const int menu=e->menu;
    const float x=menu==1 ? 4 : menu==2 ? 56 : menu==5 ? 104 : 8, y=menu<3 || menu==5 ? 22 : 112;
    const int count=menu==1 ? 5 : menu==2 || menu==3 ? 2 : 5;
    const tide_rect r={x,y,232,(float)count*28+8};
    // Controls are emitted after the disabled workspace, so a menu never also
    // selects a scene object or starts an Inspector edit underneath it.
    tide_draw_rect(&e->gui.list,tide_f2(r.x+r.width/2+3,r.y+r.height/2+3),tide_f2(r.width+4,r.height+4),rgb(.08f,.08f,.08f));
    tide_draw_rect(&e->gui.list,tide_f2(r.x+r.width/2,r.y+r.height/2),tide_f2(r.width,r.height),rgb(.22f,.22f,.22f));
    static const char *file[]={"New Scene...","New Prefab...","Open...","Save    Ctrl+S","Reload"};
    static const char *edit[]={"Undo    Ctrl+Z","Redo    Ctrl+Y"};
    static const char *create[]={"Empty Object","Prefab Instance..."};
    static const char *object[]={"Rename    F2","Duplicate...","Save as Prefab...","Delete","Open Source Prefab"};
    for(int i=0;i<count;i++) {
        const bool editable=e->doc.path && editor_can_structure(&e->doc,e->selected);
        const bool disabled=menu==1 ? (i>=3 && !e->doc.path) || (i==3 && !e->doc.prefabs.count && !editor_dirty(&e->doc)) : menu==2 ? !e->doc.path : menu==3 ? !editable : i<4 ? !editable || e->selected<=0 : !e->doc.prefabs.count;
        const int scope=tide_gui_begin_disabled(&e->gui,disabled);
        const bool clicked=list_item(e,47000u+(uint32_t)menu*16u+(uint32_t)i,(tide_rect){x+4,y+4+(float)i*28,224,28},menu==1 ? file[i] : menu==2 ? edit[i] : menu==3 ? create[i] : object[i],false,disabled ? dim : ink);
        tide_gui_close(&e->gui,scope);
        if(!clicked) continue;
        e->menu=0;
        if(menu==1) {
            if(i<2) open_dialog(e,i==0 ? DIALOG_NEW_SCENE : DIALOG_NEW_PREFAB,i==0 ? "Level" : "Object",NULL);
            else if(i==2) open_dialog(e,DIALOG_OPEN,NULL,NULL);
            else if(i==3) queue(e,SAVE);
            else if(editor_dirty(&e->doc)) e->discard=true; else queue(e,RELOAD);
        } else if(menu==2) queue(e,i==0 ? UNDO : REDO);
        else if(menu==3) {
            if(i==0) open_dialog(e,DIALOG_CHILD,"Object",NULL);
            else { e->picker=true; e->nested_picker=true; e->argument=0; replace(&e->search,""); }
        } else if(i==0) open_dialog(e,DIALOG_RENAME,node_name(&e->doc.nodes.items[e->selected]),NULL);
        else if(i==1) { sb name={0}; sb_printf(&name,"%sCopy",node_name(&e->doc.nodes.items[e->selected])); open_dialog(e,DIALOG_DUPLICATE,name.data,NULL); }
        else if(i==2) open_dialog(e,DIALOG_EXTRACT,node_name(&e->doc.nodes.items[e->selected]),NULL);
        else if(i==3) queue(e,DELETE_NODE);
        else open_node_source(e);
    }
    const tide_rect anchor=menu==1 ? (tide_rect){4,0,48,22} : menu==2 ? (tide_rect){54,0,48,22} : menu==5 ? (tide_rect){104,0,112,22} : (tide_rect){0};
    if(e->devices.keyboard.escape.down || (e->gui.mouse_pressed && !contains(r,e->gui.mouse) && !contains(anchor,e->gui.mouse))) e->menu=0;
}
