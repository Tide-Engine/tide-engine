#include "startup.h"
#include "sys.h"
#include "tide_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct startup_fixture {
    char *root, *file, *script, *startup;
} startup_fixture;

static const char *startup_tree = "namespace Levels;\nprefab First { Transform; }\nprefab Second { Transform; entity Kid { Transform; } }\n";

static startup_fixture fixture(const char *name, const char *code, const char *prefab)
{
    char *base = path_absolute(TIDE_EDITOR_TEST_DIR), *folder = path_join(base, "startup");
    startup_fixture f = {0}; f.root = path_join(folder, name);
    const size_t n = strlen(base);
    // Only this fixture's resolved folder beneath the configured test directory.
    if (strlen(f.root) <= n || strncmp(f.root, base, n) != 0 || f.root[n] != '/') abort();
    sys_remove_tree(f.root);
    if (!sys_mkdirs(f.root)) abort();
    char *manifest = path_join(f.root, "tide.packages");
    f.file = path_join(f.root, "Level.tprefab"); f.script = path_join(f.root, "game.tide");
    f.startup = path_join(f.root, "Startup.tide");
    if (!sys_write_text(manifest, "") || !sys_write_text(f.script, code) || !sys_write_text(f.file, prefab)) abort();
    free(manifest); free(folder); free(base);
    return f;
}

static void release(startup_fixture *f)
{
    free(f->root); free(f->file); free(f->script); free(f->startup);
}

static void disk_bytes(const char *path, const char *expected, const size_t len)
{
    size_t size = 0; char *disk = sys_read_file(path, &size);
    TIDE_CHECK(disk != NULL);
    TIDE_CHECK(size == len);
    TIDE_CHECK(disk && size == len && memcmp(disk, expected, len) == 0);
    free(disk);
}

static void disk_is(const char *path, const char *expected) { disk_bytes(path, expected, strlen(expected)); }

static void unchanged(const startup_fixture *f, const char *code, const char *prefab)
{
    disk_is(f->script, code); disk_is(f->file, prefab);
    char *manifest = path_join(f->root, "tide.packages"); disk_is(manifest, ""); free(manifest);
    TIDE_CHECK(!sys_exists(f->startup));
}

static bool write_bytes(const char *path, const char *bytes, const size_t len)
{
    FILE *file = fopen(path, "wb"); if (!file) return false;
    const bool written = fwrite(bytes, 1, len, file) == len;
    const bool closed = fclose(file) == 0;
    return written && closed;
}

static bool second(editor_document *d)
{
    for (int i = 0; i < d->prefabs.count; i++) if (str_eq_c(d->prefabs.items[i]->qualified, "Levels.Second")) {
        editor_select_prefab(d, i); return true;
    }
    return false;
}

TIDE_TEST(editor_startup_replaces_syntax_valid_wrong_string_value_without_leaving_quotes)
{
    const char *code = "scene Main;\nsettings { startScene = \"wrong\" /* preserve */; }\n";
    startup_fixture f = fixture("string-setting", code, startup_tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, f.file, NULL)); TIDE_REQUIRE(d.errors > 0 && !d.syntax_errors);
    TIDE_REQUIRE(editor_set_startup(&d));
    disk_is(f.script, "scene Main;\nsettings { startScene = Levels.First /* preserve */; }\n");
    TIDE_CHECK(d.errors == 0);
    editor_close(&d); release(&f);
}

