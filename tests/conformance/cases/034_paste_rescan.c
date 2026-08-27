#define PEGA(v) ___X_##v
#define ___X_200112L (vacio)
#define ___X_ABC     (letras)
a = PEGA(200112L)
b = PEGA(ABC)
#define SUF(v) num_##v
#define num_1L (uno_ele)
d = SUF(1L)
e = 1.5e-9 0x1p+3 200112L
