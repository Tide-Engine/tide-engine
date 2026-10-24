#include "document.h"
#include "sys.h"
#include "tide_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *fixture(const char *name, const char *code, const char *prefab)
{
    char *root = path_join(TIDE_EDITOR_TEST_DIR, name);
    // Only this test's named folder beneath its configured build directory.
    if (strncmp(root, TIDE_EDITOR_TEST_DIR, strlen(TIDE_EDITOR_TEST_DIR)) != 0) abort();
    sys_remove_tree(root); sys_mkdirs(root);
    char *file = path_join(root, "tide.packages"); sys_write_text(file, ""); free(file);
    file = path_join(root, "game.tide"); sys_write_text(file, code); free(file);
    file = path_join(root, "Level.tprefab"); sys_write_text(file, prefab); free(root); return file;
}

static const char *code = "scene Main {}\ncomponent Health { int value = 100; string label = \"base\"; }\n"
                          "component Target { [SceneRequired] Entity entity; }\n";
static const char *tree = "// untouched h\xc3\xa9llo\r\n"
    "prefab Level {\r\n    Transform;\r\n    Health { value = 5 /* keep value comment */, label = \"hello\" };\r\n"
    "    entity Child { Transform; Health; }\r\n}\r\n";

TIDE_TEST(editor_opted_views_match_authored_prefabs_without_game_spawns)
{
    const char *text = "scene Main {}\ncomponent Health { int value = 5; }\n"
        "[RunInEditor] view Shared(Health health) { Draw.Circle(float2(health.value,0),1,Color.white); }\n"
        "[EditorOnly] view Local(local Health health) { Draw.Circle(float2(health.value,0),1,Color.white); }\n";
    char *file = fixture("opted-views", text, "prefab Level { Health; entity Child { Health; } }\n");
    editor_document d; TIDE_REQUIRE(editor_open(&d, file, NULL));
    TIDE_CHECK(d.errors == 0 && d.warnings == 0);
    editor_close(&d); free(file);
}

static int node(editor_document *d, const char *path)
{
    for (int i = 0; i < d->nodes.count; i++) if (str_eq_c(d->nodes.items[i].path, path)) return i;
    return -1;
}

TIDE_TEST(editor_nesting_prefab_keeps_authored_root_transform)
{
    char *file=fixture("nested-root-transform","scene Main;\n",
        "prefab Level { Transform; }\nprefab Crate { Transform { localScale = float3(2) }; }\n");
    editor_document d; TIDE_REQUIRE(editor_open(&d,file,NULL));
    TIDE_REQUIRE(editor_add_named_child(&d,0,"Copy","Crate"));
    TIDE_CHECK(strstr(d.text,"entity Copy : Crate { }")!=NULL);
    expr *transform=editor_component(&d.nodes.items[node(&d,"Copy")],str_from("Transform"));
    TIDE_REQUIRE(transform && transform->type_decl);
    for(int i=0;i<transform->type_decl->fields.count;i++) {
        field *f=&transform->type_decl->fields.items[i];
        if(str_eq_c(f->name,"localScale")) TIDE_CHECK(strcmp(editor_expression(editor_value(transform,f)),"float3(2)")==0);
    }
    editor_close(&d); free(file);
}

TIDE_TEST(editor_edits_preserve_source_and_undo)
{
    char *file = fixture("edits", code, tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    TIDE_CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "9 + 2"));
    TIDE_CHECK(strstr(d.text, "value = 9 + 2 /* keep value comment */") != NULL);
    TIDE_CHECK(strstr(d.text, "// untouched h\xc3\xa9llo\r\n") != NULL);
    TIDE_CHECK(strstr(d.text, "label = \"hello\"") != NULL);
    expr *health = editor_component(&d.nodes.items[0], str_from("Health"));
    TIDE_REQUIRE(health && health->type_decl);
    TIDE_CHECK(strcmp(editor_expression(editor_value(health, &health->type_decl->fields.items[0])), "9 + 2") == 0);
    TIDE_CHECK(editor_dirty(&d)); TIDE_CHECK(sys_exists(d.draft_path));
    TIDE_CHECK(editor_undo(&d, false)); TIDE_CHECK(strcmp(d.text, tree) == 0);
    TIDE_CHECK(!sys_exists(d.draft_path));
    TIDE_CHECK(editor_undo(&d, true)); TIDE_CHECK(editor_dirty(&d));
    TIDE_CHECK(editor_reset_field(&d, 0, str_from("Health"), str_from("label")));
    TIDE_CHECK(d.errors == 0); TIDE_CHECK(strstr(d.text, "label =") == NULL);
    TIDE_CHECK(editor_set_field(&d, 0, str_from("Transform"), str_from("localPosition"), "float3(2, 3, 4)"));
    TIDE_CHECK(d.errors == 0); TIDE_CHECK(strstr(d.text, "Transform { localPosition = float3(2, 3, 4) };") != NULL);
    TIDE_CHECK(editor_save(&d)); TIDE_CHECK(!editor_dirty(&d)); TIDE_CHECK(!sys_exists(d.draft_path));
    const uint64_t saved_revision = d.revision;
    editor_poll(&d); TIDE_CHECK(d.revision == saved_revision);
    char *disk = sys_read_file(file, NULL); TIDE_CHECK(strcmp(disk, d.text) == 0); free(disk);
    editor_close(&d); free(file);
}