TIDE_TEST(editor_startup_creates_source_and_preserves_selected_prefab)
{
    const char *code = "scene Main;\n";
    startup_fixture f = fixture("create", code, startup_tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, f.file, NULL)); TIDE_REQUIRE(second(&d));
    TIDE_REQUIRE(editor_set_startup(&d));
    disk_is(f.startup, "settings {\n    startScene = Levels.Second;\n}\n");
    disk_is(f.script, code); disk_is(f.file, startup_tree);
    TIDE_REQUIRE(d.active >= 0 && d.active < d.prefabs.count);
    TIDE_CHECK(str_eq_c(d.prefabs.items[d.active]->qualified, "Levels.Second"));
    TIDE_CHECK(d.prog->startup == d.prefabs.items[d.active]);
    TIDE_CHECK(d.nodes.count == 2 && str_eq_c(d.nodes.items[1].path, "Kid"));
    TIDE_CHECK(strstr(d.message, "Levels.Second") != NULL);
    TIDE_CHECK(editor_set_startup(&d));
    disk_is(f.startup, "settings {\n    startScene = Levels.Second;\n}\n");
    TIDE_CHECK(str_eq_c(d.prefabs.items[d.active]->qualified, "Levels.Second"));
    editor_close(&d); release(&f);
}

TIDE_TEST(editor_startup_replaces_only_rhs_with_bom_crlf_and_comments)
{
    const char *code = "\xEF\xBB\xBFsettings { startScene = /* leading */ Main /* trailing h\xC3\xA9llo */  ; } // after\r\n"
                       "scene Main;\r\n";
    const char *expected = "\xEF\xBB\xBFsettings { startScene = /* leading */ Levels.Second /* trailing h\xC3\xA9llo */  ; } // after\r\n"
                           "scene Main;\r\n";
    startup_fixture f = fixture("bom-replace", code, startup_tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, f.file, NULL)); TIDE_REQUIRE(second(&d));
    TIDE_REQUIRE(editor_set_startup(&d)); unchanged(&f, expected, startup_tree);
    TIDE_CHECK(d.errors == 0);
    editor_close(&d); release(&f);
}

TIDE_TEST(editor_startup_inserts_into_namespaced_bom_settings)
{
    const char *code = "\xEF\xBB\xBFnamespace Config { settings { tickRate = 30; /* keep */ } }\r\nscene Main;\r\n";
    const char *expected = "\xEF\xBB\xBFnamespace Config { settings { tickRate = 30; /* keep */ \r\n"
                           "    startScene = Levels.Second;\r\n} }\r\nscene Main;\r\n";
    startup_fixture f = fixture("namespace-insert", code, startup_tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, f.file, NULL)); TIDE_REQUIRE(second(&d));
    TIDE_REQUIRE(editor_set_startup(&d)); unchanged(&f, expected, startup_tree);
    TIDE_CHECK(d.errors == 0 && d.prog->startup == d.prefabs.items[d.active]);
    editor_close(&d); release(&f);
}

TIDE_TEST(editor_startup_refuses_ambiguous_settings_and_duplicate_fields)
{
    const char *codes[] = {
        "scene Main;\nsettings { tickRate = 30; }\nsettings { }\n",
        "scene Main;\nsettings { startScene = Main; startScene = Main; }\n",
        "scene Main;\nsettings { startScene = Main; }\nsettings { startScene = Main; }\n"
    };
    const char *names[] = {"ambiguous", "duplicate-field", "duplicate-block"};
    for (size_t i = 0; i < sizeof codes / sizeof *codes; i++) {
        startup_fixture f = fixture(names[i], codes[i], startup_tree); editor_document d;
        TIDE_REQUIRE(editor_open(&d, f.file, NULL));
        TIDE_CHECK(!editor_set_startup(&d)); TIDE_CHECK(d.message[0] != '\0');
        unchanged(&f, codes[i], startup_tree);
        editor_close(&d); release(&f);
    }
}

TIDE_TEST(editor_startup_duplicates_across_sources_do_not_edit_either)
{
    const char *code = "scene Main;\nsettings { startScene = Main; }\n";
    const char *extra = "namespace Config;\nsettings { startScene = Main; }\n";
    startup_fixture f = fixture("duplicate-sources", code, startup_tree);
    char *other = path_join(f.root, "other.tide"); TIDE_REQUIRE(sys_write_text(other, extra));
    editor_document d; TIDE_REQUIRE(editor_open(&d, f.file, NULL));
    TIDE_CHECK(!editor_set_startup(&d)); unchanged(&f, code, startup_tree); disk_is(other, extra);
    editor_close(&d); free(other); release(&f);
}

