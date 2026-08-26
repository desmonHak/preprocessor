@echo off
setlocal EnableDelayedExpansion
rem ===========================================================================
rem build_all_abis.bat  --  construye vpp para todas las ABI de Windows y genera
rem                         un unico instalador que las contiene todas.
rem
rem El problema que resuelve: una biblioteca estatica NO es intercambiable entre
rem MSVC y MinGW (distinto mangling de C++, distinto modelo de excepciones,
rem distinta STL) ni entre 32 y 64 bits.  No hay conversion posible: hay que
rem compilar una vez por ABI.  Este script hace esas pasadas y las fusiona.
rem
rem Uso:
rem     cmake\build_all_abis.bat [opciones]
rem
rem Opciones:
rem     --build-root <dir>   Raiz de los directorios de build (def: build-abis)
rem     --vcvarsall <ruta>   vcvarsall.bat de Visual Studio.  Sin esto se omiten
rem                          las ABI de MSVC (se avisa, no es un error).
rem     --mingw-cxx <ruta>   g++ de MinGW (def: el del PATH)
rem     --no-mingw           Omite las ABI de MinGW
rem     --no-msvc            Omite las ABI de MSVC
rem     --no-installer       Compila e instala, pero no genera el .exe
rem
rem Ejemplo:
rem     cmake\build_all_abis.bat --vcvarsall "U:\visual\VC\Auxiliary\Build\vcvarsall.bat"
rem ===========================================================================

set "SRC_DIR=%~dp0.."
set "BUILD_ROOT=%CD%\build-abis"
set "VCVARSALL="
set "MINGW_CXX="
set "DO_MINGW=1"
set "DO_MSVC=1"
set "DO_INSTALLER=1"

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--build-root"   ( set "BUILD_ROOT=%~2" & shift & shift & goto parse )
if /I "%~1"=="--vcvarsall"    ( set "VCVARSALL=%~2"  & shift & shift & goto parse )
if /I "%~1"=="--mingw-cxx"    ( set "MINGW_CXX=%~2"  & shift & shift & goto parse )
if /I "%~1"=="--no-mingw"     ( set "DO_MINGW=0"     & shift & goto parse )
if /I "%~1"=="--no-msvc"      ( set "DO_MSVC=0"      & shift & goto parse )
if /I "%~1"=="--no-installer" ( set "DO_INSTALLER=0" & shift & goto parse )
echo [ERROR] opcion desconocida: %~1
exit /b 2
:parsed

if "%DO_MSVC%"=="1" if "%VCVARSALL%"=="" (
    echo [aviso] sin --vcvarsall no se pueden construir las ABI de MSVC; se omiten.
    set "DO_MSVC=0"
)
if "%DO_MSVC%"=="1" if not exist "%VCVARSALL%" (
    echo [ERROR] no existe el vcvarsall indicado: %VCVARSALL%
    exit /b 2
)

echo.
echo === vpp: construccion multi-ABI ===
echo   Fuentes    : %SRC_DIR%
echo   Builds     : %BUILD_ROOT%
echo   MinGW      : %DO_MINGW%
echo   MSVC       : %DO_MSVC%
echo.

rem Lista de "tag=directorio" que se pasara al build primario.
set "EXTRA_ABIS="
set "STAGE_ROOT=%BUILD_ROOT%\_stage"
set "PRIMARY_DIR="
set "PRIMARY_TAG="

rem ---------------------------------------------------------------------------
rem MinGW x64 y x86.  El primero que se construya sera el build PRIMARIO: es el
rem que aporta el ejecutable, las cabeceras y la documentacion, y el que corre
rem CPack al final.
rem ---------------------------------------------------------------------------
if "%DO_MINGW%"=="1" (
    call :build_mingw x64 ""   || exit /b 1
    call :build_mingw x86 -m32 || exit /b 1
)

rem ---------------------------------------------------------------------------
rem MSVC x64 y x86.
rem ---------------------------------------------------------------------------
if "%DO_MSVC%"=="1" (
    call :build_msvc x64 x64 || exit /b 1
    call :build_msvc x86 x86 || exit /b 1
)

if "%PRIMARY_DIR%"=="" (
    echo [ERROR] no se construyo ninguna ABI.
    exit /b 1
)

rem ---------------------------------------------------------------------------
rem Reconfigurar el build primario enganchandole las demas ABI, y empaquetar.
rem ---------------------------------------------------------------------------
echo.
echo === Reconfigurando el build primario (%PRIMARY_TAG%) con las ABI extra ===
if "%EXTRA_ABIS%"=="" (
    echo   (ninguna ABI adicional)
) else (
    echo   %EXTRA_ABIS%
)
cmake -S "%SRC_DIR%" -B "%PRIMARY_DIR%" -DVPP_EXTRA_ABI_STAGES="%EXTRA_ABIS%" || exit /b 1

if "%DO_INSTALLER%"=="0" (
    echo.
    echo === Listo: builds en %BUILD_ROOT% ===
    exit /b 0
)

echo.
echo === Generando el instalador ===
cmake --build "%PRIMARY_DIR%" --target installer || exit /b 1