TIDE_TEST(editor_semantic_drafts_save_and_syntax_can_be_corrected)
{
    char *file = fixture("invalid", code, tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL));
    TIDE_CHECK(!editor_set_field(&d, 0, str_from("Health"), str_from("value"), "float3("));
    TIDE_CHECK(strcmp(d.text, tree) == 0); TIDE_CHECK(d.errors == 0); TIDE_CHECK(d.undo_count == 0);
    TIDE_CHECK(strstr(d.message, "Edit rejected:") != NULL);
    TIDE_CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "\"wrong type\""));
    TIDE_CHECK(d.errors > 0); TIDE_CHECK(editor_save(&d));
    char *disk = sys_read_file(file, NULL); TIDE_CHECK(strcmp(disk, d.text) == 0); free(disk);
    TIDE_CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "12"));
    TIDE_CHECK(d.errors == 0); TIDE_CHECK(editor_save(&d));
    editor_close(&d); free(file);
}

TIDE_TEST(editor_nested_overrides_and_required_references)
{
    const char *prefab = "prefab Level { Target { entity = Child.Lid }; entity Child : Box {} }\n"
                         "prefab Box { Target { entity = Lid }; Health { value = 30 }; entity Lid { Transform; } }\n";
    char *file = fixture("nested", code, prefab); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    TIDE_CHECK(d.nodes.count == 3); TIDE_CHECK(!d.nodes.items[node(&d, "Child.Lid")].authored);
    expr *target = editor_component(&d.nodes.items[node(&d, "Child")], str_from("Target"));
    TIDE_REQUIRE(target);
    TIDE_CHECK(strcmp(editor_reference_expression(&d, node(&d, "Child"), editor_value(target, &target->type_decl->fields.items[0])), "Child.Lid") == 0);
    TIDE_CHECK(!editor_remove_node(&d, node(&d, "Child.Lid")));
    TIDE_CHECK(editor_set_field(&d, node(&d, "Child"), str_from("Health"), str_from("value"), "55"));
    TIDE_CHECK(d.errors == 0); TIDE_CHECK(strstr(d.text, "Health { value = 30 }") != NULL);
    TIDE_CHECK(editor_reset_field(&d, node(&d, "Child"), str_from("Health"), str_from("value")));
    expr *health = editor_component(&d.nodes.items[node(&d, "Child")], str_from("Health"));
    TIDE_REQUIRE(health); TIDE_CHECK(editor_value(health, &health->type_decl->fields.items[0])->int_value == 30);
    TIDE_CHECK(editor_remove_node(&d, node(&d, "Child"))); TIDE_CHECK(d.errors > 0);
    TIDE_CHECK(editor_save(&d)); TIDE_CHECK(editor_undo(&d, false)); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(editor_add_child(&d, 0, "Box")); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(node(&d, "Entity1.Lid") >= 0);
    editor_close(&d); free(file);
}

TIDE_TEST(editor_external_conflicts_and_recovery_keep_drafts)
{
    char *file = fixture("conflict", code, tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL));
    TIDE_CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "17"));
    editor_close(&d);
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.recovery);
    TIDE_CHECK(!editor_dirty(&d)); TIDE_CHECK(editor_recover(&d, true)); TIDE_CHECK(editor_dirty(&d));
    const char *outside = "// another programmer\nprefab Level { Health; }\n";
    TIDE_CHECK(sys_write_text(file, outside)); editor_poll(&d);
    TIDE_CHECK(d.conflict); TIDE_CHECK(strstr(d.text, "value = 17") != NULL); TIDE_CHECK(!editor_save(&d));
    char *disk = sys_read_file(file, NULL); TIDE_CHECK(strcmp(disk, outside) == 0); free(disk);
    TIDE_CHECK(!editor_reload(&d, false)); TIDE_CHECK(editor_reload(&d, true));
    TIDE_CHECK(!d.conflict); TIDE_CHECK(!editor_dirty(&d)); TIDE_CHECK(!sys_exists(d.draft_path));
    editor_close(&d); free(file);
}