TIDE_TEST(editor_startup_unique_field_selects_its_namespaced_settings_block)
{
    const char *code = "scene Main;\nsettings { tickRate = 30; }\nnamespace Config { settings { startScene = Main /* keep */; } }\n";
    const char *expected = "scene Main;\nsettings { tickRate = 30; }\nnamespace Config { settings { startScene = Levels.Second /* keep */; } }\n";
    startup_fixture f = fixture("unique-target", code, startup_tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, f.file, NULL)); TIDE_REQUIRE(second(&d));
    TIDE_REQUIRE(editor_set_startup(&d)); unchanged(&f, expected, startup_tree);
    TIDE_CHECK(d.prog->startup == d.prefabs.items[d.active]);
    editor_close(&d); release(&f);
}

TIDE_TEST(editor_startup_refuses_target_source_syntax_without_mutating_analysis)
{
    const char *codes[] = {
        "scene Main;\nsettings { startScene = Main; }\ncomponent Broken { int value = ; }\n",
        "scene Main;\nsettings { tickRate = 30; }\n$\n",
        "scene Main;\nsettings { startScene = Main;\n"
    };
    const char *names[] = {"parse-error", "lex-error", "unclosed-settings"};
    for (size_t i = 0; i < sizeof codes / sizeof *codes; i++) {
        startup_fixture f = fixture(names[i], codes[i], startup_tree); editor_document d;
        TIDE_REQUIRE(editor_open(&d, f.file, NULL));
        TIDE_REQUIRE(d.errors > 0 && !d.syntax_errors);
        program *prog = d.prog; const int errors = d.errors, diagnostics = d.diagnostics.count;
        const int compiler_errors = diag_error_count(), sources = diag_source_count();
        for (int attempt = 0; attempt < 2; attempt++) {
            TIDE_CHECK(!editor_set_startup(&d)); unchanged(&f, codes[i], startup_tree);
            TIDE_CHECK(d.prog == prog && d.errors == errors && d.diagnostics.count == diagnostics);
            TIDE_CHECK(diag_error_count() == compiler_errors && diag_source_count() == sources);
        }
        TIDE_REQUIRE(sys_write_text(f.script, "scene Main;\nsettings { startScene = Main; }\n"));
        editor_refresh(&d); TIDE_REQUIRE(d.errors == 0);
        TIDE_REQUIRE(editor_set_startup(&d));
        unchanged(&f, "scene Main;\nsettings { startScene = Levels.First; }\n", startup_tree);
        editor_close(&d); release(&f);
    }
}

TIDE_TEST(editor_startup_refuses_active_file_syntax_but_allows_unrelated_source_errors)
{
    const char *code = "scene Main;\nsettings { startScene = Main; }\n";
    const char *broken = "prefab First { Transform; }\nprefab Broken { Transform { localPosition = }; }\n";
    startup_fixture f = fixture("active-syntax", code, broken); editor_document d;
    TIDE_REQUIRE(editor_open(&d, f.file, NULL)); TIDE_REQUIRE(d.syntax_errors);
    TIDE_CHECK(!editor_set_startup(&d)); unchanged(&f, code, broken);
    editor_close(&d); release(&f);

    f = fixture("other-syntax", code, startup_tree);
    char *other = path_join(f.root, "other.tide");
    const char *bad = "component Broken { int value = ; }\n";
    TIDE_REQUIRE(sys_write_text(other, bad)); TIDE_REQUIRE(editor_open(&d, f.file, NULL));
    TIDE_REQUIRE(d.errors > 0 && !d.syntax_errors); TIDE_REQUIRE(second(&d));
    TIDE_REQUIRE(editor_set_startup(&d));
    unchanged(&f, "scene Main;\nsettings { startScene = Levels.Second; }\n", startup_tree); disk_is(other, bad);
    editor_close(&d); free(other); release(&f);
}

