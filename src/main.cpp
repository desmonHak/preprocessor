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
        << "  -M <ruta>         Anade ruta de busqueda para #import <...>\n"
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

int main(int argc, char* argv[]) {
    // --- parsear argumentos de la linea de comandos ---
    std::string input_file;
    std::string output_file;
    std::vector<std::string> defines;
    std::vector<std::string> include_paths;
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
        if (std::strncmp(argv[i], "-I", 2) == 0) {
            if (argv[i][2] != '\0') {
                include_paths.push_back(argv[i] + 2);
            } else if (i + 1 < argc) {
                include_paths.push_back(argv[++i]);
            }
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
    vpp::Preprocessor pp([&had_error](const vpp::Diagnostic& d) {
        std::cerr << d.format() << '\n';
        if (d.level >= vpp::DiagLevel::ERR) had_error = true;
    });

    // agregar rutas de stdlib detectadas automaticamente (antes de las del usuario)
    add_auto_stdlib(import_paths, argv[0]);

    auto& opts = pp.options();
    opts.emit_line_markers = line_markers;
    opts.expand_macros     = !no_expand;
    opts.include_paths     = include_paths;
    opts.import_paths      = import_paths;
    opts.predefines        = defines;

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

    if (had_error) {
        std::cerr << "vpp: preprocesamiento fallido con "
                  << pp.diagnostics().error_count() << " error(es)\n";
        return 1;
    }

    // --- escribir la salida ---
    if (output_file.empty()) {
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