TIDE_TEST(editor_project_boundaries_and_source_changes)
{
    char *file = fixture("project", code, tree); char *root = path_dir(file), *folder = path_join(root, "scenes");
    sys_mkdirs(folder); char *nested = path_join(folder, "Level.tprefab"); sys_rename(file, nested);
    editor_document d; TIDE_REQUIRE(editor_open(&d, nested, NULL));
    TIDE_CHECK(strcmp(d.project, root) == 0); TIDE_CHECK(d.errors == 0);
    char *script = path_join(root, "game.tide");
    TIDE_CHECK(sys_write_text(script, "scene Main {}\ncomponent Health { int renamed = 100; string label; }\n"));
    editor_poll(&d); TIDE_CHECK(d.errors > 0); TIDE_CHECK(editor_save(&d));
    TIDE_CHECK(sys_write_text(script, code)); editor_poll(&d); TIDE_CHECK(d.errors == 0);
    char *separate = path_join(root, "separate"); sys_mkdirs(separate);
    char *manifest = path_join(separate, "tide.packages"); sys_write_text(manifest, "");
    char *other = path_join(separate, "bad.tide"); sys_write_text(other, "component Bad { syntax broken ! }");
    editor_refresh(&d); TIDE_CHECK(d.errors == 0);
    editor_close(&d); free(other); free(manifest); free(separate); free(script); free(nested); free(folder); free(root); free(file);
}

TIDE_TEST(editor_structural_changes_and_last_field_reset)
{
    char *file = fixture("structural", code, "prefab Level { Health { value = 1 }; }\n"); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL));
    TIDE_CHECK(editor_reset_field(&d, 0, str_from("Health"), str_from("value"))); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("label"), "\"a,b } \\\"quote\\\"\"")); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "8")); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(editor_reset_field(&d, 0, str_from("Health"), str_from("value"))); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(editor_add_component(&d, 0, str_from("Transform"))); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(editor_add_child(&d, 0, NULL)); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(editor_add_child(&d, 0, NULL)); TIDE_CHECK(node(&d, "Entity2") >= 0);
    TIDE_CHECK(editor_remove_component(&d, 0, str_from("Health"))); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(editor_save(&d)); editor_close(&d); free(file);
}

TIDE_TEST(editor_recovery_detects_source_changed_between_sessions)
{
    char *file = fixture("recovery-conflict", code, tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL));
    TIDE_CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "44")); editor_close(&d);
    const char *newer = "prefab Level { Health { value = 99 }; }\n";
    TIDE_CHECK(sys_write_text(file, newer)); TIDE_REQUIRE(editor_open(&d, file, NULL));
    TIDE_CHECK(editor_recover(&d, true)); TIDE_CHECK(d.conflict); TIDE_CHECK(!editor_save(&d));
    char *disk = sys_read_file(file, NULL); TIDE_CHECK(strcmp(disk, newer) == 0); free(disk);
    editor_close(&d); free(file);
}

TIDE_TEST(editor_package_components_and_added_source_files)
{
    char *file = fixture("packages", "using Props;\nscene Main {}\n", "using Props;\nprefab Level { Health; }\n");
    char *root = path_dir(file), *dependency = path_join(root, "dependency"); sys_mkdirs(dependency);
    char *meta = path_join(dependency, "tide.packages"), *comp = path_join(dependency, "health.tide");
    TIDE_CHECK(sys_write_text(meta, "package Props\n")); TIDE_CHECK(sys_write_text(comp, "namespace Props;\ncomponent Health { int value = 7; }\n"));
    char *manifest = path_join(root, "tide.packages"); TIDE_CHECK(sys_write_text(manifest, "./dependency\n"));
    editor_document d; TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    TIDE_CHECK(editor_set_field(&d, 0, str_from("Props.Health"), str_from("value"), "8")); TIDE_CHECK(d.errors == 0);
    char *added = path_join(root, "more.tide"); TIDE_CHECK(sys_write_text(added, "component More { bool active; }\n"));
    editor_poll(&d); bool seen = false;
    for (int i = 0; i < d.prog->decls.count; i++) seen |= str_eq_c(d.prog->decls.items[i]->name, "More");
    TIDE_CHECK(seen); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(editor_add_component(&d, 0, str_from("More"))); TIDE_CHECK(d.errors == 0);
    editor_close(&d); free(added); free(manifest); free(comp); free(meta); free(dependency); free(root); free(file);
}

#ifdef _WIN32
TIDE_TEST(editor_windows_path_spelling_preserves_filename_and_recovery)
{
    char *file = fixture("path-case", code, tree);
    char *lower = malloc(strlen(file) + 1); TIDE_REQUIRE(lower);
    strcpy(lower, file);
    for (char *p = lower; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
    editor_document d; TIDE_REQUIRE(editor_open(&d, lower, NULL)); TIDE_REQUIRE(d.errors == 0);
    TIDE_CHECK(strstr(d.path, "Level.tprefab") != NULL);
    TIDE_CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "22")); editor_close(&d);
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_CHECK(d.recovery != NULL);
    TIDE_CHECK(editor_recover(&d, true)); TIDE_CHECK(editor_save(&d));
    editor_close(&d); free(lower); free(file);
}
#endif

static const char *hierarchy_code = "scene Main;\ncomponent Links { Entity self; Entity other; List<Entity> items; }\n";
static const char *hierarchy_tree = "// keep h\xc3\xa9llo\r\n"
    "prefab Level {\r\n"
    "    Links { other = Group.Kid };\r\n"
    "    entity Outside { Transform; }\r\n"
    "    entity Group {\r\n"
    "        Transform { localPosition = float3(2, 3, 4) }; // local TRS\r\n"
    "        Links { self = this, other = Outside, items = [Group.Kid, Outside] };\r\n"
    "        entity Kid { Links { other = Group }; }\r\n"
    "    }\r\n"
    "    entity Container { Transform; }\r\n"
    "}\r\n";