TIDE_TEST(editor_startup_refuses_external_settings_changes_including_appended_nul_bytes)
{
    const char *code = "scene Main;\nsettings { startScene = Main; }\n";
    const char outside[] = "scene Main;\nsettings { startScene = Main; }\n\0outside edit";
    startup_fixture f = fixture("external-settings", code, startup_tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, f.file, NULL));
    TIDE_REQUIRE(write_bytes(f.script, outside, sizeof outside - 1));
    TIDE_CHECK(!editor_set_startup(&d));
    disk_bytes(f.script, outside, sizeof outside - 1); disk_is(f.file, startup_tree);
    TIDE_CHECK(!sys_exists(f.startup));
    editor_close(&d); release(&f);
}

TIDE_TEST(editor_startup_refuses_external_active_scene_and_unsaved_or_conflicting_edits)
{
    const char *code = "scene Main;\nsettings { startScene = Main; }\n";
    const char *outside = "// outside\nprefab Changed { Transform; }\n";
    startup_fixture f = fixture("active-state", code, startup_tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, f.file, NULL));
    TIDE_REQUIRE(sys_write_text(f.file, outside));
    TIDE_CHECK(!editor_set_startup(&d)); unchanged(&f, code, outside);
    TIDE_REQUIRE(sys_write_text(f.file, startup_tree));
    d.conflict = true; TIDE_CHECK(!editor_set_startup(&d)); d.conflict = false;
    TIDE_REQUIRE(editor_set_field(&d, 0, str_from("Transform"), str_from("localPosition"), "float3(1,2,3)"));
    TIDE_CHECK(editor_dirty(&d)); TIDE_CHECK(!editor_set_startup(&d)); unchanged(&f, code, startup_tree);
    editor_close(&d); release(&f);
}

TIDE_TEST(editor_startup_exclusive_creation_keeps_a_file_created_after_analysis)
{
    const char *code = "scene Main;\n";
    const char *outside = "// another editor owns this file\n";
    startup_fixture f = fixture("exclusive-create", code, startup_tree); editor_document d;
    TIDE_REQUIRE(editor_open(&d, f.file, NULL)); TIDE_REQUIRE(d.prog->settings.count == 0);
    TIDE_REQUIRE(sys_write_text(f.startup, outside));
    TIDE_CHECK(!editor_set_startup(&d)); disk_is(f.startup, outside);
    disk_is(f.script, code); disk_is(f.file, startup_tree);
    TIDE_CHECK(sys_remove(f.startup)); TIDE_CHECK(editor_set_startup(&d));
    disk_is(f.startup, "settings {\n    startScene = Levels.First;\n}\n");
    editor_close(&d); release(&f);
}

TIDE_TEST(editor_startup_containment_normalizes_project_slashes_and_trailing_separator)
{
    const char *code = "scene Main;\nsettings { startScene = Main; }\n";
    startup_fixture f = fixture("containment", code, startup_tree); editor_document d;
    char *spelling = path_join(f.root, "");
#ifdef _WIN32
    for (char *p = spelling; *p; p++) if (*p == '/') *p = '\\';
#endif
    TIDE_REQUIRE(editor_open(&d, f.file, spelling)); free(spelling); TIDE_REQUIRE(second(&d));
    spelling = path_join(d.project, ""); free(d.project); d.project = spelling;
    TIDE_REQUIRE(editor_set_startup(&d));
    unchanged(&f, "scene Main;\nsettings { startScene = Levels.Second; }\n", startup_tree);
    editor_close(&d); release(&f);
}

