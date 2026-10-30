#include "shader_names.h"
#include "tide_test.h"

#include <stdio.h>
#include <string.h>

TIDE_TEST(shader_names_preserve_bindings_builtins_numbers_and_identifier_boundaries)
{
    static const char source[] =
        "// tf_Secret\nstruct tt_Secret { float m_secret; };\n"
        "float q0 = 1e-3; float q1 = 0x1.fp+2;\n"
        "float tf_Secret(float t_secret) { return t_secret + q0 + q1; }\n"
        "void main() { tt_Secret t_value; t_value.m_secret = tf_Secret(1.0);\n"
        "gl_Position = tide_camera * vec4(t_value.m_secret); }\n";
    sb map = {0};
    const char *out = shader_minify_names(source, &map);
    TIDE_CHECK(strstr(out, "Secret") == NULL && strstr(out, "t_secret") == NULL);
    TIDE_CHECK(strstr(out, "float q0 = 1e-3; float q1 = 0x1.fp+2;") != NULL);
    TIDE_CHECK(strstr(out, "void main()") != NULL && strstr(out, "gl_Position = tide_camera * vec4(") != NULL);
    TIDE_CHECK(strstr(map.data, "q0\t") == NULL && strstr(map.data, "q1\t") == NULL);
    TIDE_CHECK(strstr(map.data, "\ttf_Secret\n") != NULL && strstr(map.data, "\tm_secret\n") != NULL);
    TIDE_CHECK(strcmp(out, shader_minify_names(source, NULL)) == 0);
    TIDE_CHECK(strcmp(out, shader_minify_names(out, NULL)) == 0);
    arena_reset();
}

TIDE_TEST(shader_names_skip_strings_remove_nested_comments_and_preserve_token_separation)
{
    const char *out = shader_minify_names("t_secret/* secret /* nested */ secret */+ +t_secret;\n"
        "\"t_secret\\\"m_secret\" // t_secret\n@vertex fn vertex() {} @fragment fn fragment() {}", NULL);
    TIDE_CHECK(strstr(out, "q0 + +q0;") != NULL);
    TIDE_CHECK(strstr(out, "\"t_secret\\\"m_secret\"") != NULL);
    TIDE_CHECK(strstr(out, "nested") == NULL);
    TIDE_CHECK(strstr(out, "@vertex fn vertex() {} @fragment fn fragment() {}") != NULL);
    TIDE_CHECK(strcmp(shader_minify_names("", NULL), "") == 0);
    arena_reset();
}

TIDE_TEST(shader_names_grow_the_lookup_without_alias_collisions)
{
    sb source = {0}, map = {0};
    for (int i = 0; i < 4096; i++) sb_printf(&source, "float t_secret%d = %d.0;\n", i, i);
    const char *out = shader_minify_names(source.data, &map);
    TIDE_CHECK(strstr(out, "secret") == NULL);
    TIDE_CHECK(strstr(out, "float q4095 = 4095.0;") != NULL);
    TIDE_CHECK(strstr(map.data, "q4095\tt_secret4095\n") != NULL);
    arena_reset();
}