static expr *value(editor_document *d, const char *path, const char *component, const char *field_name)
{
    const int i = node(d, path);
    if (i < 0) return NULL;
    expr *c = editor_component(&d->nodes.items[i], str_from(component));
    for (int f = 0; c && c->type_decl && f < c->type_decl->fields.count; f++)
        if (str_eq_c(c->type_decl->fields.items[f].name, field_name)) return editor_value(c, &c->type_decl->fields.items[f]);
    return NULL;
}

static bool references(editor_document *d, expr *e, const char *path)
{
    return e && e->prefab_reference && e->prefab_entity == node(d, path);
}

TIDE_TEST(editor_rename_duplicate_reparent_checked_references_and_local_trs)
{
    char *file = fixture("hierarchy", hierarchy_code, hierarchy_tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    TIDE_REQUIRE(editor_rename_node(&d, node(&d, "Group"), "Renamed"));
    TIDE_CHECK(d.errors == 0 && d.undo_count == 1);
    TIDE_CHECK(references(&d, value(&d, "", "Links", "other"), "Renamed.Kid"));
    TIDE_CHECK(references(&d, value(&d, "Renamed.Kid", "Links", "other"), "Renamed"));
    TIDE_CHECK(strstr(d.text, "// keep h\xc3\xa9llo\r\n") != NULL);
    TIDE_CHECK(strstr(d.text, "// local TRS\r\n") != NULL);
    TIDE_REQUIRE(editor_undo(&d, false)); TIDE_CHECK(strcmp(d.text, hierarchy_tree) == 0);
    TIDE_REQUIRE(editor_duplicate_node(&d, node(&d, "Group"), "Copy"));
    TIDE_CHECK(d.errors == 0 && d.undo_count == 1);
    TIDE_CHECK(references(&d, value(&d, "", "Links", "other"), "Group.Kid"));
    TIDE_CHECK(references(&d, value(&d, "Copy", "Links", "self"), "Copy"));
    TIDE_CHECK(references(&d, value(&d, "Copy", "Links", "other"), "Outside"));
    TIDE_CHECK(references(&d, value(&d, "Copy.Kid", "Links", "other"), "Copy"));
    expr *items = value(&d, "Copy", "Links", "items");
    TIDE_REQUIRE(items && items->args.count == 2);
    TIDE_CHECK(references(&d, items->args.items[0], "Copy.Kid")); TIDE_CHECK(references(&d, items->args.items[1], "Outside"));
    TIDE_REQUIRE(editor_undo(&d, false)); TIDE_CHECK(strcmp(d.text, hierarchy_tree) == 0);
    TIDE_CHECK(!editor_reparent_node(&d, node(&d, "Group"), node(&d, "Group.Kid")));
    TIDE_CHECK(!editor_rename_node(&d, node(&d, "Group"), "Outside"));
    TIDE_CHECK(!editor_rename_node(&d, node(&d, "Group"), "bad name"));
    TIDE_REQUIRE(editor_reparent_node(&d, node(&d, "Group"), node(&d, "Container")));
    TIDE_CHECK(d.errors == 0 && d.undo_count == 1);
    TIDE_CHECK(references(&d, value(&d, "", "Links", "other"), "Container.Group.Kid"));
    TIDE_CHECK(references(&d, value(&d, "Container.Group.Kid", "Links", "other"), "Container.Group"));
    TIDE_CHECK(references(&d, value(&d, "Container.Group", "Links", "other"), "Outside"));
    TIDE_CHECK(strstr(d.text, "localPosition = float3(2, 3, 4)") != NULL);
    TIDE_REQUIRE(editor_undo(&d, false)); TIDE_CHECK(strcmp(d.text, hierarchy_tree) == 0);
    editor_close(&d); free(file);
}

TIDE_TEST(editor_inherited_deep_field_overrides_remain_structurally_readonly)
{
    const char *prefab = "prefab Level { entity Box : BoxPrefab {} }\n"
        "prefab BoxPrefab { entity Part { Health { value = 3 }; entity Deep { Health { value = 4 }; } } }\n";
    char *file = fixture("inherited-overrides", code, prefab); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    int deep = node(&d, "Box.Part.Deep"); TIDE_REQUIRE(deep >= 0);
    TIDE_CHECK(editor_node_inherited(&d, deep) && editor_can_edit_fields(&d, deep) && !editor_can_structure(&d, deep));
    TIDE_REQUIRE(editor_set_field(&d, deep, str_from("Health"), str_from("value"), "9"));
    TIDE_CHECK(d.errors == 0 && d.undo_count == 1);
    deep = node(&d, "Box.Part.Deep");
    TIDE_CHECK(d.nodes.items[deep].authored && editor_node_inherited(&d, deep));
    TIDE_CHECK(value(&d, "Box.Part.Deep", "Health", "value")->int_value == 9);
    TIDE_CHECK(editor_field_overridden(&d, deep, str_from("Health"), str_from("value")));
    TIDE_CHECK(!editor_add_child(&d, deep, NULL));
    TIDE_CHECK(!editor_remove_node(&d, deep));
    TIDE_CHECK(!editor_rename_node(&d, deep, "Changed"));
    TIDE_CHECK(!editor_duplicate_node(&d, deep, "Copy"));
    TIDE_CHECK(!editor_reparent_node(&d, deep, 0));
    TIDE_CHECK(!editor_add_component(&d, deep, str_from("Transform")));
    TIDE_CHECK(!editor_set_field(&d, deep, str_from("Transform"), str_from("localPosition"), "float3(1,2,3)"));
    TIDE_CHECK(!editor_set_field(&d, deep, str_from("Health"), str_from("missing"), "1"));
    TIDE_REQUIRE(editor_reset_field(&d, deep, str_from("Health"), str_from("value")));
    TIDE_CHECK(d.errors == 0 && value(&d, "Box.Part.Deep", "Health", "value")->int_value == 4);
    TIDE_CHECK(!d.nodes.items[node(&d, "Box.Part")].authored);
    TIDE_CHECK(strstr(d.text, "prefab BoxPrefab { entity Part { Health { value = 3 }; entity Deep { Health { value = 4 }; } } }") != NULL);
    TIDE_REQUIRE(editor_undo(&d, false)); TIDE_CHECK(value(&d, "Box.Part.Deep", "Health", "value")->int_value == 9);
    TIDE_REQUIRE(editor_save(&d)); editor_close(&d);
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_CHECK(editor_node_inherited(&d, node(&d, "Box.Part.Deep")));
    editor_close(&d); free(file);
}

TIDE_TEST(editor_creation_extract_place_undo_and_recovery_loop)
{
    char *file = fixture("creation-loop", code, "prefab Level {}\n");
    char *root = path_dir(file), *created = path_join(root, "NewScene.tprefab"), *asset = path_join(root, "Crate.tprefab");
    editor_document d;
    TIDE_CHECK(!editor_create(&d, created, root, "bad name")); TIDE_CHECK(!sys_exists(created));
    TIDE_CHECK(!editor_create(&d, created, root, "Level")); TIDE_CHECK(!sys_exists(created));
    TIDE_REQUIRE(editor_create(&d, created, root, "NewScene")); TIDE_CHECK(d.errors == 0 && !editor_dirty(&d));
    TIDE_REQUIRE(editor_add_named_child(&d, 0, "Box", NULL));
    TIDE_REQUIRE(editor_add_named_child(&d, node(&d, "Box"), "Lid", NULL));
    TIDE_REQUIRE(editor_add_component(&d, node(&d, "Box"), str_from("Target")));
    TIDE_CHECK(d.errors > 0); TIDE_REQUIRE(editor_save(&d));
    TIDE_REQUIRE(editor_set_field(&d, node(&d, "Box"), str_from("Target"), str_from("entity"), "Box.Lid"));
    TIDE_REQUIRE(editor_rename_node(&d, node(&d, "Box"), "CrateObject"));
    TIDE_REQUIRE(editor_set_field(&d, node(&d, "CrateObject"), str_from("Transform"), str_from("localPosition"), "float3(1,2,3)"));
    const int history = d.undo_count;
    char *before = malloc(strlen(d.text) + 1); TIDE_REQUIRE(before); strcpy(before, d.text);
    TIDE_REQUIRE(editor_extract_prefab(&d, node(&d, "CrateObject"), asset, "Crate"));
    TIDE_CHECK(d.errors == 0 && d.undo_count == history + 1);
    TIDE_CHECK(strstr(d.text, "entity CrateObject : Crate {}") != NULL);
    TIDE_CHECK(references(&d, value(&d, "CrateObject", "Target", "entity"), "CrateObject.Lid"));
    char *disk = sys_read_file(asset, NULL); TIDE_REQUIRE(disk);
    TIDE_CHECK(strstr(disk, "entity = Lid") != NULL && strstr(disk, "float3(1,2,3)") != NULL);
    TIDE_REQUIRE(editor_undo(&d, false)); TIDE_CHECK(strcmp(d.text, before) == 0); TIDE_CHECK(sys_exists(asset));
    TIDE_REQUIRE(editor_undo(&d, true));
    TIDE_REQUIRE(editor_add_named_child(&d, 0, "Second", "Crate")); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(references(&d, value(&d, "Second", "Target", "entity"), "Second.Lid"));
    TIDE_CHECK(editor_node_inherited(&d, node(&d, "Second.Lid")));
    TIDE_CHECK(!editor_add_named_child(&d, 0, "Second", NULL));
    TIDE_CHECK(!editor_extract_prefab(&d, node(&d, "Second"), asset, "Again"));
    editor_close(&d);
    TIDE_REQUIRE(editor_open(&d, created, root)); TIDE_REQUIRE(editor_recover(&d, true));
    TIDE_CHECK(node(&d, "Second.Lid") >= 0 && d.errors == 0); TIDE_REQUIRE(editor_save(&d));
    editor_close(&d);
    TIDE_CHECK(!editor_create(&d, created, root, "Overwrite"));
    char *unchanged = sys_read_file(asset, NULL); TIDE_CHECK(unchanged && strcmp(unchanged, disk) == 0);
    free(unchanged); free(disk); free(before); free(asset); free(created); free(root); free(file);
}

TIDE_TEST(editor_extract_reference_boundaries_and_collisions_do_not_lose_work)
{
    char *file = fixture("extract-boundaries", hierarchy_code, hierarchy_tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL));
    char *root = path_dir(file), *asset = path_join(root, "Extracted.tprefab");
    TIDE_CHECK(!editor_extract_prefab(&d, node(&d, "Group"), asset, "Extracted"));
    TIDE_CHECK(!sys_exists(asset) && strcmp(d.text, hierarchy_tree) == 0 && !d.undo_count);
    TIDE_REQUIRE(editor_set_field(&d, node(&d, "Group"), str_from("Links"), str_from("other"), "this"));
    TIDE_REQUIRE(editor_reset_field(&d, node(&d, "Group"), str_from("Links"), str_from("items")));
    TIDE_REQUIRE(editor_reset_field(&d, node(&d, "Group.Kid"), str_from("Links"), str_from("other")));
    TIDE_REQUIRE(editor_extract_prefab(&d, node(&d, "Group"), asset, "Extracted"));
    TIDE_CHECK(d.errors == 0); TIDE_CHECK(references(&d, value(&d, "", "Links", "other"), "Group.Kid"));
    TIDE_CHECK(references(&d, value(&d, "Group", "Links", "self"), "Group"));
    TIDE_CHECK(!editor_extract_prefab(&d, node(&d, "Container"), asset, "Other"));
    editor_close(&d); free(asset); free(root); free(file);
}

