// Numeric suffixes remain part of one source token, including their spelling
// and location. A sign is an operator, and malformed suffixes never split into
// a valid number followed by an identifier.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "tide_test.h"

typedef struct lexer_diagnostics {
    int errors;
    loc first;
    char message[256];
} lexer_diagnostics;

static void capture(void *user, const diag_severity severity, const loc at, const char *message)
{
    lexer_diagnostics *out = user;
    if (severity != DIAG_ERROR) return;
    if (!out->errors) {
        out->first = at;
        snprintf(out->message, sizeof out->message, "%s", message);
    }
    out->errors++;
}

static void input(source *src, const char *text, lexer_diagnostics *out)
{
    diag_reset();
    lex_symbols_reset();
    diag_set_sink(capture, out);
    *src = (source){.path = "degrees.tide", .text = text, .len = strlen(text)};
    diag_add_source(src);
}

static void done(void)
{
    diag_set_sink(NULL, NULL);
    diag_reset();
    arena_reset();
    lex_symbols_reset();
}

TIDE_TEST(lexer_double_literals_preserve_suffixes_without_changing_float_defaults)
{
    static const char *const valid[] = {"1d", ".5D", "1.25d", "1e300D", "1_000.000_125d", "5e-324d"};
    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; i++) {
        lexer_diagnostics out = {0};
        source src;
        input(&src, valid[i], &out);
        token *tokens = lex(&src);
        TIDE_REQUIRE(tokens && !out.errors);
        TIDE_CHECK(tokens[0].kind == T_FLOAT && str_eq_c(tokens[0].text, valid[i]));
        TIDE_CHECK(tokens[1].kind == T_EOF);
        done();
    }
    static const char *const invalid[] = {"1df", "1fd", "1ddeg", "1degd", "1dL", ".5d_suffix"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        lexer_diagnostics out = {0};
        source src;
        input(&src, invalid[i], &out);
        TIDE_CHECK(!lex(&src) && out.errors > 0);
        done();
    }
}

TIDE_TEST(lexer_degree_literals_keep_the_entire_float_token)
{
    static const char *const valid[] = {"180deg", ".5deg", "0.5deg", "1e2deg", "1E+2deg", "1e-2deg", "1_8__0deg",
        "1_2.5_0e+1_0deg", ".5_0e-0_1deg", "16_777_217deg", "0deg", "1e-100deg", "3.4028234e38deg"};
    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; i++) {
        lexer_diagnostics out = {0};
        source src;
        input(&src, valid[i], &out);
        // Allocate exactly the source length: no terminator protects a suffix
        // lookahead that forgets the end of the supplied source.
        char *exact = malloc(src.len);
        TIDE_REQUIRE(exact);
        memcpy(exact, src.text, src.len);
        src.text = exact;
        token *tokens = lex(&src);
        TIDE_REQUIRE(tokens);
        TIDE_CHECK(out.errors == 0 && diag_error_count() == 0);
        TIDE_CHECK(tokens[0].kind == T_FLOAT && tokens[0].text.ptr == exact);
        TIDE_CHECK(tokens[0].text.len == (int)src.len && str_eq_c(tokens[0].text, valid[i]));
        TIDE_CHECK(tokens[0].at.line == 1 && tokens[0].at.col == 1 && tokens[0].at.file == src.file);
        TIDE_CHECK(tokens[1].kind == T_EOF && tokens[1].text.ptr == exact + src.len && tokens[1].text.len == 0);
        TIDE_CHECK(tokens[1].at.line == 1 && tokens[1].at.col == (int)src.len + 1);
        done();
        free(exact);
    }
}

TIDE_TEST(lexer_degree_tokens_preserve_unary_minus_operators_names_and_locations)
{
    static const char text[] = "float angle = -180deg + .5deg;\nvar deg = 1e2deg;\n";
    static const struct { tok_kind kind; const char *text; int offset, line, column; } expected[] = {
        {T_IDENT, "float", 0, 1, 1}, {T_IDENT, "angle", 6, 1, 7}, {T_ASSIGN, "=", 12, 1, 13},
        {T_MINUS, "-", 14, 1, 15}, {T_FLOAT, "180deg", 15, 1, 16}, {T_PLUS, "+", 22, 1, 23},
        {T_FLOAT, ".5deg", 24, 1, 25}, {T_SEMI, ";", 29, 1, 30}, {T_VAR, "var", 31, 2, 1},
        {T_IDENT, "deg", 35, 2, 5}, {T_ASSIGN, "=", 39, 2, 9}, {T_FLOAT, "1e2deg", 41, 2, 11},
        {T_SEMI, ";", 47, 2, 17}, {T_EOF, "", 49, 3, 1},
    };
    lexer_diagnostics out = {0};
    source src;
    input(&src, text, &out);
    token *tokens = lex(&src);
    TIDE_REQUIRE(tokens);
    TIDE_CHECK(out.errors == 0);
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        TIDE_CHECK(tokens[i].kind == expected[i].kind && str_eq_c(tokens[i].text, expected[i].text));
        TIDE_CHECK(tokens[i].text.ptr == text + expected[i].offset);
        TIDE_CHECK(tokens[i].at.line == expected[i].line && tokens[i].at.col == expected[i].column && tokens[i].at.file == src.file);
    }
    done();
}