TIDE_TEST(editor_startup_active_tide_source_updates_saved_baseline_and_selection)
{
    const char *code = "scene Main;\nsettings { startScene = Main; }\n"
        "prefab First { Transform; }\nprefab Second { Transform; entity Kid { Transform; } }\n";
    const char *expected = "scene Main;\nsettings { startScene = Second; }\n"
        "prefab First { Transform; }\nprefab Second { Transform { localPosition = float3(1,2,3) }; entity Kid { Transform; } }\n";
    startup_fixture f = fixture("active-tide-source", code, "prefab Marker {}\n"); editor_document d;
    TIDE_REQUIRE(editor_open(&d, f.script, NULL)); TIDE_REQUIRE(d.prefabs.count == 2);
    editor_select_prefab(&d, 1); TIDE_REQUIRE(str_eq_c(d.prefabs.items[d.active]->qualified, "Second"));
    TIDE_REQUIRE(editor_set_field(&d, 0, str_from("Transform"), str_from("localPosition"), "float3(1,2,3)"));
    TIDE_REQUIRE(editor_save(&d)); TIDE_REQUIRE(d.undo_count == 1);
    TIDE_REQUIRE(editor_set_startup(&d));
    disk_is(f.script, expected); disk_is(f.file, "prefab Marker {}\n");
    TIDE_CHECK(strcmp(d.text, expected) == 0 && strcmp(d.saved, expected) == 0);
    TIDE_CHECK(!editor_dirty(&d) && !d.conflict && d.errors == 0);
    TIDE_CHECK(d.undo_count == 0 && d.redo_count == 0);
    TIDE_CHECK(str_eq_c(d.prefabs.items[d.active]->qualified, "Second") && d.prog->startup == d.prefabs.items[d.active]);
    TIDE_CHECK(d.nodes.count == 2 && str_eq_c(d.nodes.items[1].path, "Kid"));
    TIDE_REQUIRE(editor_save(&d)); TIDE_CHECK(!d.conflict && !editor_dirty(&d)); disk_is(f.script, expected);
    TIDE_REQUIRE(editor_set_startup(&d)); TIDE_CHECK(d.prog->startup == d.prefabs.items[d.active]);
    disk_is(f.script, expected);
    editor_close(&d); release(&f);
}

TIDE_TEST(editor_startup_refuses_global_prefab_shadowed_by_settings_namespace)
{
    const char *codes[] = {
        "scene Main;\nnamespace Config { settings { startScene = Config.Start; } }\n",
        "scene Main;\nnamespace Config { settings { tickRate = 30; } }\n",
        "scene Main;\nnamespace Config.Inner { settings { startScene = Config.Start; } }\n"
    };
    const char *names[] = {"shadow-replace", "shadow-insert", "shadow-parent"};
    const char *prefab = "prefab Start { Transform; }\nnamespace Config { prefab Start { Transform; } }\n";
    for (size_t i = 0; i < sizeof codes / sizeof *codes; i++) {
        startup_fixture f = fixture(names[i], codes[i], prefab); editor_document d;
        TIDE_REQUIRE(editor_open(&d, f.file, NULL)); TIDE_REQUIRE(d.errors == 0);
        TIDE_REQUIRE(str_eq_c(d.prefabs.items[d.active]->qualified, "Start"));
        program *before = d.prog;
        const int errors = diag_error_count(), sources = diag_source_count();
        TIDE_CHECK(!editor_set_startup(&d)); unchanged(&f, codes[i], prefab);
        TIDE_CHECK(strstr(d.message, "global scope") != NULL);
        TIDE_CHECK(d.prog == before && diag_error_count() == errors && diag_source_count() == sources);
        TIDE_CHECK(!editor_dirty(&d) && !d.conflict);
        editor_close(&d); release(&f);
    }
}