TIDE_TEST(editor_hierarchy_updates_consumers_and_rejects_other_source_edits)
{
    const char *prefab = "prefab Base { entity Child { Health; } }\n"
        "prefab Level { Target { entity = First.Child }; entity First : Base { entity Child { Health { value = 9 }; } } }\n";
    char *file = fixture("consumer-paths", code, prefab); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    TIDE_REQUIRE(editor_rename_node(&d, node(&d, "Child"), "Renamed")); TIDE_CHECK(d.errors == 0);
    TIDE_CHECK(strstr(d.text, "First.Renamed") != NULL);
    TIDE_CHECK(strstr(d.text, "entity Renamed { Health { value = 9 }") != NULL);
    TIDE_REQUIRE(editor_undo(&d, false)); TIDE_CHECK(strcmp(d.text, prefab) == 0);
    char *root = path_dir(file), *consumer = path_join(root, "Other.tprefab");
    TIDE_REQUIRE(sys_write_text(consumer, "prefab Other { Target { entity = Instance.Child }; entity Instance : Base {} }\n"));
    editor_refresh(&d); TIDE_REQUIRE(d.errors == 0);
    TIDE_CHECK(!editor_rename_node(&d, node(&d, "Child"), "Changed"));
    TIDE_CHECK(strcmp(d.text, prefab) == 0 && !d.undo_count);
    editor_close(&d); free(consumer); free(root); free(file);
}

