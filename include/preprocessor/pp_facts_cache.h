/**
 * @file pp_facts_cache.h
 * @brief Memoria entre ejecuciones de lo que un compilador contesta.
 */
#pragma once

#include <string>
#include <unordered_map>

namespace vpp {

/**
 * @brief Recuerda entre ejecuciones lo que un compilador ha contestado.
 *
 * Preguntarle a un compilador cuesta lanzar un proceso -- unos 23 ms medidos --
 * y una unidad de traduccion de C++ pregunta decenas de veces.  La memoria que
 * ya habia duraba lo que el proceso, asi que cada fichero de una compilacion
 * volvia a pagarlo entero.  Esto lo baja a leer un fichero.
 *
 * Lo que se guarda son HECHOS SOBRE UN COMPILADOR, no sobre un proyecto: que
 * este `gcc` tenga `__builtin_expect` no depende de lo que se este compilando.
 * De ahi que la memoria sea del usuario y se comparta entre todos sus
 * proyectos, en vez de calentarse una vez en cada uno.
 *
 * Lo que NO cabe aqui es nada que dependa del proyecto.  `__has_include` es el
 * ejemplo: pregunta por las rutas de busqueda de quien compila, no por el
 * compilador, y recordarlo daria respuestas de otro proyecto.  Se contesta
 * aparte y no llega hasta aqui.
 *
 * @par Concurrencia
 * Una compilacion en paralelo lanza muchos vpp a la vez sobre la misma memoria.
 * Por eso hay un fichero por compilador y se escribe entero de una vez (ver
 * write_file_atomically): nadie lee nunca uno a medias y no hace falta ningun
 * cerrojo.  Si dos procesos se pisan, lo peor que pasa es que una respuesta se
 * vuelva a preguntar -- cada entrada es una funcion pura de (compilador,
 * pregunta), asi que dos escritores no pueden discrepar y siempre convergen.
 */
class FactsCache {
public:
    /**
     * @brief Constructor.
     *
     * @param dir         Directorio donde vive la memoria.  Vacio la desactiva.
     * @param fingerprint Huella del compilador (ver CompilerId).  Vacia la
     *                    desactiva, porque sin saber a quien se pregunta no hay
     *                    forma de notar que ha cambiado y recordar seria peor
     *                    que no recordar.
     */
    FactsCache(std::string dir, std::string fingerprint);

    /** @brief Destructor: escribe lo aprendido si hay algo nuevo. */
    ~FactsCache();

    FactsCache(const FactsCache&)            = delete;
    FactsCache& operator=(const FactsCache&) = delete;

    /** @brief Indica si la memoria esta operativa. @return true si lo esta. */
    bool enabled() const noexcept { return m_enabled; }

    /**
     * @brief Busca una respuesta ya conocida.
     *
     * @param key   Pregunta.
     * @param value Recibe la respuesta si estaba.
     * @return true si estaba.
     */
    bool lookup(const std::string& key, std::string& value) const;

    /**
     * @brief Guarda una respuesta.
     *
     * Solo deben entrar respuestas de verdad.  Un fallo al invocar al
     * compilador NO es una respuesta: guardarlo dejaria un problema pasajero
     * escrito como si fuera una propiedad del compilador, y el error
     * sobreviviria a su causa.
     *
     * @param key   Pregunta.
     * @param value Respuesta.
     */
    void store(const std::string& key, std::string value);

    /**
     * @brief Escribe a disco lo aprendido, si hay algo nuevo.
     *
     * Lo llama el destructor; se expone para poder forzarlo y para probarlo.
     *
     * @return true si se escribio.
     */
    bool flush();

    /** @brief Cuantas respuestas hay cargadas. @return Numero de entradas. */
    std::size_t size() const noexcept { return m_entries.size(); }

    /**
     * @brief Ruta del fichero de un compilador dentro de un directorio.
     *
     * Publica para que las pruebas puedan mirar lo que se escribio sin tener
     * que replicar como se nombra.
     *
     * @param dir         Directorio de la memoria.
     * @param fingerprint Huella del compilador.
     * @return Ruta del fichero.
     */
    static std::string file_path(const std::string& dir,
                                 const std::string& fingerprint);

private:
    /** @brief Carga el fichero de este compilador, si existe. */
    void load();

    std::string m_dir;          ///< Directorio de la memoria
    std::string m_fingerprint;  ///< Huella del compilador
    bool        m_enabled;      ///< false si falta el directorio o la huella
    bool        m_dirty;        ///< true si hay algo aun sin escribir

    /// Respuestas conocidas, por pregunta.
    std::unordered_map<std::string, std::string> m_entries;
};

} // namespace vpp
