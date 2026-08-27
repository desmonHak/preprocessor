#define LOG(fmt, ...) printf(fmt __VA_OPT__(,) __VA_ARGS__)
LOG("hola")
LOG("%d", 7)
