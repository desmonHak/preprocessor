/**
 * @file pp_lexer.cpp
 * @brief Implementacion del lexer del preprocesador vpp.
 */

#include <iostream>
#include "preprocessor/pp_lexer.h"
#include <cctype>
#include <stdexcept>

namespace vpp {

/* --- tabla de nombres de tipos de token ----------------------------------- */

const char* pp_token_type_name(PPTokenType t) {
    switch (t) {
        case PPTokenType::IDENT:        return "IDENT";
        case PPTokenType::NUMBER:       return "NUMBER";
        case PPTokenType::STRING:       return "STRING";
        case PPTokenType::CHAR_LIT:     return "CHAR_LIT";
        case PPTokenType::ANGLE_STRING: return "ANGLE_STRING";
        case PPTokenType::PLUS:         return "PLUS";
        case PPTokenType::MINUS:        return "MINUS";
        case PPTokenType::STAR:         return "STAR";
        case PPTokenType::SLASH:        return "SLASH";
        case PPTokenType::PERCENT:      return "PERCENT";
        case PPTokenType::AMP:          return "AMP";
        case PPTokenType::PIPE:         return "PIPE";
        case PPTokenType::CARET:        return "CARET";
        case PPTokenType::TILDE:        return "TILDE";
        case PPTokenType::BANG:         return "BANG";
        case PPTokenType::LSHIFT:       return "LSHIFT";
        case PPTokenType::RSHIFT:       return "RSHIFT";
        case PPTokenType::EQ:           return "EQ";
        case PPTokenType::NEQ:          return "NEQ";
        case PPTokenType::LT:           return "LT";
        case PPTokenType::GT:           return "GT";
        case PPTokenType::LEQ:          return "LEQ";
        case PPTokenType::GEQ:          return "GEQ";
        case PPTokenType::AND:          return "AND";
        case PPTokenType::OR:           return "OR";
        case PPTokenType::QUESTION:     return "QUESTION";
        case PPTokenType::COLON:        return "COLON";
        case PPTokenType::LPAREN:       return "LPAREN";
        case PPTokenType::RPAREN:       return "RPAREN";
        case PPTokenType::LBRACKET:     return "LBRACKET";
        case PPTokenType::RBRACKET:     return "RBRACKET";
        case PPTokenType::COMMA:        return "COMMA";
        case PPTokenType::HASH:         return "HASH";
        case PPTokenType::HASHHASH:     return "HASHHASH";
        case PPTokenType::ELLIPSIS:     return "ELLIPSIS";
        case PPTokenType::BACKSLASH:    return "BACKSLASH";
        case PPTokenType::ASSIGN:       return "ASSIGN";
        case PPTokenType::NEWLINE:      return "NEWLINE";
        case PPTokenType::WHITESPACE:   return "WHITESPACE";
        case PPTokenType::TEXT:         return "TEXT";
        case PPTokenType::PP_EOF:       return "PP_EOF";
        case PPTokenType::UNKNOWN:      return "UNKNOWN";
    }
    return "?";
}

/* --- PPLexer -------------------------------------------------------------- */

// @brief Deja el fuente con finales de linea de un solo caracter.
//
// El lexer trata el salto de linea como un token y usa la barra invertida
// seguida de salto para empalmar lineas.  Con CRLF, el retorno de carro se
// interponia: no contaba como blanco, rompia el empalme de lineas y se colaba
// en la salida por duplicado.  Normalizar a la entrada lo arregla de una vez
// para todo lo que viene despues, en lugar de tener que acordarse del retorno de carro en
// cada sitio que mire un salto.  Se contempla tambien el CR suelto de los Mac
// antiguos.
//
// @param s Fuente tal y como se leyo.
// @return El mismo texto con saltos de linea normalizados.
static std::string normalizar_saltos(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == 0x0D) {                       // retorno de carro
            if (i + 1 < s.size() && s[i + 1] == 0x0A) ++i;   // CRLF
            out += 0x0A;
        } else {
            out += s[i];
        }
    }
    return out;
}

