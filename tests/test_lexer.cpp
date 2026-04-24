/**
 * @file test_lexer.cpp
 * @brief Tests de validacion del lexer del preprocesador vpp.
 */

#include "preprocessor/pp_lexer.h"
#include "preprocessor/pp_diagnostics.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace vpp;

/* --- utilidades de test --------------------------------------------------- */

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::cerr << "FALLO en " << __FILE__ << ":" << __LINE__ << '\n'; \
        ++tests_failed; \
    } else { ++tests_passed; } \
} while(0)

#define ASSERT_EQ_TYPE(a, b) do { \
    if ((a) != (b)) { \
        std::cerr << "FALLO en " << __FILE__ << ":" << __LINE__ \
                  << " -> esperado=" << vpp::pp_token_type_name(b) \
                  << " obtenido=" << vpp::pp_token_type_name(a) << '\n'; \
        ++tests_failed; \
    } else { ++tests_passed; } \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::cerr << "FALLO en " << __FILE__ << ":" << __LINE__ \
                  << " -> expresion falsa: " #expr "\n"; \
        ++tests_failed; \
    } else { ++tests_passed; } \
} while(0)

static std::vector<PPToken> lex(const std::string& src) {
    DiagnosticEngine diag;
    PPLexer l(src, "<test>", diag);
    return l.tokenize();
}

/* --- tests ---------------------------------------------------------------- */

static void test_empty_source() {
    auto toks = lex("");
    ASSERT_EQ(toks.size(), 1u);
    ASSERT_TRUE(toks[0].is_eof());
}

static void test_simple_directive_define() {
    auto toks = lex("#define FOO 42\n");
    // esperado: HASH, IDENT("define"), [WHITESPACE], IDENT("FOO"), [WHITESPACE], NUMBER("42"), NEWLINE, PP_EOF
    // el lexer emite WHITESPACE explicitamente en directivas para preservar cuerpos de macros
    ASSERT_EQ_TYPE(toks[0].type, PPTokenType::HASH);
    ASSERT_EQ_TYPE(toks[1].type, PPTokenType::IDENT);
    ASSERT_EQ(toks[1].value, std::string("define"));
    // buscar IDENT "FOO" y NUMBER "42" en el vector (puede haber WHITESPACE entre ellos)
    bool found_foo = false, found_number = false;
    for (const auto& t : toks) {
        if (t.type == PPTokenType::IDENT && t.value == "FOO") found_foo = true;
        if (t.type == PPTokenType::NUMBER && t.value == "42") found_number = true;
    }
    ASSERT_TRUE(found_foo);
    ASSERT_TRUE(found_number);
}

static void test_text_line_preserves_content() {
    auto toks = lex("hola mundo\n");
    // hola y mundo son IDENT; " " y "\n" son TEXT o NEWLINE
    bool found_hola  = false;
    bool found_mundo = false;
    for (const auto& t : toks) {
        if (t.type == PPTokenType::IDENT && t.value == "hola")  found_hola  = true;
        if (t.type == PPTokenType::IDENT && t.value == "mundo") found_mundo = true;
    }
    ASSERT_TRUE(found_hola);
    ASSERT_TRUE(found_mundo);
}

static void test_directive_ifdef() {
    auto toks = lex("#ifdef MY_MACRO\n");
    ASSERT_EQ_TYPE(toks[0].type, PPTokenType::HASH);
    bool found = false;
    for (const auto& t : toks) {
        if (t.type == PPTokenType::IDENT && t.value == "ifdef") { found = true; break; }
    }
    ASSERT_TRUE(found);
}

static void test_string_literal_not_expanded() {
    auto toks = lex("\"hola FOO mundo\"\n");
    bool found_string = false;
    for (const auto& t : toks) {
        if (t.type == PPTokenType::STRING) { found_string = true; break; }
    }
    ASSERT_TRUE(found_string);
    // FOO no debe aparecer como IDENT separado dentro de la cadena
    for (const auto& t : toks) {
        if (t.type == PPTokenType::IDENT && t.value == "FOO") {
            ASSERT_TRUE(false); // no debe ocurrir
        }
    }
    ASSERT_TRUE(found_string);
}

static void test_line_comment_stripped() {
    auto toks = lex("#define X 1 // comentario\n");
    for (const auto& t : toks) {
        ASSERT_TRUE(t.type != PPTokenType::SLASH);
    }
}

static void test_block_comment_stripped() {
    auto toks = lex("int /* comentario */ x;\n");
    bool found_slash_star = false;
    for (const auto& t : toks) {
        if (t.value == "/*" || t.value == "*/") found_slash_star = true;
    }
    ASSERT_TRUE(!found_slash_star);
}

static void test_multiline_directive_continuation() {
    auto toks = lex("#define LONG \\\n    value\n");
    bool found_value = false;
    for (const auto& t : toks) {
        if (t.type == PPTokenType::IDENT && t.value == "value") {
            found_value = true; break;
        }
    }
    ASSERT_TRUE(found_value);
}

static void test_number_hex() {
    auto toks = lex("#define HEX 0xFF\n");
    bool found = false;
    for (const auto& t : toks) {
        if (t.type == PPTokenType::NUMBER && t.value == "0xFF") { found = true; break; }
    }
    ASSERT_TRUE(found);
}

static void test_hashhash_token() {
    auto toks = lex("#define PASTE(a,b) a##b\n");
    bool found = false;
    for (const auto& t : toks) {
        if (t.type == PPTokenType::HASHHASH) { found = true; break; }
    }
    ASSERT_TRUE(found);
}

static void test_angle_brackets_lt_gt() {
    auto toks = lex("#include <stdio.h>\n");
    bool found_lt = false;
    for (const auto& t : toks) {
        if (t.type == PPTokenType::LT) { found_lt = true; break; }
    }
    ASSERT_TRUE(found_lt);
}

static void test_source_location() {
    auto toks = lex("linea1\n#define X 1\n");
    // el HASH de la segunda linea debe estar en linea 2
    for (const auto& t : toks) {
        if (t.type == PPTokenType::HASH) {
            ASSERT_EQ(t.loc.line, 2u);
            break;
        }
    }
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests del lexer del preprocesador vpp ===\n";

    test_empty_source();
    test_simple_directive_define();
    test_text_line_preserves_content();
    test_directive_ifdef();
    test_string_literal_not_expanded();
    test_line_comment_stripped();
    test_block_comment_stripped();
    test_multiline_directive_continuation();
    test_number_hex();
    test_hashhash_token();
    test_angle_brackets_lt_gt();
    test_source_location();

    std::cout << "Resultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