TIDE_TEST(editor_creation_respects_project_boundaries_and_existing_bytes)
{
    char *file = fixture("create-boundaries", code, "prefab Level {}\n"), *root = path_dir(file);
    char *sub = path_join(root, "scenes"), *nested = path_join(root, "nested"), *hidden = path_join(root, ".hidden");
    TIDE_REQUIRE(sys_mkdirs(sub) && sys_mkdirs(nested) && sys_mkdirs(hidden));
    char *marker = path_join(nested, "tide.packages"); TIDE_REQUIRE(sys_write_text(marker, ""));
    editor_document d;
    char *path = path_join(sub, "Scene.tprefab");
    TIDE_REQUIRE(editor_create(&d, path, root, "Scene")); TIDE_CHECK(strcmp(d.project, root) == 0); editor_close(&d);
    char *before = sys_read_file(path, NULL); TIDE_REQUIRE(before);
    TIDE_CHECK(!editor_create(&d, path, root, "Replacement"));
    char *after = sys_read_file(path, NULL); TIDE_CHECK(after && strcmp(before, after) == 0); free(before); free(after); free(path);
    path = path_join(nested, "Other.tprefab"); TIDE_CHECK(!editor_create(&d, path, root, "Other")); TIDE_CHECK(!sys_exists(path)); free(path);
    path = path_join(hidden, "Hidden.tprefab"); TIDE_CHECK(!editor_create(&d, path, root, "Hidden")); TIDE_CHECK(!sys_exists(path)); free(path);
    path = path_join(sub, "Wrong.tide"); TIDE_CHECK(!editor_create(&d, path, root, "Wrong")); TIDE_CHECK(!sys_exists(path)); free(path);
    path = path_join(sub, "../Scene:stream.tprefab"); TIDE_CHECK(!editor_create(&d, path, root, "Stream")); free(path);
    free(marker); free(hidden); free(nested); free(sub); free(root); free(file);
}