PPLexer::PPLexer(std::string source, std::string filename,
                 DiagnosticEngine& diag, LexerOptions opts)
    : m_src(normalizar_saltos(std::move(source)))
    , m_file(std::move(filename))
    , m_diag(diag)
    , m_opts(opts)
    , m_pos(0)
    , m_line(1)
    , m_col(1)
    , m_at_bol(true) // al inicio siempre estamos al comienzo de una linea
{}

/* --- acceso al flujo de caracteres ---------------------------------------- */

char PPLexer::peek(int offset) const {
    size_t idx = m_pos + static_cast<size_t>(offset);
    if (idx >= m_src.size()) return '\0';
    return m_src[idx];
}

char PPLexer::advance() {
    char c = m_src[m_pos++];
    if (c == '\n') {
        ++m_line;
        m_col   = 1;
        m_at_bol = true;
    } else {
        ++m_col;
        m_at_bol = false;
    }
    return c;
}

bool PPLexer::match(char c) {
    if (at_end() || m_src[m_pos] != c) return false;
    advance();
    return true;
}

bool PPLexer::at_end() const noexcept {
    return m_pos >= m_src.size();
}

SourceLocation PPLexer::loc() const {
    return SourceLocation(m_file, m_line, m_col);
}

bool PPLexer::is_directive_start() const {
    // es directiva si estamos en BOL y el proximo caracter no-espacio es '#'
    // esto permite espacios/tabulaciones antes del '#' (p.ej.: "    #define")
    if (!m_at_bol) return false;
    size_t look = m_pos;
    while (look < m_src.size() && (m_src[look] == ' ' || m_src[look] == '\t'))
        ++look;
    return look < m_src.size() && m_src[look] == '#';
}

/* --- tokenizacion principal ----------------------------------------------- */

std::vector<PPToken> PPLexer::tokenize() {
    std::vector<PPToken> result;
    result.reserve(1024);

    while (!at_end()) {
        // manejar continuacion de linea con backslash
        if (peek() == '\\' && peek(1) == '\n') {
            advance(); // salta '\'
            advance(); // salta '\n'
            continue;
        }

        if (is_directive_start()) {
            // linea de directiva: tokenizar completamente
            scan_directive_line(result);
        } else if (peek() == '\n') {
            // fin de linea fuera de directiva
            SourceLocation l = loc();
            advance();
            result.emplace_back(PPTokenType::NEWLINE, "\n", l);
        } else {
            // linea de texto normal: escanear para expansion de macros
            scan_text_line(result);
        }
    }

    result.emplace_back(PPTokenType::PP_EOF, "", loc());
    return result;
}

/* --- escaner de linea de directiva ---------------------------------------- */