echo.
echo === Listo ===
dir /b "%PRIMARY_DIR%\vpp-*.exe" 2>nul
exit /b 0

rem ===========================================================================
rem :build_mingw <arch> <flag-m32-o-vacio>
rem ===========================================================================
:build_mingw
set "_arch=%~1"
set "_m32=%~2"
set "_tag=%_arch%-mingw"
set "_dir=%BUILD_ROOT%\%_tag%"
echo.
echo --- %_tag% ---

rem El build primario instala tambien lo comun (exe, cabeceras, docs); los
rem demas solo aportan su biblioteca, para no duplicar ni pisar nada.
if "%PRIMARY_DIR%"=="" ( set "_common=ON" ) else ( set "_common=OFF" )
if "%PRIMARY_DIR%"=="" ( set "_exe=ON" )    else ( set "_exe=OFF" )

set "_cxxflags=%_m32%"
cmake -S "%SRC_DIR%" -B "%_dir%" -G "MinGW Makefiles" ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_FLAGS="%_cxxflags%" ^
      -DCMAKE_EXE_LINKER_FLAGS="%_cxxflags%" ^
      -DCMAKE_SHARED_LINKER_FLAGS="%_cxxflags%" ^
      -DVPP_BUILD_TESTS=OFF ^
      -DVPP_BUILD_EXE=%_exe% ^
      -DVPP_INSTALL_COMMON=%_common% || exit /b 1
cmake --build "%_dir%" -j || exit /b 1

rem Los builds secundarios se PREINSTALAN en su propio staging: el primario
rem solo copiara ese arbol ya resuelto, sin necesitar su herramienta de
rem construccion al empaquetar (ver el comentario en VppPackaging.cmake).
if not "%PRIMARY_DIR%"=="" (
    cmake --install "%_dir%" --prefix "%STAGE_ROOT%\%_tag%" >nul || exit /b 1
)

call :register_abi "%_tag%" "%_dir%" "%STAGE_ROOT%\%_tag%"
exit /b 0

rem ===========================================================================
rem :build_msvc <tag-arch> <vcvarsall-arch>
rem ===========================================================================
:build_msvc
set "_arch=%~1"
set "_vcarch=%~2"
set "_tag=%_arch%-msvc"
set "_dir=%BUILD_ROOT%\%_tag%"
echo.
echo --- %_tag% ---

if "%PRIMARY_DIR%"=="" ( set "_common=ON" ) else ( set "_common=OFF" )
if "%PRIMARY_DIR%"=="" ( set "_exe=ON" )    else ( set "_exe=OFF" )

rem vcvarsall solo afecta al proceso que lo llama, asi que configurar, compilar
rem e instalar tienen que ocurrir DENTRO del mismo cmd que lo ejecuta.  Se hace
rem via un .bat temporal en vez de un `cmd /C "... ^ ..."` porque cmd no procesa
rem las continuaciones de linea dentro del argumento de /C.
rem Se lanza con `cmd /C` y no con `call` para que el entorno de vcvars quede
rem AISLADO: si contaminase este proceso, la siguiente ABI heredaria el
rem compilador de la anterior.
if not exist "%BUILD_ROOT%" mkdir "%BUILD_ROOT%"
set "_bat=%BUILD_ROOT%\_msvc_%_tag%.bat"
> "%_bat%" echo @echo off
>>"%_bat%" echo call "%VCVARSALL%" %_vcarch% ^>nul
>>"%_bat%" echo if errorlevel 1 exit /b 1
>>"%_bat%" echo cmake -S "%SRC_DIR%" -B "%_dir%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DVPP_BUILD_TESTS=OFF -DVPP_BUILD_EXE=%_exe% -DVPP_INSTALL_COMMON=%_common%
>>"%_bat%" echo if errorlevel 1 exit /b 1
>>"%_bat%" echo cmake --build "%_dir%"
>>"%_bat%" echo if errorlevel 1 exit /b 1
if not "%PRIMARY_DIR%"=="" (
    >>"%_bat%" echo cmake --install "%_dir%" --prefix "%STAGE_ROOT%\%_tag%" ^>nul
    >>"%_bat%" echo if errorlevel 1 exit /b 1
)
cmd /C "%_bat%" || exit /b 1

call :register_abi "%_tag%" "%_dir%" "%STAGE_ROOT%\%_tag%"
exit /b 0

rem ===========================================================================
rem :register_abi <tag> <build-dir> <stage-dir>
rem     El primero que llega es el PRIMARIO (aporta exe, cabeceras y docs, y es
rem     el que empaqueta).  Los demas se apuntan por su directorio de staging.
rem ===========================================================================
:register_abi
if "%PRIMARY_DIR%"=="" (
    set "PRIMARY_DIR=%~2"
    set "PRIMARY_TAG=%~1"
) else (
    if "!EXTRA_ABIS!"=="" (
        set "EXTRA_ABIS=%~1=%~3"
    ) else (
        set "EXTRA_ABIS=!EXTRA_ABIS!;%~1=%~3"
    )
)
exit /b 0
