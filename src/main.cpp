/**
 * @file main.cpp
 * @brief Punto de entrada del ejecutable del preprocesador vpp.
 *
 * Uso:
 *   vpp [opciones] archivo_entrada
 *
 * Opciones:
 *   -o <archivo>      Archivo de salida (por defecto: stdout)
 *   -D NAME[=val]     Define una macro
 *   -I <ruta>         Anade una ruta de busqueda para #include
 *   --line-markers    Emite marcadores #line tras cada #include
 *   --no-expand       No expande macros en texto plano
 *   --stdin           Lee el fuente desde stdin
 *   -v, --version     Muestra la version del preprocesador
 *   -h, --help        Muestra esta ayuda
 */

#include "preprocessor/preprocessor.h"
#include "preprocessor/pp_deps.h"
#include "preprocessor/pp_diag_render.h"

#include <cstdlib>
#ifdef _WIN32
    #include <io.h>
#else
    #include <unistd.h>
#endif
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>

static void print_version() {
    std::cout << "vpp - preprocesador Vesta v1.0.0\n"
              << "Copyright (c) 2026 David Lopez T.\n";
}

static void print_help(const char* prog) {
    std::cout
        << "Uso: " << prog << " [opciones] <archivo_entrada>\n\n"
        << "Opciones:\n"
        << "  -o <archivo>      Archivo de salida (por defecto: stdout)\n"
        << "  -D NAME[=val]     Define una macro antes de procesar\n"
        << "  -I <ruta>         Anade ruta de busqueda para #include <...>\n"
        << "  -U NAME           Quita una macro (tambien -UNAME)\n"
        << "  -isystem <ruta>   Ruta de sistema: se busca despues de las -I y\n"
        << "                    lo que aparezca en ella queda fuera de -MM\n"
        << "  -M <ruta>         Anade ruta de busqueda para #import <...>\n"
        << "  --predef <f>      Precarga las directivas de un fichero\n"
        << "  --predef-from <c> Precarga las directivas que emita un comando,\n"
        << "                    p.ej. --predef-from \"gcc -dM -E -\"\n"
        << "  --capabilities-from <c>\n"
        << "                    Con que resolver __has_builtin y companeros,\n"
        << "                    p.ej. --capabilities-from \"gcc -E -P -x c\"\n"
        << "  --cache-dir <d>   Donde recordar lo que contesta el compilador\n"
        << "                    (por omision, la cache del usuario)\n"
        << "  --no-cache        No recuerda nada entre ejecuciones\n"
        << "  --deps            Emite la lista de dependencias en formato make\n"
        << "                    en lugar de la salida (el -M de cc, que aqui ya\n"
        << "                    significa ruta de #import)\n"
        << "  -MD               Emite las dependencias ADEMAS de la salida\n"
        << "  -MM               Como --deps, sin las cabeceras de sistema\n"
        << "  -MMD              Como -MD, sin las cabeceras de sistema\n"
        << "  -MF <f>           Escribe las dependencias en ese fichero\n"
        << "  -MT <t>           Nombre del objetivo de la regla\n"
        << "  -MP               Anade una regla vacia por dependencia\n"
        << "  --marker <s>      Que marca una directiva (por omision '#');\n"
        << "                    el fichero puede decirlo con 'vpp:marker=s'\n"
        << "  --line-markers    Emite marcadores #line tras cada #include\n"
        << "  --no-expand       Desactiva la expansion de macros en texto\n"
        << "  --stdin           Lee el fuente de la entrada estandar\n"
        << "  -v, --version     Muestra la version y termina\n"
        << "  -h, --help        Muestra esta ayuda y termina\n\n"
        << "Stdlib (include_lib):\n"
        << "  La carpeta include_lib/ se detecta automaticamente junto al\n"
        << "  ejecutable o en el nivel superior del directorio de trabajo.\n"
        << "  Uso: #import <vesta>           -- todos los modulos\n"
        << "        #import <vesta/types>     -- tipos y rangos enteros\n"
        << "        #import <vesta/registers> -- aliases de registros\n"
        << "        #import <vesta/io>        -- constantes de E/S\n"
        << "        #import <vesta/math>      -- constantes IEEE 754\n"
        << "        #import <vesta/platform>  -- deteccion de plataforma\n\n"
        << "Macros predefinidas de plataforma:\n"
        << "  __VPP_WINDOWS__  __VPP_LINUX__  __VPP_MACOS__\n"
        << "  __VPP_X86_64__   __VPP_AARCH64__  __POINTER_WIDTH__\n"
        << "  __FILE__  __LINE__  __COUNTER__\n"
        << "  __DATE__  __TIME__\n"
        << "  __VPP_VERSION__  __VPP_VERSION_MAJOR__  __VPP_VERSION_MINOR__\n\n"
        << "Directivas soportadas:\n"
        << "  #define  #undef  #include  #import  #if  #ifdef  #ifndef  #elif  #else  #endif\n"
        << "  #error  #warning  #pragma  #line\n"
        << "  #foreach VAR in (list)  #foreach VAR, IDX in ARRAY  #endforeach\n"
        << "  #repeat N  #endrepeat\n"
        << "  #array NAME (...)  #set VAR [op=] expr\n"
        << "  #exec VAR cmd  #assert expr [msg]\n"
        << "  #macro NAME(params) ... #endmacro\n";
}