void PPLexer::scan_directive_line(std::vector<PPToken>& out) {
    // emitir whitespace opcional que precede al '#' en la misma linea
    // (directivas indentadas: "    #define")
    if (!at_end() && (peek() == ' ' || peek() == '\t')) {
        SourceLocation wl = loc();
        std::string ws;
        while (!at_end() && (peek() == ' ' || peek() == '\t'))
            ws += advance();
        out.emplace_back(PPTokenType::WHITESPACE, std::move(ws), wl);
    }

    // consumir el '#' de la directiva
    SourceLocation hash_loc = loc();

    advance(); // consume '#'

    // verificar si es '##' (pegado de tokens, raro al inicio de linea pero valido)
    bool is_double = !at_end() && peek() == '#';
    if (is_double) {
        advance();
        out.emplace_back(PPTokenType::HASHHASH, "##", hash_loc);
    } else {
        out.emplace_back(PPTokenType::HASH, "#", hash_loc);
    }

    // tokenizar el resto de la linea de directiva
    while (!at_end() && peek() != '\n') {
        // manejar continuacion de linea con backslash
        if (peek() == '\\' && peek(1) == '\n') {
            advance();
            advance();
            continue;
        }
        // emitir whitespace explicitamente para preservar espacios en cuerpos de macros
        if (peek() == ' ' || peek() == '\t' || peek() == '\r') {
            SourceLocation wl = loc();
            std::string ws;
            while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\r')) {
                ws += advance();
            }
            out.emplace_back(PPTokenType::WHITESPACE, std::move(ws), wl);
            continue;
        }
        // manejar comentarios de linea
        if (peek() == '/' && peek(1) == '/' && m_opts.strip_line_comments) {
            skip_line_comment();
            break;
        }
        // manejar comentarios de bloque
        if (peek() == '/' && peek(1) == '*' && m_opts.strip_block_comments) {
            // Mismas lineas que ocupaba, repuestas (ver scan_text_line).
            const size_t saltos = skip_block_comment();
            for (size_t k = 0; k < saltos; ++k)
                out.emplace_back(PPTokenType::TEXT, "\n", loc());
            continue;
        }
        // escanear el siguiente token no-espacio
        out.push_back(next_directive_token_nosp());
    }

    // emitir el NEWLINE que cierra la directiva
    if (!at_end() && peek() == '\n') {
        SourceLocation nl_loc = loc();
        advance();
        out.emplace_back(PPTokenType::NEWLINE, "\n", nl_loc);
    }
}

/* --- escaner de linea de texto (no directiva) ----------------------------- */

void PPLexer::scan_text_line(std::vector<PPToken>& out) {
    std::string text_buf;
    SourceLocation text_start = loc();

    auto flush_text = [&]() {
        if (!text_buf.empty()) {
            out.emplace_back(PPTokenType::TEXT, std::move(text_buf), text_start);
            text_buf.clear();
            text_start = loc();
        }
    };

    while (!at_end() && peek() != '\n') {
        // manejar comentarios de linea
        if (peek() == '/' && peek(1) == '/' && m_opts.strip_line_comments) {
            flush_text();
            skip_line_comment();
            break;
        }
        // manejar comentarios de bloque
        if (peek() == '/' && peek(1) == '*' && m_opts.strip_block_comments) {
            flush_text();
            const size_t saltos = skip_block_comment();
            /* Las lineas que ocupaba el comentario se reponen vacias.  El
             * texto sobra, pero su SITIO no: sin esto todo lo de abajo se
             * corre hacia arriba y cualquier mensaje que cite una linea cita
             * la equivocada. */
            for (size_t k = 0; k < saltos; ++k) text_buf += '\n';
            flush_text();
            // el comentario de bloque puede continuar en la linea, no hacemos break
            text_start = loc();
            continue;
        }
        // manejar continuacion de linea
        if (peek() == '\\' && peek(1) == '\n') {
            advance();
            advance();
            text_buf += ' '; // la continuacion se reemplaza por espacio
            continue;
        }
        // numeros: tienen que salir como UN token, o el pegado con ## los parte
        if (std::isdigit((unsigned char)peek()) ||
            (peek() == '.' && std::isdigit((unsigned char)peek(1)))) {
            flush_text();
            out.push_back(scan_pp_number());
            text_start = loc();
            continue;
        }
        // identificadores: candidatos a expansion de macro
        if (std::isalpha((unsigned char)peek()) || peek() == '_') {
            flush_text();
            SourceLocation id_loc = loc();
            out.push_back(scan_ident());
            text_start = loc();
            continue;
        }
        // literales de cadena: no expandir macros dentro de ellas
        if (peek() == '"') {
            flush_text();
            out.push_back(scan_string('"'));
            text_start = loc();
            continue;
        }
        if (peek() == '\'') {
            flush_text();
            out.push_back(scan_string('\''));
            text_start = loc();
            continue;
        }
        // espacios y tabulaciones: emitir como WHITESPACE para facilitar trim en args
        if (peek() == ' ' || peek() == '\t') {
            flush_text();
            SourceLocation wl = loc();
            std::string ws;
            while (!at_end() && (peek() == ' ' || peek() == '\t')) ws += advance();
            out.emplace_back(PPTokenType::WHITESPACE, std::move(ws), wl);
            text_start = loc();
            continue;
        }
        // parentesis y coma: emitir como tokens propios para expansion de macros funcion
        if (peek() == '(') {
            flush_text();
            SourceLocation pl = loc();
            advance();
            out.emplace_back(PPTokenType::LPAREN, "(", pl);
            text_start = loc();
            continue;
        }
        if (peek() == ')') {
            flush_text();
            SourceLocation pl = loc();
            advance();
            out.emplace_back(PPTokenType::RPAREN, ")", pl);
            text_start = loc();
            continue;
        }
        if (peek() == ',') {
            flush_text();
            SourceLocation pl = loc();
            advance();
            out.emplace_back(PPTokenType::COMMA, ",", pl);
            text_start = loc();
            continue;
        }
        // cualquier otro caracter: agregar al buffer de texto
        text_buf += advance();
    }

    flush_text();

    // emitir NEWLINE
    if (!at_end() && peek() == '\n') {
        SourceLocation nl_loc = loc();
        advance();
        out.emplace_back(PPTokenType::NEWLINE, "\n", nl_loc);
    }
}

