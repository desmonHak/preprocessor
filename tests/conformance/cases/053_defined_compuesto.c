#define A 1
#define B 2
#if defined(A) && defined(B)
ambos
#endif
#if defined(A) && !defined(C)
solo_a
#endif