// detecta rutas de stdlib relativas al ejecutable y las agrega a import_paths
static void add_auto_stdlib(std::vector<std::string>& import_paths, const char* argv0) {
    namespace fs = std::filesystem;

    auto try_push = [&](const fs::path& p) {
        try {
            if (fs::is_directory(p)) {
                import_paths.push_back(fs::weakly_canonical(p).string());
            }
        } catch (...) {}
    };

    // resolver el directorio del ejecutable
    try {
        fs::path exe = fs::weakly_canonical(fs::absolute(fs::path(argv0)));
        fs::path exe_dir = exe.parent_path();
        // caso 1: include_lib junto al ejecutable (layout instalado o build con POST_BUILD)
        try_push(exe_dir / "include_lib");
        // caso 2: include_lib un nivel arriba (desarrollo: exe en build/, fuentes en ../)
        try_push(exe_dir.parent_path() / "include_lib");
    } catch (...) {}

    // caso 3: include_lib en el directorio de trabajo actual
    try {
        try_push(fs::current_path() / "include_lib");
    } catch (...) {}
}


/**
 * @brief Decide si conviene resaltar la salida de error.
 *
 * Solo si va a una terminal.  Redirigida a un fichero o a otro programa, las
 * secuencias ANSI son basura que ensucia lo que despues alguien tiene que leer
 * o parsear.  Se respeta ademas NO_COLOR, que es la convencion para apagarlo.
 *
 * @return true si se debe colorear.
 */
