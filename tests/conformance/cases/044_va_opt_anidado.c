#define F(a, ...) [a __VA_OPT__(| __VA_ARGS__)]
F(1)
F(1, 2, 3)