TIDE_TEST(editor_startup_global_prefab_outranks_namespaced_scene_and_struct)
{
    const char *codes[] = {
        "scene Main;\nnamespace Config { scene Start; settings { startScene = Main; } }\n",
        "scene Main;\nnamespace Config { struct Start { int value; } settings { startScene = Main; } }\n"
    };
    const char *expected[] = {
        "scene Main;\nnamespace Config { scene Start; settings { startScene = Start; } }\n",
        "scene Main;\nnamespace Config { struct Start { int value; } settings { startScene = Start; } }\n"
    };
    const char *names[] = {"scene-kind-precedence", "struct-kind-precedence"};
    const char *prefab = "prefab Start { Transform; }\n";
    for (size_t i = 0; i < sizeof codes / sizeof *codes; i++) {
        startup_fixture f = fixture(names[i], codes[i], prefab); editor_document d;
        TIDE_REQUIRE(editor_open(&d, f.file, NULL)); TIDE_REQUIRE(d.errors == 0);
        TIDE_REQUIRE(editor_set_startup(&d)); unchanged(&f, expected[i], prefab);
        TIDE_CHECK(d.errors == 0 && d.prog->startup == d.prefabs.items[d.active]);
        editor_close(&d); release(&f);
    }
}

TIDE_TEST(editor_startup_preserves_pending_active_source_recovery)
{
    const char *code = "scene Main;\nsettings { startScene = Main; }\nprefab First { Transform; }\n";
    startup_fixture f = fixture("pending-active-recovery", code, "prefab Marker {}\n"); editor_document d;
    TIDE_REQUIRE(editor_open(&d, f.script, NULL));
    TIDE_REQUIRE(editor_set_field(&d, 0, str_from("Transform"), str_from("localPosition"), "float3(1,2,3)"));
    editor_close(&d);
    TIDE_REQUIRE(editor_open(&d, f.script, NULL)); TIDE_REQUIRE(d.recovery != NULL);
    char *draft = sys_read_file(d.draft_path, NULL); TIDE_REQUIRE(draft);
    program *before = d.prog;
    TIDE_CHECK(!editor_set_startup(&d));
    TIDE_CHECK(d.prog == before && d.recovery != NULL && !editor_dirty(&d));
    disk_is(d.draft_path, draft); unchanged(&f, code, "prefab Marker {}\n");
    TIDE_REQUIRE(editor_recover(&d, false)); TIDE_REQUIRE(editor_set_startup(&d));
    TIDE_CHECK(!editor_dirty(&d) && d.prog->startup == d.prefabs.items[d.active]);
    free(draft); editor_close(&d); release(&f);
}

TIDE_TEST(editor_startup_unity_folder_alias_uses_unique_descendant_only)
{
    const char *code = "scene Main;\nnamespace UnityEngine.Prefabs.Config { settings { startScene = Main; } }\n";
    const char *prefabs[] = {
        "prefab Start { Transform; }\nnamespace UnityEngine.Prefabs.A { prefab Start { Transform; } }\n",
        "prefab Start { Transform; }\nnamespace UnityEngine.Prefabs.A { prefab Start { Transform; } }\nnamespace UnityEngine.Prefabs.B { prefab Start { Transform; } }\n"
    };
    const char *names[] = {"unique-folder-shadow", "ambiguous-folder-fallback"};
    for (size_t i = 0; i < sizeof prefabs / sizeof *prefabs; i++) {
        startup_fixture f = fixture(names[i], code, prefabs[i]); editor_document d;
        TIDE_REQUIRE(editor_open(&d, f.file, NULL)); TIDE_REQUIRE(d.errors == 0);
        TIDE_REQUIRE(str_eq_c(d.prefabs.items[d.active]->qualified, "Start"));
        if (i == 0) { TIDE_CHECK(!editor_set_startup(&d)); unchanged(&f, code, prefabs[i]); }
        else {
            TIDE_REQUIRE(editor_set_startup(&d));
            unchanged(&f, "scene Main;\nnamespace UnityEngine.Prefabs.Config { settings { startScene = Start; } }\n", prefabs[i]);
            TIDE_CHECK(d.errors == 0 && d.prog->startup == d.prefabs.items[d.active]);
        }
        editor_close(&d); release(&f);
    }
}