/* --- escaner de tokens individuales dentro de directivas ------------------ */

PPToken PPLexer::next_directive_token() {
    skip_inline_whitespace();
    return next_directive_token_nosp();
}

PPToken PPLexer::next_directive_token_nosp() {
    if (at_end() || peek() == '\n') {
        return PPToken(PPTokenType::NEWLINE, "", loc());
    }

    SourceLocation l = loc();
    char c = peek();

    // identificadores y palabras clave de directivas
    if (std::isalpha((unsigned char)c) || c == '_') {
        return scan_ident();
    }

    // numeros
    if (std::isdigit((unsigned char)c)) {
        return scan_number();
    }

    // literales de cadena
    if (c == '"') {
        return scan_string('"');
    }
    if (c == '\'') {
        return scan_string('\'');
    }

    // cadena de angulo para #include <...>
    if (c == '<') {
        // distinguir entre operador '<' y '<...>' de include segun contexto;
        // el parser decidira; aqui emitimos LT o ANGLE_STRING segun si parece include
        // por simplicidad devolvemos LT y scan_angle_string se llama explicitamente por el parser
        advance();
        if (peek() == '<') { advance(); return PPToken(PPTokenType::LSHIFT, "<<", l); }
        if (peek() == '=') { advance(); return PPToken(PPTokenType::LEQ,    "<=", l); }
        return PPToken(PPTokenType::LT, "<", l);
    }

    advance(); // consume el caracter

    switch (c) {
        case '>':
            if (match('>')) return PPToken(PPTokenType::RSHIFT, ">>", l);
            if (match('=')) return PPToken(PPTokenType::GEQ,    ">=", l);
            return PPToken(PPTokenType::GT, ">", l);
        case '=':
            if (match('=')) return PPToken(PPTokenType::EQ, "==", l);
            return PPToken(PPTokenType::ASSIGN, "=", l);
        case '!':
            if (match('=')) return PPToken(PPTokenType::NEQ,  "!=", l);
            return PPToken(PPTokenType::BANG, "!", l);
        case '&':
            if (match('&')) return PPToken(PPTokenType::AND, "&&", l);
            return PPToken(PPTokenType::AMP, "&", l);
        case '|':
            if (match('|')) return PPToken(PPTokenType::OR, "||", l);
            return PPToken(PPTokenType::PIPE, "|", l);
        case '#':
            if (match('#')) return PPToken(PPTokenType::HASHHASH, "##", l);
            return PPToken(PPTokenType::HASH, "#", l);
        case '.':
            // verificar '...'
            if (peek() == '.' && peek(1) == '.') {
                advance(); advance();
                return PPToken(PPTokenType::ELLIPSIS, "...", l);
            }
            return PPToken(PPTokenType::UNKNOWN, ".", l);
        case '+': return PPToken(PPTokenType::PLUS,     "+",  l);
        case '-': return PPToken(PPTokenType::MINUS,    "-",  l);
        case '*': return PPToken(PPTokenType::STAR,     "*",  l);
        case '/': return PPToken(PPTokenType::SLASH,    "/",  l);
        case '%': return PPToken(PPTokenType::PERCENT,  "%",  l);
        case '^': return PPToken(PPTokenType::CARET,    "^",  l);
        case '~': return PPToken(PPTokenType::TILDE,    "~",  l);
        case '(': return PPToken(PPTokenType::LPAREN,   "(",  l);
        case ')': return PPToken(PPTokenType::RPAREN,   ")",  l);
        case '[': return PPToken(PPTokenType::LBRACKET, "[",  l);
        case ']': return PPToken(PPTokenType::RBRACKET, "]",  l);
        case ',': return PPToken(PPTokenType::COMMA,    ",",  l);
        case ':': return PPToken(PPTokenType::COLON,    ":",  l);
        case '?': return PPToken(PPTokenType::QUESTION, "?",  l);
        case '\\':return PPToken(PPTokenType::BACKSLASH,"\\", l);
        default:
            return PPToken(PPTokenType::UNKNOWN, std::string(1, c), l);
    }
}