TIDE_TEST(editor_inherited_field_reset_preserves_other_overrides_and_paths)
{
    const char *prefab = "prefab Level { entity Box : BoxPrefab {} }\n"
        "prefab BoxPrefab { entity Child { Health { value = 3, label = \"base\" }; Transform; } }\n";
    char *file = fixture("override-reset", code, prefab); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    TIDE_REQUIRE(editor_set_field(&d, node(&d, "Box.Child"), str_from("Health"), str_from("value"), "7"));
    TIDE_REQUIRE(editor_set_field(&d, node(&d, "Box.Child"), str_from("Health"), str_from("label"), "\"local\""));
    TIDE_REQUIRE(editor_set_field(&d, node(&d, "Box.Child"), str_from("Transform"), str_from("localPosition"), "float3(1,2,3)"));
    TIDE_REQUIRE(editor_reset_field(&d, node(&d, "Box.Child"), str_from("Health"), str_from("value")));
    TIDE_CHECK(d.errors == 0 && value(&d, "Box.Child", "Health", "value")->int_value == 3);
    TIDE_CHECK(editor_field_overridden(&d, node(&d, "Box.Child"), str_from("Health"), str_from("label")));
    TIDE_REQUIRE(editor_reset_field(&d, node(&d, "Box.Child"), str_from("Health"), str_from("label")));
    TIDE_CHECK(d.errors == 0 && d.nodes.items[node(&d, "Box.Child")].authored);
    TIDE_CHECK(!editor_field_overridden(&d, node(&d, "Box.Child"), str_from("Health"), str_from("label")));
    TIDE_CHECK(editor_field_overridden(&d, node(&d, "Box.Child"), str_from("Transform"), str_from("localPosition")));
    TIDE_REQUIRE(editor_add_named_child(&d, 0, "Moving", NULL));
    TIDE_CHECK(!editor_reparent_node(&d, node(&d, "Moving"), node(&d, "Box.Child")));
    TIDE_CHECK(!editor_rename_node(&d, node(&d, "Moving"), "Box"));
    TIDE_REQUIRE(editor_reset_field(&d, node(&d, "Box.Child"), str_from("Transform"), str_from("localPosition")));
    TIDE_CHECK(!d.nodes.items[node(&d, "Box.Child")].authored && d.errors == 0);
    editor_close(&d); free(file);
}

TIDE_TEST(editor_other_source_syntax_errors_allow_draft_save_and_field_edits)
{
    char *file = fixture("broken-code-draft", code, tree), *root = path_dir(file), *script = path_join(root, "broken.tide");
    TIDE_REQUIRE(sys_write_text(script, "component Broken { int value = ; }\n"));
    editor_document d; TIDE_REQUIRE(editor_open(&d, file, root));
    TIDE_REQUIRE(d.errors > 0 && !d.syntax_errors);
    TIDE_REQUIRE(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "28"));
    TIDE_REQUIRE(editor_save(&d)); TIDE_CHECK(d.errors > 0);
    char *disk = sys_read_file(file, NULL); TIDE_CHECK(disk && strcmp(disk, d.text) == 0); free(disk);
    editor_close(&d); free(script); free(root); free(file);
}

