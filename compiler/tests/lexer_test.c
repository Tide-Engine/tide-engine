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