PPToken PPLexer::scan_ident() {
    SourceLocation l = loc();
    std::string s;
    while (!at_end() &&
           (std::isalnum((unsigned char)peek()) || peek() == '_')) {
        s += advance();
    }
    return PPToken(PPTokenType::IDENT, std::move(s), l);
}

PPToken PPLexer::scan_pp_number() {
    SourceLocation l = loc();
    std::string s;

    // Regla de "numero de preprocesado" del estandar de C: empieza por digito
    // (o por punto seguido de digito) y a partir de ahi absorbe digitos,
    // letras, guiones bajos, puntos y el signo que sigue a e/E/p/P.
    //
    // Es mas amplia que "un numero" a proposito: 200112L, 1.5e-9 y hasta 0x1p+3
    // son UN token.  Partirlos rompe el pegado con ##, que junta el ultimo
    // token de un lado con el PRIMERO del otro: con `200112L` partido en
    // `200112` y `L`, un `___X_##v` producia `___X_200112` mas una `L` suelta,
    // y el resultado ya no coincidia con ninguna macro.  Es lo que impedia
    // preprocesar las cabeceras del SDK de macOS.
    if (peek() == '.') s += advance();
    while (!at_end()) {
        const char c = peek();
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            std::isalpha(static_cast<unsigned char>(c)) ||
            c == '_' || c == '.') {
            // el signo solo cuenta si viene detras de un exponente
            const bool exp = (c == 'e' || c == 'E' || c == 'p' || c == 'P');
            s += advance();
            if (exp && !at_end() && (peek() == '+' || peek() == '-')) {
                s += advance();
            }
            continue;
        }
        break;
    }
    return PPToken(PPTokenType::NUMBER, std::move(s), l);
}

PPToken PPLexer::scan_number() {
    SourceLocation l = loc();
    std::string s;
    // prefijo hexadecimal, octal, binario
    if (peek() == '0') {
        s += advance();
        if (!at_end() && (peek() == 'x' || peek() == 'X')) {
            s += advance();
            while (!at_end() && std::isxdigit((unsigned char)peek())) s += advance();
        } else if (!at_end() && (peek() == 'b' || peek() == 'B')) {
            s += advance();
            while (!at_end() && (peek() == '0' || peek() == '1')) s += advance();
        } else {
            while (!at_end() && std::isdigit((unsigned char)peek())) s += advance();
        }
    } else {
        while (!at_end() && std::isdigit((unsigned char)peek())) s += advance();
    }
    // parte decimal
    if (!at_end() && peek() == '.') {
        s += advance();
        while (!at_end() && std::isdigit((unsigned char)peek())) s += advance();
    }
    // sufijo (u, l, ul, ll, ull, etc.)
    while (!at_end() && (peek() == 'u' || peek() == 'U' ||
                         peek() == 'l' || peek() == 'L' ||
                         peek() == 'f' || peek() == 'F')) {
        s += advance();
    }
    return PPToken(PPTokenType::NUMBER, std::move(s), l);
}

