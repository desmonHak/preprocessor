/**
 * @file pp_lexer.h
 * @brief Lexer del preprocesador: convierte texto fuente en tokens PPToken.
 */
#pragma once

#include "pp_token.h"
#include "pp_diagnostics.h"
#include <vector>
#include <string>

namespace vpp {

/**
 * @brief Opciones de configuracion del lexer del preprocesador.
 *
 * Permiten adaptar el lexer a distintos lenguajes fuente configurando
 * el estilo de comentarios que debe ignorar durante la expansion de macros.
 */
struct LexerOptions {
    bool strip_line_comments  = true;  ///< Ignorar comentarios de linea //
    bool strip_block_comments = true;  ///< Ignorar comentarios de bloque /* */
    bool keep_whitespace      = false; ///< Preservar tokens WHITESPACE en directivas
};

/**
 * @brief Lexer del preprocesador.
 *
 * Recorre el texto fuente caracter a caracter y produce una secuencia de
 * PPToken. Solo las lineas que comienzan con '#' (precedido opcionalmente
 * por espacios) se tokenizan como directivas de preprocesador. El resto
 * del contenido se emite como tokens de tipo TEXT e IDENT para permitir
 * la expansion de macros en texto normal.
 *
 * El lexer es independiente del lenguaje: no interpreta la semantica del
 * texto que no sea directiva, solo identifica identificadores candidatos
 * a expansion de macro.
 */
class PPLexer {
public:
    /**
     * @brief Constructor.
     * @param source   Contenido completo del archivo fuente.
     * @param filename Nombre del archivo (para mensajes de diagnostico).
     * @param diag     Motor de diagnosticos compartido.
     * @param opts     Opciones del lexer.
     */
    PPLexer(std::string source,
            std::string filename,
            DiagnosticEngine& diag,
            LexerOptions opts = {});

    /**
     * @brief Tokeniza todo el fuente y devuelve la secuencia de tokens.
     *
     * El ultimo token del vector siempre es PP_EOF.
     *
     * @return Vector de tokens.
     */
    std::vector<PPToken> tokenize();

private:
    std::string       m_src;    ///< Texto fuente completo
    std::string       m_file;   ///< Nombre del archivo
    DiagnosticEngine& m_diag;   ///< Motor de diagnosticos
    LexerOptions      m_opts;   ///< Opciones del lexer
    size_t            m_pos;    ///< Posicion actual en m_src
    uint32_t          m_line;   ///< Linea actual (base 1)
    uint32_t          m_col;    ///< Columna actual (base 1)
    bool              m_at_bol; ///< True si estamos al inicio de una linea

    // --- metodos de acceso al flujo de caracteres ---

    /**
     * @brief Devuelve el caracter en la posicion actual + offset, sin avanzar.
     * @param offset Desplazamiento desde la posicion actual (default 0).
     * @return Caracter en esa posicion, o '\0' si fuera del rango.
     */
    char peek(int offset = 0) const;

    /**
     * @brief Avanza una posicion y devuelve el caracter consumido.
     * @return Caracter consumido.
     */
    char advance();

    /**
     * @brief Avanza si el caracter actual coincide con c.
     * @param c Caracter esperado.
     * @return true si se avanzo, false en caso contrario.
     */
    bool match(char c);

    /**
     * @brief Indica si se alcanzo el fin del fuente.
     * @return true si m_pos >= m_src.size().
     */
    bool at_end() const noexcept;

    /**
     * @brief Devuelve la ubicacion actual como SourceLocation.
     * @return SourceLocation con el archivo, linea y columna actuales.
     */
    SourceLocation loc() const;

    // --- escaner de lineas ---

    /**
     * @brief Escanea una linea de directiva comenzando tras el '#'.
     * @param out Vector donde se agregan los tokens de la directiva.
     */
    void scan_directive_line(std::vector<PPToken>& out);

    /**
     * @brief Escanea una linea de texto normal (no directiva).
     * @param out Vector donde se agregan los tokens de texto e identificadores.
     */
    void scan_text_line(std::vector<PPToken>& out);

    // --- escaner de tokens individuales (dentro de directivas) ---

    /**
     * @brief Escanea el siguiente token dentro de una directiva saltando whitespace inicial.
     * @return Token escaneado.
     */
    PPToken next_directive_token();

    /**
     * @brief Escanea el siguiente token dentro de una directiva sin saltar whitespace.
     *
     * Asume que el whitespace ya fue consumido o emitido por el llamador.
     *
     * @return Token escaneado.
     */
    PPToken next_directive_token_nosp();

    /**
     * @brief Escanea un identificador desde la posicion actual.
     * @return Token IDENT con el valor del identificador.
     */
    PPToken scan_ident();

    /**
     * @brief Escanea un literal numerico desde la posicion actual.
     * @return Token NUMBER con el valor textual del numero.
     */
    PPToken scan_number();

    /**
     * @brief Escanea un literal de cadena con el delimitador indicado.
     * @param delim Caracter delimitador de cierre ('"' o '\'').
     * @return Token STRING o CHAR_LIT con el contenido de la cadena.
     */
    PPToken scan_string(char delim);

    /**
     * @brief Escanea una cadena entre angulos <...> para #include.
     * @return Token ANGLE_STRING con el contenido entre angulos.
     */
    PPToken scan_angle_string();

    /**
     * @brief Salta un comentario de linea (desde // hasta el fin de linea).
     */
    void skip_line_comment();

    /**
     * @brief Salta un comentario de bloque (desde / * hasta * /).
     */
    void skip_block_comment();

    /**
     * @brief Salta espacios y tabulaciones sin saltar saltos de linea.
     */
    void skip_inline_whitespace();

    /**
     * @brief Indica si estamos al inicio de una directiva (# en BOL).
     * @return true si el caracter actual es '#' y estamos en BOL.
     */
    bool is_directive_start() const;
};

} // namespace vpp
