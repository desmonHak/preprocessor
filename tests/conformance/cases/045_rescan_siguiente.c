/* El resultado de una expansion se relee JUNTO con los tokens que le siguen,
   no solo por su cuenta.  Sin eso, un nombre de macro funcion producido por una
   expansion nunca se encuentra con los parentesis que tiene al lado.

   Es el idioma con el que las cabeceras eligen macro segun el numero de
   argumentos, y con el que <stdio.h> de macOS define __API_AVAILABLE. */

#define ELIGE(_1,_2,_3,_4,_5,NOMBRE,...) NOMBRE
#define F1(a)         uno
#define F2(a,b)       dos
#define F3(a,b,c)     tres
#define F4(a,b,c,d)   cuatro
#define F5(a,b,c,d,e) cinco
#define F(...) ELIGE(__VA_ARGS__, F5, F4, F3, F2, F1)(__VA_ARGS__)

F(x)
F(x,y)
F(x,y,z)
F(x,y,z,w)
F(x,y,z,w,v)

/* Los argumentos pueden llevar parentesis propios sin descuadrar la cuenta. */
F(a(1), b(2), c(3), d(4))

/* Encadenado: cada expansion produce el nombre de la siguiente. */
#define G(x) H
#define H(y) fin
G(1)(2)

/* Una macro no puede reentrar en si misma por esta via. */
#define R(x) R
R(1)(2)
