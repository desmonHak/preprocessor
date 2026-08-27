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
    /**
     * @brief Lo que marca el comienzo de una directiva.
     *
     * `#` es el de C, y por eso es el de por omision, pero vpp no es un
     * preprocesador de C: en Python, shell, Ruby, Make o YAML el `#` es un
     * COMENTARIO, y tomarlo por directiva se come lineas que no son suyas.
     *
     * Con el marcador declarado no hay que elegir entre cazar erratas y
     * respetar el lenguaje: lo que empieza por el marcador es una directiva y
     * un nombre desconocido ahi es un error; lo que no, es texto y sale intacto.
     *
     * Puede tener varios caracteres (`//#`, `;;`) para lenguajes en los que
     * conviene que la directiva sea ademas un comentario del propio lenguaje.
     */
    std::string directive_marker = "#";

    bool strip_line_comments  = true;  ///< Ignorar comentarios de linea //
    bool strip_block_comments = true;  ///< Ignorar comentarios de bloque /* */
    bool keep_whitespace      = false; ///< Preservar tokens WHITESPACE en directivas
    /**
     * @brief Reconocer cadenas crudas de C++ (`R"delim( ... )delim"`).
     *
     * Se puede apagar para un lenguaje en el que un identificador acabado en R
     * pueda ir pegado a una comilla y signifique otra cosa.  Activado por
     * omision porque el tratamiento de comillas ya es el de C.
     */
    bool raw_strings         = true;

    /**
     * @brief Tratar `"` como comienzo de un literal de cadena.
     *
     * Apagarlo hace que la comilla sea texto corriente.  Hace falta en un
     * lenguaje -- o en texto llano -- donde no delimite nada, porque entonces
     * la comilla de apertura no tiene cierre y el literal se come el resto.
     */
    bool strings             = true;

    /**
     * @brief Tratar `'` como comienzo de un literal de caracter.
     *
     * Va aparte de `strings` porque son dos decisiones distintas: SQL usa la
     * comilla simple para cadenas y la doble para identificadores, Rust la
     * simple tambien para tiempos de vida, y en texto llano es un apostrofo.
     * Con esto encendido, `It's a test` fallaba con "literal de cadena sin
     * cerrar" -- una frase corriente en ingles.
     */
    bool char_literals       = true;
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
    /**
     * @brief Nombre del archivo, ya compartido.
     *
     * Se interna UNA vez al construir el lexer y desde ahi cada token recibe el
     * mismo puntero.  Guardarlo por valor obligaba a copiar la ruta en cada
     * ubicacion, y eso es una reserva de memoria por token.
     */
    const std::string* m_file;
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
    /**
     * @brief Escanea un "numero de preprocesado" segun la regla del estandar.
     *
     * Mas amplia que la de un numero: absorbe letras, guiones bajos, puntos y
     * el signo que sigue a un exponente, de modo que 200112L o 1.5e-9 son UN
     * solo token.  Partirlos rompe el pegado con ##.
     *
     * @return Token NUMBER con el literal completo.
     */
    PPToken scan_pp_number();

    PPToken scan_number();

    /**
     * @brief Escanea un literal de cadena con el delimitador indicado.
     * @param delim Caracter delimitador de cierre ('"' o '\'').
     * @return Token STRING o CHAR_LIT con el contenido de la cadena.
     */
    PPToken scan_string(char delim);

    /**
     * @brief Lee una cadena cruda de C++ a partir de su prefijo.
     * @param s Texto ya leido (el prefijo), al que se anade el resto.
     * @param l Ubicacion del comienzo del literal.
     * @return Token STRING con el literal entero, delimitadores incluidos.
     */
    PPToken scan_raw_string(std::string s, const SourceLocation& l);

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
    /// Se come un comentario de bloque y devuelve cuantos saltos de linea
    /// ocupaba, para que quien llama los reponga (ver la implementacion).
    size_t skip_block_comment();

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