TIDE_TEST(editor_model_placement_is_one_checked_asset_assignment)
{
    const char *script = "scene Main;\nasset Mesh<Vertex3> triangle = \"triangle.obj\";\n"
        "asset List<Mesh<Vertex3>> triangles = \"*.obj\";\ncomponent Model { Asset<Mesh<Vertex3>> mesh; int number; }\n";
    char *file = fixture("place-model", script, "prefab Level {}\n"), *root = path_dir(file), *obj = path_join(root, "triangle.obj");
    TIDE_REQUIRE(sys_write_text(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
    editor_document d; TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    TIDE_CHECK(!editor_add_model(&d, 0, "Wrong", str_from("Model"), str_from("number"), "triangle"));
    TIDE_CHECK(!editor_add_model(&d, 0, "Wrong", str_from("Model"), str_from("mesh"), "triangles"));
    TIDE_REQUIRE(editor_add_model(&d, 0, "Placed", str_from("Model"), str_from("mesh"), "triangle"));
    TIDE_CHECK(d.errors == 0 && d.undo_count == 1);
    expr *asset = value(&d, "Placed", "Model", "mesh");
    TIDE_CHECK(asset && asset->asset_reference && asset->constant && str_eq_c(asset->constant->qualified, "triangle"));
    TIDE_CHECK(editor_component(&d.nodes.items[node(&d, "Placed")], str_from("Transform")) != NULL);
    TIDE_REQUIRE(editor_undo(&d, false)); TIDE_CHECK(strcmp(d.text, "prefab Level {}\n") == 0);
    editor_close(&d); free(obj); free(root); free(file);
}

TIDE_TEST(editor_consumer_name_collisions_refuse_silent_entity_merges)
{
    const char *prefab = "prefab Base { entity A { Health; } }\n"
        "prefab Level { Target { entity = Inst.A }; entity Inst : Base { entity B { Health { value = 9 }; } } }\n";
    char *file = fixture("consumer-collision", code, prefab); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    TIDE_CHECK(!editor_rename_node(&d, node(&d, "A"), "B"));
    TIDE_CHECK(!editor_duplicate_node(&d, node(&d, "A"), "B"));
    TIDE_CHECK(strcmp(d.text, prefab) == 0 && !d.undo_count);
    editor_close(&d); free(file);
}

TIDE_TEST(editor_duplicate_instance_keeps_inherited_relative_references)
{
    const char *prefab = "prefab Level { entity Group : Box {} }\n"
        "prefab Box { Target { entity = Kid }; entity Kid { Health; } }\n";
    char *file = fixture("duplicate-instance", code, prefab); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    TIDE_REQUIRE(editor_duplicate_node(&d, node(&d, "Group"), "Copy"));
    TIDE_CHECK(d.errors == 0 && d.undo_count == 1);
    TIDE_CHECK(references(&d, value(&d, "Copy", "Target", "entity"), "Copy.Kid"));
    TIDE_CHECK(references(&d, value(&d, "Group", "Target", "entity"), "Group.Kid"));
    editor_close(&d); free(file);
}

TIDE_TEST(editor_conditional_path_actions_refuse_unchecked_branch_references)
{
    const char *prefab = "prefab Level {\n entity Group {\n Target { entity =\n#if DEBUG\n Group.Kid\n#else\n Group.Kid\n#endif\n };\n entity Kid { Health; }\n }\n}\n";
    char *file = fixture("conditional-paths", code, prefab); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    char *root = path_dir(file), *asset = path_join(root, "Extracted.tprefab");
    TIDE_CHECK(!editor_extract_prefab(&d, node(&d, "Group"), asset, "Extracted"));
    TIDE_CHECK(!editor_duplicate_node(&d, node(&d, "Group"), "Copy"));
    TIDE_CHECK(!editor_rename_node(&d, node(&d, "Group"), "Changed"));
    TIDE_CHECK(!sys_exists(asset) && strcmp(d.text, prefab) == 0 && !d.undo_count);
    editor_close(&d); free(asset); free(root); free(file);
}

TIDE_TEST(editor_new_paths_cannot_capture_entity_valued_constant_members)
{
    const char *script = "scene Main;\nstruct RefSet { Entity Next; }\nconst RefSet Refs = RefSet {};\n"
        "component Links { Entity other; }\n";
    const char *prefab = "prefab Level { Links { other = Refs.Next }; entity Group { entity Next { Transform; } } }\n";
    char *file = fixture("binding-capture", script, prefab); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    TIDE_CHECK(!editor_rename_node(&d, node(&d, "Group"), "Refs"));
    TIDE_CHECK(!editor_duplicate_node(&d, node(&d, "Group"), "Refs"));
    TIDE_CHECK(strcmp(d.text, prefab) == 0 && !d.undo_count);
    editor_close(&d); free(file);
    const char *nested = "prefab Level { entity Group { Links { other = Refs.Next }; entity Refs { entity Next { Transform; } } } }\n";
    file = fixture("extract-binding-capture", script, nested);
    TIDE_REQUIRE(editor_open(&d, file, NULL)); TIDE_REQUIRE(d.errors == 0);
    char *root = path_dir(file), *asset = path_join(root, "Captured.tprefab");
    TIDE_CHECK(!editor_extract_prefab(&d, node(&d, "Group"), asset, "Captured"));
    TIDE_CHECK(!sys_exists(asset) && strcmp(d.text, nested) == 0 && !d.undo_count && d.errors == 0);
    bool phantom = false;
    for (int i = 0; i < d.prog->decls.count; i++) phantom |= str_eq_c(d.prog->decls.items[i]->qualified, "Captured");
    TIDE_CHECK(!phantom);
    editor_close(&d); free(asset); free(root); free(file);
}

TIDE_TEST(editor_save_refuses_external_appended_nul_bytes)
{
    char *file = fixture("binary-conflict", code, tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL));
    TIDE_REQUIRE(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "77"));
    const char suffix[] = "\0outside edit";
    const size_t prefix = strlen(tree), len = prefix + sizeof suffix - 1;
    char *outside = malloc(len); TIDE_REQUIRE(outside);
    memcpy(outside, tree, prefix); memcpy(outside + prefix, suffix, sizeof suffix - 1);
    TIDE_REQUIRE(sys_write_file(file, outside, len));
    TIDE_CHECK(!editor_save(&d) && d.conflict && editor_dirty(&d));
    size_t disk_len = 0; char *disk = sys_read_file(file, &disk_len);
    TIDE_CHECK(disk && disk_len == len && memcmp(disk, outside, len) == 0);
    TIDE_CHECK(strstr(d.text, "value = 77") != NULL && sys_exists(d.draft_path));
    free(disk); free(outside); editor_close(&d); free(file);
}