// A build's symbols with values (--define SIDE=50) and a file's own, as code
// reads them (defined(NAME), define(NAME, default)): the file's first, the
// build's noted with their values for the game's identity.
TIDE_TEST(lexer_symbols_with_values_are_read_by_file_and_noted)
{
    lexer_diagnostics out = {0};
    source src;
    input(&src, "#define OWN\n#undef SIDE\n#if SIDE\nleft out\n#endif\nint x;\n", &out);
    TIDE_CHECK(lex_define_ok("ARMY=") && lex_define_ok("TITLE=a b=c") && lex_define_ok("FAST"));
    TIDE_CHECK(!lex_define_ok("=5") && !lex_define_ok("") && !lex_define_ok("TIDE_X=1") && !lex_define_ok("PACKAGE_A=1"));
    TIDE_CHECK(!lex_define_ok("1A=2") && !lex_define_ok("true=1") && !lex_define_ok("A B=1"));
    TIDE_CHECK(lex_define_name_length("TITLE=a=b") == 5 && lex_define_name_length("FAST") == 4);
    lex_define("SIDE=50");
    lex_define("FAST");
    lex_define("TITLE=a=b");
    lex_define("SIDE=60"); // Said again: the later value
    lex_define("EMPTY=");
    token *tokens = lex(&src);
    TIDE_REQUIRE(tokens && !out.errors);
    TIDE_CHECK(tokens[0].kind == T_IDENT && str_eq_c(tokens[0].text, "int")); // #if SIDE left its line out, #undef said
    const char *value = "unset";
    bool own = false;
    TIDE_CHECK(lex_read_symbol(str_from("OWN"), src.file, &value, &own) == 1 && !value && own); // The file's own: no value
    TIDE_CHECK(lex_read_symbol(str_from("SIDE"), src.file, &value, &own) == 0 && !value && own); // ...and its #undef
    TIDE_CHECK(lex_read_symbol(str_from("OWN"), src.file + 1, &value, &own) == 0 && !own);       // Another file has neither
    TIDE_CHECK(lex_read_symbol(str_from("SIDE"), src.file + 1, &value, &own) == 1 && value && strcmp(value, "60") == 0 && !own);
    TIDE_CHECK(lex_read_symbol(str_from("TITLE"), src.file, &value, &own) == 1 && value && strcmp(value, "a=b") == 0);
    TIDE_CHECK(lex_read_symbol(str_from("EMPTY"), src.file, &value, &own) == 1 && value && strcmp(value, "") == 0);
    TIDE_CHECK(lex_read_symbol(str_from("FAST"), src.file, &value, &own) == 1 && !value);
    TIDE_CHECK(lex_read_symbol(str_from("NONE"), src.file, &value, &own) == 0 && !value);
    TIDE_CHECK(lex_read_symbol(str_from("TIDE_0_1_OR_NEWER"), src.file, &value, &own) == 1);
    TIDE_CHECK(lex_read_symbol(str_from("TIDE_0_1"), src.file, &value, &own) == -1);
    // What the game's identity lists: the build's, by name, with values; not the file's own, nor versions
    int count = 0;
    const lex_symbol *read = lex_symbols_read(&count);
    TIDE_REQUIRE(count == 6);
    TIDE_CHECK(strcmp(read[0].name, "EMPTY") == 0 && read[0].defined && read[0].value && !read[0].value[0]);
    TIDE_CHECK(strcmp(read[1].name, "FAST") == 0 && read[1].defined && !read[1].value);
    TIDE_CHECK(strcmp(read[2].name, "NONE") == 0 && !read[2].defined && !read[2].value);
    TIDE_CHECK(strcmp(read[3].name, "OWN") == 0 && !read[3].defined); // Read in the other file, where it's the build's
    TIDE_CHECK(strcmp(read[4].name, "SIDE") == 0 && read[4].defined && strcmp(read[4].value, "60") == 0);
    TIDE_CHECK(strcmp(read[5].name, "TITLE") == 0 && read[5].defined && strcmp(read[5].value, "a=b") == 0);
    // Lexed again, a file says its symbols again
    source again = src;
    again.text = "int y;\n";
    again.len = strlen(again.text);
    TIDE_REQUIRE(lex(&again));
    TIDE_CHECK(lex_read_symbol(str_from("OWN"), src.file, &value, &own) == 0);
    done();
}

TIDE_TEST(lexer_degree_suffixes_reject_combined_base_and_identifier_continuations)
{
    static const char *const invalid[] = {"180fdeg", "180Fdeg", "180Ldeg", "180ldeg", "180degf", "180degF", "180degL",
        "180degl", "0x10deg", "0b10deg", "180degrees", "180deg2", "180deg_", "180_deg", "180DEG", "180Deg",
        "1e2_deg", ".5degx", "1e_deg", "1e+deg"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        lexer_diagnostics out = {0};
        source src;
        input(&src, invalid[i], &out);
        TIDE_CHECK(lex(&src) == NULL);
        TIDE_CHECK(out.errors > 0 && diag_error_count() == out.errors);
        TIDE_CHECK(strncmp(out.message, "invalid number '", 16) == 0);
        TIDE_CHECK(out.first.line == 1 && out.first.col == 1 && out.first.file == src.file);
        done();
    }
    lexer_diagnostics out = {0};
    source src;
    input(&src, "180degf; 90deg", &out);
    token *tokens = lex_all(&src);
    TIDE_REQUIRE(tokens);
    TIDE_CHECK(out.errors == 1);
    TIDE_CHECK(tokens[0].kind == T_SEMI && tokens[0].at.col == 8);
    TIDE_CHECK(tokens[1].kind == T_FLOAT && str_eq_c(tokens[1].text, "90deg") && tokens[1].at.col == 10);
    TIDE_CHECK(tokens[2].kind == T_EOF && tokens[2].at.col == 15);
    done();
}
