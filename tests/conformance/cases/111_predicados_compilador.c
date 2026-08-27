/* Ademas de los __has_*, cada compilador trae predicados propios que solo el
   sabe contestar: clang tiene __is_target_arch y companeros, y las cabeceras
   de macOS los usan de verdad.  vpp los delega igual que los __has_*, pero
   solo si el compilador de destino LOS TIENE -- si no, un identificador
   cualquiera seguido de un parentesis se tomaria por uno de ellos y una
   expresion mal escrita se quedaria sin diagnostico.

   El caso vale con cualquier compilador de referencia porque se compara contra
   EL MISMO: con clang entra en la rama de los predicados, con gcc no. */

#if defined(__has_builtin)
 #if __has_builtin(__is_target_arch)
  #if (__is_target_arch(x86_64) && __is_target_vendor(unknown))
es_x86_64_unknown
  #else
otro_objetivo
  #endif
 #else
sin_is_target
 #endif
#else
sin_has_builtin
#endif

/* El juego de operadores depende del MODO, no solo del compilador: clang tiene
   __has_cpp_attribute compilando C++ y no compilando C.  Darlo por bueno sin
   preguntar lleva a consultarlo y a que el compilador rechace la consulta. */
#if defined(__has_cpp_attribute)
tiene_cpp_attribute
#else
no_tiene_cpp_attribute
#endif
