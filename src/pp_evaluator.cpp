/**
 * @file pp_evaluator.cpp
 * @brief Implementacion del evaluador de expresiones constantes del preprocesador vpp.
 */

#include "preprocessor/pp_evaluator.h"
#include <stdexcept>
#include <climits>

namespace vpp {

static const PPToken EOF_TOKEN(PPTokenType::PP_EOF, "", {});

PPEvaluator::PPEvaluator(MacroTable& macros, DiagnosticEngine& diag,
                         CapabilityResolver caps)
    : m_macros(macros), m_diag(diag), m_caps(std::move(caps)), m_pos(0)
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

/**
 * @brief Valor numerico de una constante de caracter.
 *
 * Solo se contempla el caso de un unico caracter, que es el que aparece en las
 * condiciones del preprocesador.  Se reconocen las secuencias de escape
 * habituales; una constante multi-caracter se evalua por su ultimo caracter,
 * que es lo que hacen gcc y clang.
 *
 * @param t Token CHAR_LIT, con sus comillas simples incluidas.
 * @return Valor entero del caracter.
 */
static int64_t char_literal_value(const PPToken& t) {
    const std::string& v = t.value;

    // quitar las comillas simples delimitadoras
    size_t i   = (!v.empty() && v.front() == 0x27) ? 1 : 0;
    size_t end = v.size();
    if (end > i && v[end - 1] == 0x27) --end;

    int64_t value = 0;
    while (i < end) {
        unsigned char c = static_cast<unsigned char>(v[i]);

        // Las secuencias de escape se resuelven por CODIGO y no escribiendo
        // el literal correspondiente: asi el codigo no depende de como
        // sobreviva una barra invertida a las herramientas que lo editan.
        if (c == 0x5C && i + 1 < end) {      // 0x5C = barra invertida
            ++i;
            switch (v[i]) {
                case 'n':  c = 0x0A; break;  // salto de linea
                case 't':  c = 0x09; break;  // tabulador
                case 'r':  c = 0x0D; break;  // retorno de carro
                case 'a':  c = 0x07; break;  // alerta
                case 'b':  c = 0x08; break;  // retroceso
                case 'f':  c = 0x0C; break;  // salto de pagina
                case 'v':  c = 0x0B; break;  // tabulador vertical
                case '0':  c = 0x00; break;  // nulo
                case '"': c = 0x22; break;  // comilla doble
                default:
                    // 0x5C y 0x27 (barra y comilla simple) caen aqui, igual
                    // que cualquier escape no reconocido: el caracter vale
                    // por si mismo.
                    c = static_cast<unsigned char>(v[i]);
                    break;
            }
        }

        // Una constante multi-caracter se queda con el ULTIMO, que es lo que
        // hacen gcc y clang en una condicion del preprocesador.
        value = static_cast<int64_t>(c);
        ++i;
    }
    return value;
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

    // La cadena arrastra el caracter de signo, pero hacia fuera solo importa si
    // la condicion es cierta o falsa, asi que se devuelven los bits.
    const PPValue val = parse_ternary();

    if (!check(PPTokenType::PP_EOF) && m_pos < m_toks.size()) {
        m_diag.warning(cur().loc,
            "tokens sobrantes tras la expresion del preprocesador");
    }
    return val.v;
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

PPValue PPEvaluator::parse_ternary() {
    PPValue cond = parse_or();
    if (!match(PPTokenType::QUESTION)) return cond;
    PPValue then_val = parse_ternary();
    if (!match(PPTokenType::COLON)) {
        m_diag.error(cur().loc, "se esperaba ':' en operador ternario");
        return then_val;
    }
    PPValue else_val = parse_ternary();
    // El tipo del resultado sale de las DOS ramas, no solo de la elegida: si
    // cualquiera es sin signo, el resultado lo es.
    PPValue r = cond.v ? then_val : else_val;
    r.is_unsigned = then_val.is_unsigned || else_val.is_unsigned;
    return r;
}

PPValue PPEvaluator::parse_or() {
    PPValue lhs = parse_and();
    while (check(PPTokenType::OR)) {
        consume();
        PPValue rhs = parse_and();
        // los operadores logicos dan un int CON signo, valgan lo que valgan
        lhs = PPValue{(lhs.v || rhs.v) ? 1 : 0, false};
    }
    return lhs;
}

PPValue PPEvaluator::parse_and() {
    PPValue lhs = parse_bitor();
    while (check(PPTokenType::AND)) {
        consume();
        PPValue rhs = parse_bitor();
        lhs = PPValue{(lhs.v && rhs.v) ? 1 : 0, false};
    }
    return lhs;
}

PPValue PPEvaluator::parse_bitor() {
    PPValue lhs = parse_xor();
    while (check(PPTokenType::PIPE)) {
        consume();
        PPValue rhs = parse_xor();
        // el bit a bit da los mismos bits con o sin signo, pero SI
        // propaga el caracter sin signo al resultado
        lhs = PPValue{lhs.v | rhs.v,
                      lhs.is_unsigned || rhs.is_unsigned};
    }
    return lhs;
}

PPValue PPEvaluator::parse_xor() {
    PPValue lhs = parse_bitand();
    while (check(PPTokenType::CARET)) {
        consume();
        PPValue rhs = parse_bitand();
        // el bit a bit da los mismos bits con o sin signo, pero SI
        // propaga el caracter sin signo al resultado
        lhs = PPValue{lhs.v ^ rhs.v,
                      lhs.is_unsigned || rhs.is_unsigned};
    }
    return lhs;
}

PPValue PPEvaluator::parse_bitand() {
    PPValue lhs = parse_equality();
    while (check(PPTokenType::AMP)) {
        consume();
        PPValue rhs = parse_equality();
        // el bit a bit da los mismos bits con o sin signo, pero SI
        // propaga el caracter sin signo al resultado
        lhs = PPValue{lhs.v & rhs.v,
                      lhs.is_unsigned || rhs.is_unsigned};
    }
    return lhs;
}

PPValue PPEvaluator::parse_equality() {
    PPValue lhs = parse_relational();
    while (true) {
        bool eq;
        if      (check(PPTokenType::EQ)) { consume(); eq = true;  }
        else if (check(PPTokenType::NEQ))  { consume(); eq = false; }
        else break;
        PPValue rhs = parse_relational();
        // la igualdad no cambia con el signo: compara los mismos bits
        const bool r = (lhs.v == rhs.v);
        lhs = PPValue{(eq ? r : !r) ? 1 : 0, false};
    }
    return lhs;
}

PPValue PPEvaluator::parse_relational() {
    PPValue lhs = parse_shift();
    while (true) {
        int op;   // 0:<  1:>  2:<=  3:>=
        if      (check(PPTokenType::LT))  { consume(); op = 0; }
        else if (check(PPTokenType::GT))  { consume(); op = 1; }
        else if (check(PPTokenType::LEQ)) { consume(); op = 2; }
        else if (check(PPTokenType::GEQ)) { consume(); op = 3; }
        else break;

        PPValue rhs = parse_shift();

        // Aqui es donde la conversion cambia el resultado: basta con que UN
        // operando sea sin signo para que la comparacion entera lo sea, y
        // entonces `-1 > 0u` es VERDADERO, porque el -1 se convierte en un
        // valor enorme.  Es la clase de diferencia que no da error: da lo
        // contrario.
        bool r;
        if (lhs.is_unsigned || rhs.is_unsigned) {
            const uint64_t a = lhs.u(), b = rhs.u();
            r = (op == 0) ? (a <  b) : (op == 1) ? (a >  b)
              : (op == 2) ? (a <= b) : (a >= b);
        } else {
            const int64_t a = lhs.v, b = rhs.v;
            r = (op == 0) ? (a <  b) : (op == 1) ? (a >  b)
              : (op == 2) ? (a <= b) : (a >= b);
        }
        // la comparacion produce un int con signo
        lhs = PPValue{r ? 1 : 0, false};
    }
    return lhs;
}

PPValue PPEvaluator::parse_shift() {
    PPValue lhs = parse_additive();
    while (true) {
        bool izq;
        if      (check(PPTokenType::LSHIFT)) { consume(); izq = true;  }
        else if (check(PPTokenType::RSHIFT)) { consume(); izq = false; }
        else break;

        PPValue rhs = parse_additive();
        const int n = static_cast<int>(rhs.v & 63);

        // El desplazamiento a la derecha SI depende del signo del operando
        // IZQUIERDO: sin signo entran ceros, con signo se replica el bit alto.
        // El operando derecho no influye en el tipo del resultado.
        if (izq) {
            lhs.v = static_cast<int64_t>(lhs.u() << n);
        } else if (lhs.is_unsigned) {
            lhs.v = static_cast<int64_t>(lhs.u() >> n);
        } else {
            lhs.v = lhs.v >> n;
        }
    }
    return lhs;
}

PPValue PPEvaluator::parse_additive() {
    PPValue lhs = parse_multiplicative();
    while (true) {
        bool suma;
        if      (check(PPTokenType::PLUS))  { consume(); suma = true;  }
        else if (check(PPTokenType::MINUS)) { consume(); suma = false; }
        else break;

        PPValue rhs = parse_multiplicative();
        const bool sin_signo = lhs.is_unsigned || rhs.is_unsigned;
        // se opera sobre los bits sin signo para que el desbordamiento este
        // definido; los bits resultantes son los mismos en los dos casos
        const uint64_t r = suma ? (lhs.u() + rhs.u()) : (lhs.u() - rhs.u());
        lhs = PPValue{static_cast<int64_t>(r), sin_signo};
    }
    return lhs;
}

PPValue PPEvaluator::parse_multiplicative() {
    PPValue lhs = parse_unary();
    while (true) {
        int op;   // 0:*  1:/  2:%
        if      (check(PPTokenType::STAR))    { consume(); op = 0; }
        else if (check(PPTokenType::SLASH))   { consume(); op = 1; }
        else if (check(PPTokenType::PERCENT)) { consume(); op = 2; }
        else break;

        PPValue rhs = parse_unary();
        const bool sin_signo = lhs.is_unsigned || rhs.is_unsigned;

        if (op == 0) {
            lhs = PPValue{static_cast<int64_t>(lhs.u() * rhs.u()), sin_signo};
            continue;
        }

        if (rhs.v == 0) {
            m_diag.error(cur().loc, op == 1
                ? "division por cero en expresion del preprocesador"
                : "modulo por cero en expresion del preprocesador");
            lhs = PPValue{0, sin_signo};
            continue;
        }

        // La division SI da resultados distintos segun el signo, asi que se
        // elige la operacion en vez de operar siempre sobre los bits.
        if (sin_signo) {
            const uint64_t a = lhs.u(), b = rhs.u();
            lhs = PPValue{static_cast<int64_t>(op == 1 ? a / b : a % b), true};
        } else {
            lhs = PPValue{op == 1 ? lhs.v / rhs.v : lhs.v % rhs.v, false};
        }
    }
    return lhs;
}

PPValue PPEvaluator::parse_unary() {
    if (check(PPTokenType::BANG)) {
        consume();
        // la negacion logica siempre da un int con signo
        return PPValue{parse_unary().v ? 0 : 1, false};
    }
    if (check(PPTokenType::TILDE)) {
        consume();
        PPValue x = parse_unary();
        // el complemento conserva el caracter de signo del operando
        return PPValue{~x.v, x.is_unsigned};
    }
    if (check(PPTokenType::MINUS)) {
        consume();
        PPValue x = parse_unary();
        // Negar un valor SIN signo lo deja sin signo: por eso `-1` escrito como
        // `-1u` es un numero enorme y no un negativo.  Se opera sobre los bits
        // para que el desbordamiento este definido.
        return PPValue{static_cast<int64_t>(0ull - x.u()), x.is_unsigned};
    }
    if (check(PPTokenType::PLUS)) {
        consume();
        return parse_unary();
    }
    return parse_primary();
}

PPValue PPEvaluator::parse_primary() {
    // parentesis agrupadores
    if (check(PPTokenType::LPAREN)) {
        consume();
        PPValue val = parse_ternary();
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

    // constante de caracter: 'A', '0', '\\n'...
    //
    // En una expresion del preprocesador vale su valor numerico, asi que
    // `#if 'A' == 65` es verdadero.  Sin esto, cualquier condicion con una
    // constante de caracter abortaba el fichero entero.
    if (check(PPTokenType::CHAR_LIT)) {
        PPToken t = consume();
        // una constante de caracter es un int CON signo
        return PPValue{char_literal_value(t), false};
    }

    // identificadores: defined(), true, false, o macros residuales
    if (check(PPTokenType::IDENT)) {
        PPToken t = consume();

        // Operadores de prueba de caracteristicas: __has_builtin, __has_include
        // y companeros.
        //
        // Se atienden AQUI y no como macros porque no lo son: preguntan por lo
        // que sabe hacer el compilador de destino.  Si el usuario define una
        // macro con ese nombre, no se llega hasta aqui -- la expansion la habra
        // sustituido antes -- de modo que definirla es la forma de imponer una
        // respuesta propia sin tocar nada mas.
        if (t.value.rfind("__has_", 0) == 0) {
            // El argumento se toma EN CRUDO: puede ser <vector>, "x.h" o un
            // identificador, y ninguno de los tres se parsea como expresion.
            std::string arg;
            if (check(PPTokenType::LPAREN)) {
                consume();
                int prof = 1;
                while (!check(PPTokenType::PP_EOF) && prof > 0) {
                    if (check(PPTokenType::LPAREN)) ++prof;
                    if (check(PPTokenType::RPAREN)) {
                        --prof;
                        if (prof == 0) { consume(); break; }
                    }
                    if (!check(PPTokenType::WHITESPACE)) arg += cur().value;
                    consume();
                }
            }
            const int64_t v = m_caps ? m_caps(t.value, arg) : 0;
            return PPValue{v, false};
        }

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
            return PPValue{m_macros.is_defined(macro_name) ? 1 : 0, false};
        }

        if (t.value == "true")  return PPValue{1, false};
        if (t.value == "false") return PPValue{0, false};

        // identificador no expandido: vale 0 (segun estandar C)
        return PPValue{0, false};
    }

    m_diag.error(cur().loc,
        std::string("token inesperado en expresion: ") + cur().value);
    consume();
    return PPValue{0, false};
}

PPValue PPEvaluator::parse_number_literal(const PPToken& tok) {
    const std::string& s = tok.value;
    if (s.empty()) return PPValue{0, false};

    // Sufijos: `u`/`U` hace el literal SIN signo, y eso se contagia a toda la
    // expresion.  Los de tamano (`l`, `ll`) no cambian nada aqui, porque ya se
    // opera en 64 bits, pero hay que recortarlos igual para que la conversion
    // no falle.
    size_t fin = s.size();
    bool sin_signo = false;
    while (fin > 0) {
        const char c = s[fin - 1];
        if (c == 'u' || c == 'U') { sin_signo = true; --fin; }
        else if (c == 'l' || c == 'L') { --fin; }
        else break;
    }
    const std::string cuerpo = s.substr(0, fin);
    if (cuerpo.empty()) return PPValue{0, sin_signo};

    try {
        size_t idx;
        uint64_t v;
        if (cuerpo.size() >= 2 && cuerpo[0] == '0' &&
            (cuerpo[1] == 'x' || cuerpo[1] == 'X')) {
            v = std::stoull(cuerpo, &idx, 16);
        } else if (cuerpo.size() >= 2 && cuerpo[0] == '0' &&
                   (cuerpo[1] == 'b' || cuerpo[1] == 'B')) {
            v = std::stoull(cuerpo.substr(2), &idx, 2);
        } else if (cuerpo.size() >= 2 && cuerpo[0] == '0' &&
                   std::isdigit(static_cast<unsigned char>(cuerpo[1]))) {
            v = std::stoull(cuerpo, &idx, 8);
        } else {
            v = std::stoull(cuerpo, &idx, 10);
        }

        // Un literal que no cabe en un entero con signo es sin signo aunque no
        // lleve sufijo, igual que en C.
        if (v > static_cast<uint64_t>(INT64_MAX)) sin_signo = true;
        return PPValue{static_cast<int64_t>(v), sin_signo};
    } catch (...) {
        m_diag.error(tok.loc, "literal numerico invalido: " + s);
        return PPValue{0, false};
    }
}

} // namespace vpp
