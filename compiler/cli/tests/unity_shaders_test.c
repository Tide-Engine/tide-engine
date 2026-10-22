// The Unity shaders tide unity writes (unity_shaders.h): the renderer's and the game's, each named by its SPIR-V's
// hash, the web's keys for them, and those of shaders gone removed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys.h"
#include "tide_test.h"
#include "unity_shaders.h"

static const char game_wgsl[] =
    "struct Uniforms { color: vec4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
    "    let corner = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
    "    return vec4f(corner * 2.0 - 1.0, 0.5, 1.0);\n"
    "}\n"
    "@fragment fn fragment() -> @location(0) vec4f {\n"
    "    return u.color;\n"
    "}\n";

typedef struct listed {
    int shaders, metas;
    bool stale;
} listed;

// The lines of the web's keys, each a WGSL's key and a file's.
static int key_lines(const char *shaders)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/web-keys.txt", shaders);
    char *text = sys_read_file(path, NULL);
    int lines = 0;
    for (const char *p = text; p && *p; p++) lines += *p == '\n';
    free(text);
    return lines;
}

static void count(void *user, const char *name, const bool is_dir)
{
    listed *l = user;
    if (is_dir) return;
    const size_t n = strlen(name);
    if (n > 7 && strcmp(name + n - 7, ".shader") == 0) l->shaders++;
    if (n > 5 && strcmp(name + n - 5, ".meta") == 0) l->metas++;
    if (strncmp(name, "0000000000000000", 16) == 0) l->stale = true;
}

TIDE_TEST(unity_shaders_written)
{
    const uint64_t key = unity_shader_key(game_wgsl);
    TIDE_CHECK(key != 0 && key == unity_shader_key(game_wgsl));
    TIDE_CHECK(unity_shader_key("not WGSL") == 0);

    const char *base = sys_env("TEMP") ? sys_env("TEMP") : sys_env("TMPDIR") ? sys_env("TMPDIR") : "/tmp";
    char dir[1024], shaders[1024], stale[1024], path[1200];
    snprintf(dir, sizeof dir, "%s/tide_unity_shaders_test", base);
    snprintf(shaders, sizeof shaders, "%s/Resources/TideShaders", dir);
    sys_remove_tree(dir);
    TIDE_REQUIRE(sys_mkdirs(shaders));
    // One a build before wrote, and its meta, which go
    snprintf(stale, sizeof stale, "%s/0000000000000000.shader", shaders);
    TIDE_REQUIRE(sys_write_text(stale, "old"));
    snprintf(stale, sizeof stale, "%s/0000000000000000.shader.meta", shaders);
    TIDE_REQUIRE(sys_write_text(stale, "old"));

    char game[1024];
    const size_t name_size = strlen("Fill on Corner") + 1;
    memcpy(game, "Fill on Corner", name_size);
    memcpy(game + name_size, game_wgsl, sizeof game_wgsl);
    TIDE_REQUIRE(unity_shaders_write(dir, game, name_size + sizeof game_wgsl));
    listed l = {0};
    sys_list(shaders, count, &l);
    TIDE_CHECK(l.shaders == 11); // The renderer's 10, and the game's
    TIDE_CHECK(l.metas == 0 && !l.stale);
    TIDE_CHECK(key_lines(shaders) == 11);

    snprintf(path, sizeof path, "%s/%016llx.shader", shaders, (unsigned long long)key);
    char *text = sys_read_file(path, NULL);
    TIDE_REQUIRE(text != NULL);
    char title[64];
    snprintf(title, sizeof title, "Shader \"Hidden/Tide/%016llx\"", (unsigned long long)key);
    TIDE_CHECK(strncmp(text, "// Fill on Corner\n", 18) == 0);
    TIDE_CHECK(strstr(text, title) != NULL);
    TIDE_CHECK(strstr(text, "#pragma vertex tidehl_vertex") != NULL);
    TIDE_CHECK(strstr(text, "cbuffer TideUniforms") != NULL);
    free(text);

    // The game's shader gone, its file goes too
    TIDE_REQUIRE(unity_shaders_write(dir, NULL, 0));
    l = (listed){0};
    sys_list(shaders, count, &l);
    TIDE_CHECK(l.shaders == 10);
    TIDE_CHECK(!sys_exists(path));
    TIDE_CHECK(key_lines(shaders) == 10);
    sys_remove_tree(dir);
}