static bool salida_con_color() {
    if (std::getenv("NO_COLOR") != nullptr) return false;
#ifdef _WIN32
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}
int main(int argc, char* argv[]) {
    // --- parsear argumentos de la linea de comandos ---
    std::string input_file;
    std::string output_file;
    std::vector<std::string> defines;
    std::vector<vpp::PredefSource> predef_sources;
    std::string capabilities_cmd;
    std::string cache_dir;
    std::string marker;
    bool        emitir_deps = false;
    bool        solo_deps   = false;
    bool        deps_sin_sistema = false;
    std::string deps_file;
    vpp::DepsOptions deps_opts;
    bool        use_cache = true;
    std::vector<std::string> include_paths;
    std::vector<std::string> system_include_paths;
    std::vector<std::string> undefines;
    std::vector<std::string> import_paths;
    bool line_markers = false;
    bool no_expand    = false;
    bool from_stdin   = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0 ||
            std::strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "-v") == 0 ||
            std::strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (std::strcmp(argv[i], "--stdin") == 0) {
            from_stdin = true;
            continue;
        }
        if (std::strcmp(argv[i], "--line-markers") == 0) {
            line_markers = true;
            continue;
        }
        if (std::strcmp(argv[i], "--no-expand") == 0) {
            no_expand = true;
            continue;
        }
        if (std::strncmp(argv[i], "-D", 2) == 0) {
            // -DNAME o -D NAME
            if (argv[i][2] != '\0') {
                defines.push_back(argv[i] + 2);
            } else if (i + 1 < argc) {
                defines.push_back(argv[++i]);
            }
            continue;
        }
        // Ruta de sistema: se busca DESPUES de las propias, y lo que aparezca
        // en ella queda marcado como ajeno para `-MM`.
        if (std::strcmp(argv[i], "-isystem") == 0 && i + 1 < argc) {
            system_include_paths.push_back(argv[++i]);
            continue;
        }
        // Quitar una macro.  Se admite `-U NAME` y `-UNAME`, como cc.
        if (std::strncmp(argv[i], "-U", 2) == 0) {
            if (argv[i][2] != '\0') {
                undefines.push_back(argv[i] + 2);
            } else if (i + 1 < argc) {
                undefines.push_back(argv[++i]);
            }
            continue;
        }
        if (std::strncmp(argv[i], "-I", 2) == 0) {
            if (argv[i][2] != '\0') {
                include_paths.push_back(argv[i] + 2);
            } else if (i + 1 < argc) {
                include_paths.push_back(argv[++i]);
            }
            continue;
        }
        /* --- dependencias para el build ---
         *
         * Se miran ANTES que `-M`, que en vpp es una ruta de busqueda para
         * `#import` y ademas casa por PREFIJO: sin esto se tragaria `-MD`,
         * `-MF` y compania como si fueran rutas.  Al exigir coincidencia exacta
         * aqui, una ruta escrita `-MDocs` sigue funcionando como siempre; solo
         * chocaria una que se llamase exactamente D, F, T o P.
         *
         * Los nombres son los de cc para que un build system los emita tal
         * cual.  El unico que no se puede reusar es el `-M` a secas, que ya
         * estaba cogido; su equivalente es `--deps`. */
        if (std::strcmp(argv[i], "--deps") == 0) {
            emitir_deps = true;
            solo_deps   = true;
            continue;
        }
        if (std::strcmp(argv[i], "-MM") == 0) {
            emitir_deps  = true;
            solo_deps    = true;
            deps_sin_sistema = true;
            continue;
        }
        if (std::strcmp(argv[i], "-MMD") == 0) {
            emitir_deps      = true;
            deps_sin_sistema = true;
            continue;
        }
        if (std::strcmp(argv[i], "-MD") == 0) {
            emitir_deps = true;
            continue;
        }
        if (std::strcmp(argv[i], "-MF") == 0 && i + 1 < argc) {
            deps_file   = argv[++i];
            emitir_deps = true;
            continue;
        }
        if (std::strcmp(argv[i], "-MT") == 0 && i + 1 < argc) {
            deps_opts.target = argv[++i];
            emitir_deps      = true;
            continue;
        }
        if (std::strcmp(argv[i], "-MP") == 0) {
            deps_opts.phony_targets = true;
            emitir_deps             = true;
            continue;
        }
        if (std::strncmp(argv[i], "-M", 2) == 0) {
            // -Mruta o -M ruta: ruta de busqueda para #import <...>
            if (argv[i][2] != '\0') {
                import_paths.push_back(argv[i] + 2);
            } else if (i + 1 < argc) {
                import_paths.push_back(argv[++i]);
            }
            continue;
        }
        // Precarga de macros desde un fichero de directivas.  Es generico: no
        // sabe nada de compiladores ni de lenguajes, solo dice "carga estas
        // directivas antes de empezar".
        if (std::strcmp(argv[i], "--predef") == 0 && i + 1 < argc) {
            predef_sources.push_back(
                vpp::PredefSource{vpp::PredefKind::File, argv[++i]});
            continue;
        }

        // Con que preguntar por las capacidades del compilador de destino
        // (__has_builtin y companeros).  Se apunta al binario EXACTO por el
        // mismo motivo que en --predef-from: en una maquina conviven varios
        // compiladores y cada uno responde distinto a la misma pregunta.
        if (std::strcmp(argv[i], "--capabilities-from") == 0 && i + 1 < argc) {
            capabilities_cmd = argv[++i];
            continue;
        }

        // Donde recordar entre ejecuciones lo que contesta el compilador.  Por
        // omision es la cache del usuario, porque lo que se guarda son hechos
        // sobre un COMPILADOR y valen igual en todos sus proyectos; se cambia
        // aqui cuando la compilacion tiene que ser reproducible o aislada.
        if (std::strcmp(argv[i], "--cache-dir") == 0 && i + 1 < argc) {
            cache_dir = argv[++i];
            continue;
        }


        // Marcador de directiva para ficheros que no se pueden tocar --
        // generados, o de terceros -- donde no hay donde poner la declaracion.
        // Si el fichero SI la trae, gana la suya por ser mas concreta.
        if (std::strcmp(argv[i], "--marker") == 0 && i + 1 < argc) {
            marker = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--no-cache") == 0) {
            use_cache = false;
            continue;
        }

        // Precarga desde la salida de un comando.  Se apunta al BINARIO exacto
        // en lugar de a un nombre de compilador conocido, de modo que conviven
        // varias versiones y varios toolchains en la misma maquina:
        //     --predef-from "gcc-12 -dM -E -"
        //     --predef-from "clang-15 -dM -E -x c++ -"
        if (std::strcmp(argv[i], "--predef-from") == 0 && i + 1 < argc) {
            predef_sources.push_back(
                vpp::PredefSource{vpp::PredefKind::Command, argv[++i]});
            continue;
        }

        if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
            continue;
        }
        // argumento sin flag: archivo de entrada
        if (!input_file.empty()) {
            std::cerr << "vpp: argumento inesperado: " << argv[i] << '\n';
            return 1;
        }
        input_file = argv[i];
    }

    if (!from_stdin && input_file.empty()) {
        std::cerr << "vpp: no se especifico archivo de entrada\n";
        print_help(argv[0]);
        return 1;
    }

    // --- configurar el preprocesador ---
    bool had_error = false;
    /* El diagnostico se guarda y se imprime al final, no aqui.
     *
     * Para citar la linea culpable hace falta el fuente, y el preprocesador
     * todavia lo esta leyendo cuando emite el mensaje.  Guardarlos y sacarlos
     * despues es lo que permite ensenar el contexto; y de paso salen en orden
     * aunque la salida vaya al mismo sitio. */
    const bool usar_color = salida_con_color();
    std::vector<vpp::Diagnostic> diagnosticos;
    vpp::Preprocessor pp([&had_error, &diagnosticos](const vpp::Diagnostic& d) {
        diagnosticos.push_back(d);
        if (d.level >= vpp::DiagLevel::ERR) had_error = true;
    });

    // agregar rutas de stdlib detectadas automaticamente (antes de las del usuario)
    add_auto_stdlib(import_paths, argv[0]);

    auto& opts = pp.options();
    opts.emit_line_markers = line_markers;
    opts.expand_macros     = !no_expand;
    opts.include_paths     = include_paths;
    opts.system_include_paths = system_include_paths;
    opts.undefines            = undefines;
    opts.import_paths      = import_paths;
    opts.predefines        = defines;
    opts.predef_sources    = predef_sources;
    opts.capabilities_command = capabilities_cmd;
    opts.cache_dir            = cache_dir;
    if (!marker.empty()) opts.lexer.directive_marker = marker;
    opts.use_cache            = use_cache;

    // --- leer el fuente ---
    std::string source;
    std::string filename;

    if (from_stdin || input_file.empty()) {
        filename = "<stdin>";
        source.assign(std::istreambuf_iterator<char>(std::cin),
                      std::istreambuf_iterator<char>());
    } else {
        filename = input_file;
        std::ifstream ifs(input_file, std::ios::binary);
        if (!ifs) {
            std::cerr << "vpp: no se puede abrir el archivo: " << input_file << '\n';
            return 1;
        }
        source.assign(std::istreambuf_iterator<char>(ifs),
                      std::istreambuf_iterator<char>());
    }

    // --- procesar ---
    std::string result = pp.process(source, filename);

    /* Ahora si se imprimen: los fuentes ya estan leidos y cada mensaje puede
     * citar su linea.  Va ANTES de rendirse por los errores, que si no los
     * mensajes que explican el fallo no llegarian a verse. */
    for (const auto& d : diagnosticos) {
        std::cerr << vpp::render_diagnostic(d, pp.sources(), usar_color) << '\n';
    }

    if (had_error) {
        std::cerr << "vpp: preprocesamiento fallido con "
                  << pp.diagnostics().error_count() << " error(es)\n";
        return 1;
    }
    // --- lista de dependencias ---
    //
    // Se escribe antes que la salida para que, con `-M`, lo que llegue a stdout
    // sea SOLO la regla: es lo que espera quien la canaliza a un fichero.
    if (emitir_deps) {
        const std::string regla = vpp::format_make_deps(
            output_file, input_file,
            deps_sin_sistema ? pp.user_included_files() : pp.included_files(),
            deps_opts);

        if (deps_file.empty()) {
            std::cout << regla;
        } else {
            std::ofstream dfs(deps_file, std::ios::binary);
            if (!dfs) {
                std::cerr << "vpp: no se puede crear el fichero de "
                             "dependencias: " << deps_file << '\n';
                return 1;
            }
            dfs << regla;
        }
    }

    // --- escribir la salida ---
//    // Con `-M` no hay salida: lo unico que se pide es la regla.  Es asi en cc    // y es lo que hace que se pueda canalizar sin filtrar nada.
    if (solo_deps) {
        // nada que emitir
    } else if (output_file.empty()) {
        std::cout << result;
    } else {
        std::ofstream ofs(output_file, std::ios::binary);
        if (!ofs) {
            std::cerr << "vpp: no se puede crear el archivo de salida: "
                      << output_file << '\n';
            return 1;
        }
        ofs << result;
    }

    // mostrar resumen de advertencias si hay
    if (pp.diagnostics().warning_count() > 0) {
        std::cerr << "vpp: " << pp.diagnostics().warning_count()
                  << " advertencia(s)\n";
    }

    return 0;
}
