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
    char *disk = sys_read_file(file, NULL); TIDE_CHECK(strcmp(disk, d.text) == 0); free(disk);
    editor_close(&d); free(file);
}

TIDE_TEST(editor_invalid_values_block_save_and_syntax_can_be_corrected)
{
    char *file = fixture("invalid", code, tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, file, NULL));
    TIDE_CHECK(!editor_set_field(&d, 0, str_from("Health"), str_from("value"), "float3("));
    TIDE_CHECK(strcmp(d.text, tree) == 0); TIDE_CHECK(d.errors == 0); TIDE_CHECK(d.undo_count == 0);
    TIDE_CHECK(strstr(d.message, "Edit rejected:") != NULL);
    TIDE_CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "\"wrong type\""));
    TIDE_CHECK(d.errors > 0); TIDE_CHECK(!editor_save(&d));
    char *disk = sys_read_file(file, NULL); TIDE_CHECK(strcmp(disk, tree) == 0); free(disk);
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
    TIDE_CHECK(!editor_save(&d)); TIDE_CHECK(editor_undo(&d, false)); TIDE_CHECK(d.errors == 0);
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
    editor_poll(&d); TIDE_CHECK(d.errors > 0); TIDE_CHECK(!editor_save(&d));
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
