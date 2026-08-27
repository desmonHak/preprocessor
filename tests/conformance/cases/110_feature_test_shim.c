/* Los operadores de prueba de caracteristicas tienen que constar como
   DEFINIDOS, no solo contestar.  Las bibliotecas estandar se protegen con el
   patron de abajo, y un preprocesador que no se declare capaz acaba con una
   macro que responde 0 a todo: a partir de ahi ninguna consulta vale nada y
   las cabeceras se van por ramas que su compilador no usa.  Asi moria libc++
   en un #error. */

#ifdef __has_builtin
ve_has_builtin
#else
no_ve_has_builtin
#endif

#if defined(__has_include)
ve_has_include
#else
no_ve_has_include
#endif

#ifdef __has_attribute
ve_has_attribute
#else
no_ve_has_attribute
#endif

/* El shim de compatibilidad: si el de arriba dijo que si, este no se instala y
   la consulta de abajo llega de verdad al compilador. */
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if __has_builtin(__builtin_expect)
tiene_builtin_expect
#else
no_tiene_builtin_expect
#endif

/* __has_include lo contesta vpp por su cuenta, sin compilador.  Se prueba con
   un vecino y no con una cabecera del sistema: el corpus corre sin rutas de
   busqueda, asi que preguntar por <stddef.h> mediria la configuracion del
   compilador de referencia y no la conformidad de vpp.  Las del sistema las
   cubre la prueba de integracion. */
#if __has_include("inc.h")
tiene_vecino
#else
no_tiene_vecino
#endif

#if __has_include("no_existe_jamas.h")
tiene_fantasma
#else
no_tiene_fantasma
#endif
