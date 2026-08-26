/**
 * @file pp_evaluator.cpp
 * @brief Implementacion del evaluador de expresiones constantes del preprocesador vpp.
 */

#include "preprocessor/pp_evaluator.h"
#include <stdexcept>
#include <climits>

namespace vpp {

static const PPToken EOF_TOKEN(PPTokenType::PP_EOF, "", {});

PPEvaluator::PPEvaluator(MacroTable& macros, DiagnosticEngine& diag)
    : m_macros(macros), m_diag(diag), m_pos(0)
{}

std::vector<PPToken> PPEvaluator::resolve_defined(
        const std::vector<PPToken>& tokens) const {
    std::vector<PPToken> out;
    out.reserve(tokens.size());

    for (size_t i = 0; i < tokens.size(); ++i) {
        const PPToken& t = tokens[i];

        // solo interesa el operador `defined`; el resto pasa tal cual
        if (t.type != PPTokenType::IDENT || t.value != "defined") {
            out.push_back(t);
            continue;
        }

        // saltar el espacio entre `defined` y su operando
        size_t j = i + 1;
        auto skip_ws = [&tokens](size_t k) {
            while (k < tokens.size() &&
                   (tokens[k].type == PPTokenType::WHITESPACE ||
                    tokens[k].type == PPTokenType::NEWLINE)) {
                ++k;
            }
            return k;
        };
        j = skip_ws(j);

        bool     paren = false;
        if (j < tokens.size() && tokens[j].type == PPTokenType::LPAREN) {
            paren = true;
            j = skip_ws(j + 1);
        }

        if (j >= tokens.size() || tokens[j].type != PPTokenType::IDENT) {
            // Malformado.  Se deja `defined` en el stream para que el parser
            // emita el error con su ubicacion, en lugar de inventar un valor.
            out.push_back(t);
            continue;
        }

        const std::string name = tokens[j].value;
        j = skip_ws(j + 1);

        if (paren) {
            if (j >= tokens.size() || tokens[j].type != PPTokenType::RPAREN) {
                out.push_back(t);   // falta el ')': que lo diagnostique el parser
                continue;
            }
            ++j;
        }

        // El resultado sustituye a TODO el operador, incluido su operando: asi
        // el nombre de la macro nunca llega a la fase de expansion.
        out.emplace_back(PPTokenType::NUMBER,
                         m_macros.is_defined(name) ? "1" : "0",
                         t.loc);
        i = j - 1;
    }

    return out;
}

int64_t PPEvaluator::evaluate(const std::vector<PPToken>& tokens,
                               const SourceLocation& loc) {
    // `defined` PRIMERO, y solo despues expandir.
    //
    // El estandar de C dice que las macros de la expresion se expanden "salvo
    // las modificadas por el operador defined".  Haciendolo al reves -- que era
    // lo que habia -- `defined(A)` con A definida se convierte en `defined(1)`
    // y el operador ya no encuentra un nombre de macro.  El sintoma es que
    // `#if defined(X)` solo funcionaba cuando X NO estaba definida, justo el
    // caso que no importa, y bastaba para tumbar cualquier cabecera del
    // sistema.
    m_toks = m_macros.expand(resolve_defined(tokens), loc);
    m_pos  = 0;
    m_loc  = loc;

    // eliminar WHITESPACE y NEWLINE del stream de expresion
    std::vector<PPToken> cleaned;
    cleaned.reserve(m_toks.size());
    for (auto& t : m_toks) {
        if (t.type != PPTokenType::WHITESPACE &&
            t.type != PPTokenType::NEWLINE) {
            cleaned.push_back(std::move(t));
        }
    }
    m_toks = std::move(cleaned);
    m_pos  = 0;

    if (m_toks.empty() || m_toks[0].is_eof()) {
        m_diag.error(loc, "expresion vacia en directiva condicional");
        return 0;
    }

    int64_t val = parse_ternary();

    if (!check(PPTokenType::PP_EOF) && m_pos < m_toks.size()) {
        m_diag.warning(cur().loc,
            "tokens sobrantes tras la expresion del preprocesador");
    }
    return val;
}

/* --- acceso al flujo de tokens de expresion ------------------------------- */

const PPToken& PPEvaluator::cur() const {
    if (m_pos >= m_toks.size()) return EOF_TOKEN;
    return m_toks[m_pos];
}

PPToken PPEvaluator::consume() {
    if (m_pos >= m_toks.size()) return EOF_TOKEN;
    return std::move(m_toks[m_pos++]);
}

bool PPEvaluator::check(PPTokenType t) const {
    return cur().type == t;
}

bool PPEvaluator::match(PPTokenType t) {
    if (!check(t)) return false;
    consume();
    return true;
}

/* --- parser de expresion (descendente recursivo) -------------------------- */

int64_t PPEvaluator::parse_ternary() {
    int64_t cond = parse_or();
    if (!match(PPTokenType::QUESTION)) return cond;
    int64_t then_val = parse_ternary();
    if (!match(PPTokenType::COLON)) {
        m_diag.error(cur().loc, "se esperaba ':' en operador ternario");
        return then_val;
    }
    int64_t else_val = parse_ternary();
    return cond ? then_val : else_val;
}

int64_t PPEvaluator::parse_or() {
    int64_t lhs = parse_and();
    while (check(PPTokenType::OR)) {
        consume();
        int64_t rhs = parse_and();
        lhs = (lhs || rhs) ? 1 : 0;
    }
    return lhs;
}

int64_t PPEvaluator::parse_and() {
    int64_t lhs = parse_bitor();
    while (check(PPTokenType::AND)) {
        consume();
        int64_t rhs = parse_bitor();
        lhs = (lhs && rhs) ? 1 : 0;
    }
    return lhs;
}

int64_t PPEvaluator::parse_bitor() {
    int64_t lhs = parse_xor();
    while (check(PPTokenType::PIPE)) {
        consume();
        lhs |= parse_xor();
    }
    return lhs;
}

int64_t PPEvaluator::parse_xor() {
    int64_t lhs = parse_bitand();
    while (check(PPTokenType::CARET)) {
        consume();
        lhs ^= parse_bitand();
    }
    return lhs;
}

int64_t PPEvaluator::parse_bitand() {
    int64_t lhs = parse_equality();
    while (check(PPTokenType::AMP)) {
        consume();
        lhs &= parse_equality();
    }
    return lhs;
}

int64_t PPEvaluator::parse_equality() {
    int64_t lhs = parse_relational();
    while (true) {
        if (check(PPTokenType::EQ))  { consume(); lhs = (lhs == parse_relational()) ? 1 : 0; }
        else if (check(PPTokenType::NEQ)) { consume(); lhs = (lhs != parse_relational()) ? 1 : 0; }
        else break;
    }
    return lhs;
}

int64_t PPEvaluator::parse_relational() {
    int64_t lhs = parse_shift();
    while (true) {
        if      (check(PPTokenType::LT))  { consume(); lhs = (lhs <  parse_shift()) ? 1 : 0; }
        else if (check(PPTokenType::GT))  { consume(); lhs = (lhs >  parse_shift()) ? 1 : 0; }
        else if (check(PPTokenType::LEQ)) { consume(); lhs = (lhs <= parse_shift()) ? 1 : 0; }
        else if (check(PPTokenType::GEQ)) { consume(); lhs = (lhs >= parse_shift()) ? 1 : 0; }
        else break;
    }
    return lhs;
}

int64_t PPEvaluator::parse_shift() {
    int64_t lhs = parse_additive();
    while (true) {
        if      (check(PPTokenType::LSHIFT)) { consume(); int64_t rhs = parse_additive(); lhs <<= rhs; }
        else if (check(PPTokenType::RSHIFT)) { consume(); int64_t rhs = parse_additive(); lhs >>= rhs; }
        else break;
    }
    return lhs;
}

int64_t PPEvaluator::parse_additive() {
    int64_t lhs = parse_multiplicative();
    while (true) {
        if      (check(PPTokenType::PLUS))  { consume(); lhs += parse_multiplicative(); }
        else if (check(PPTokenType::MINUS)) { consume(); lhs -= parse_multiplicative(); }
        else break;
    }
    return lhs;
}

int64_t PPEvaluator::parse_multiplicative() {
    int64_t lhs = parse_unary();
    while (true) {
        if (check(PPTokenType::STAR)) {
            consume(); lhs *= parse_unary();
        } else if (check(PPTokenType::SLASH)) {
            consume();
            int64_t rhs = parse_unary();
            if (rhs == 0) {
                m_diag.error(cur().loc, "division por cero en expresion del preprocesador");
                lhs = 0;
            } else {
                lhs /= rhs;
            }
        } else if (check(PPTokenType::PERCENT)) {
            consume();
            int64_t rhs = parse_unary();
            if (rhs == 0) {
                m_diag.error(cur().loc, "modulo por cero en expresion del preprocesador");
                lhs = 0;
            } else {
                lhs %= rhs;
            }
        } else {
            break;
        }
    }
    return lhs;
}

int64_t PPEvaluator::parse_unary() {
    if (check(PPTokenType::BANG))  { consume(); return !parse_unary()  ? 1 : 0; }
    if (check(PPTokenType::TILDE)) { consume(); return ~parse_unary(); }
    if (check(PPTokenType::MINUS)) { consume(); return -parse_unary(); }
    if (check(PPTokenType::PLUS))  { consume(); return  parse_unary(); }
    return parse_primary();
}

int64_t PPEvaluator::parse_primary() {
    // parentesis agrupadores
    if (check(PPTokenType::LPAREN)) {
        consume();
        int64_t val = parse_ternary();
        if (!match(PPTokenType::RPAREN)) {
            m_diag.error(cur().loc, "se esperaba ')' en expresion");
        }
        return val;
    }

    // literal numerico
    if (check(PPTokenType::NUMBER)) {
        PPToken t = consume();
        return parse_number_literal(t);
    }

    // identificadores: defined(), true, false, o macros residuales
    if (check(PPTokenType::IDENT)) {
        PPToken t = consume();

        // operador defined(MACRO) o defined MACRO
        if (t.value == "defined") {
            bool has_paren = check(PPTokenType::LPAREN);
            if (has_paren) consume(); // consume '('

            // saltar espacios
            while (check(PPTokenType::WHITESPACE)) consume();

            std::string macro_name;
            if (check(PPTokenType::IDENT)) {
                macro_name = cur().value;
                consume();
            } else {
                m_diag.error(cur().loc, "se esperaba nombre de macro en defined()");
            }

            if (has_paren) {
                while (check(PPTokenType::WHITESPACE)) consume();
                if (!match(PPTokenType::RPAREN)) {
                    m_diag.error(cur().loc, "se esperaba ')' en defined()");
                }
            }
            return m_macros.is_defined(macro_name) ? 1 : 0;
        }

        if (t.value == "true")  return 1;
        if (t.value == "false") return 0;

        // identificador no expandido: vale 0 (segun estandar C)
        return 0;
    }

    m_diag.error(cur().loc,
        std::string("token inesperado en expresion: ") + cur().value);
    consume();
    return 0;
}

int64_t PPEvaluator::parse_number_literal(const PPToken& tok) {
    const std::string& s = tok.value;
    if (s.empty()) return 0;

    try {
        size_t idx;
        // hexadecimal
        if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            return static_cast<int64_t>(std::stoull(s, &idx, 16));
        }
        // binario
        if (s.size() >= 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
            return static_cast<int64_t>(std::stoull(s.substr(2), &idx, 2));
        }
        // octal (si comienza con '0' seguido de digitos)
        if (s.size() >= 2 && s[0] == '0' && std::isdigit((unsigned char)s[1])) {
            return static_cast<int64_t>(std::stoull(s, &idx, 8));
        }
        // decimal
        return static_cast<int64_t>(std::stoll(s, &idx, 10));
    } catch (...) {
        m_diag.error(tok.loc, "literal numerico invalido: " + s);
        return 0;
    }
}

} // namespace vpp
