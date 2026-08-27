#define N 2
#if N == 1
uno
#elif N == 2
dos
#else
otro
#endif
#ifdef N
#  ifndef M
anidado
#  endif
#endif