PPToken PPLexer::scan_string(char delim) {
    SourceLocation l = loc();
    std::string s;
    s += advance(); // consume el delimitador de apertura
    // Detectar triple-quoted ("""..."""): consume las dos comillas extras y
    // lee hasta la siguiente secuencia """.  Permite saltos de linea
    // dentro del literal.  Solo aplica al delimitador doble (no a chars).
    if (delim == '"' && !at_end() && peek() == '"' && peek(1) == '"') {
        s += advance(); // segunda comilla
        s += advance(); // tercera comilla
        while (!at_end()) {
            if (peek() == '"' && peek(1) == '"' && peek(2) == '"') {
                s += advance(); s += advance(); s += advance();
                return PPToken(PPTokenType::STRING, std::move(s), l);
            }
            if (peek() == '\\') {
                s += advance();
                if (!at_end()) s += advance();
            } else {
                s += advance();
            }
        }
        m_diag.error(l, "literal de cadena triple-quoted sin cerrar");
        return PPToken(PPTokenType::STRING, std::move(s), l);
    }
    while (!at_end() && peek() != delim && peek() != '\n') {
        if (peek() == '\\') {
            s += advance(); // consume '\'
            if (!at_end()) s += advance(); // consume el caracter escapado
        } else {
            s += advance();
        }
    }
    if (!at_end() && peek() == delim) {
        s += advance(); // consume el delimitador de cierre
    } else {
        m_diag.error(l, "literal de cadena sin cerrar");
    }
    PPTokenType t = (delim == '"') ? PPTokenType::STRING : PPTokenType::CHAR_LIT;
    return PPToken(t, std::move(s), l);
}

PPToken PPLexer::scan_angle_string() {
    SourceLocation l = loc();
    std::string s;
    s += advance(); // consume '<'
    while (!at_end() && peek() != '>' && peek() != '\n') {
        s += advance();
    }
    if (!at_end() && peek() == '>') {
        s += advance(); // consume '>'
    } else {
        m_diag.error(l, "cadena de angulo sin cerrar en #include");
    }
    return PPToken(PPTokenType::ANGLE_STRING, std::move(s), l);
}

void PPLexer::skip_line_comment() {
    // avanza hasta el fin de la linea sin consumir el '\n'
    while (!at_end() && peek() != '\n') {
        advance();
    }
}

/**
 * @brief Cuantos saltos de linea ocupaba el comentario que se acaba de comer.
 *
 * Quien lo llama los repone en la salida.  Un comentario de bloque desaparece
 * del texto, pero las lineas que ocupaba NO pueden desaparecer con el: todo lo
 * que venga detras se correria hacia arriba y cada mensaje del compilador --
 * y cada traza de un fallo -- senalaria una linea que no es.  En un fichero
 * documentado el desfase es de decenas de lineas.
 */
size_t PPLexer::skip_block_comment() {
    SourceLocation start = loc();
    size_t saltos = 0;
    advance(); // consume '/'
    advance(); // consume '*'
    while (!at_end()) {
        if (peek() == '*' && peek(1) == '/') {
            advance(); // '*'
            advance(); // '/'
            return saltos;
        }
        if (peek() == '\n') ++saltos;
        advance();
    }
    // si llegamos aqui, el comentario no fue cerrado
    m_diag.error(start, "comentario de bloque sin cerrar al final del archivo");
    return saltos;
}

void PPLexer::skip_inline_whitespace() {
    while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\r')) {
        advance();
    }
}

} // namespace vpp
