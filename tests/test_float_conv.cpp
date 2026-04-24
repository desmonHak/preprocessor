/**
 * @file test_float_conv.cpp
 * @brief Tests de macros funcion de flotantes y conversion numerica del preprocesador vpp.
 *
 * Las macros de flotantes trabajan con bits IEEE 754 de doubles de 64 bits.
 * Los valores se pasan como enteros que representan los bits del double.
 *
 * Constantes IEEE 754 usadas:
 *   0.0  -> 0x0000000000000000
 *   1.0  -> 0x3FF0000000000000
 *   2.0  -> 0x4000000000000000
 *   4.0  -> 0x4010000000000000
 *   pi   -> 0x400921FB54442D18
 */

#include "preprocessor/preprocessor.h"
#include <iostream>
#include <string>

using namespace vpp;

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, name) do { \
    if (!(cond)) { \
        std::cerr << "FALLO: " << name \
                  << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        ++tests_failed; \
    } else { \
        std::cout << "OK:    " << name << '\n'; \
        ++tests_passed; \
    } \
} while(0)

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static std::string preprocess(const std::string& src) {
    Preprocessor pp;
    return pp.process(src, "<test>");
}

/* =========================================================================
   F2BITS / BITS2F
   ========================================================================= */

static void test_f2bits_zero() {
    // 0.0 -> 0x0000000000000000 = 0
    auto out = preprocess("__F2BITS__(0.0)\n");
    CHECK(contains(out, "0"), "__F2BITS__: 0.0 -> 0");
}

static void test_f2bits_one() {
    // 1.0 -> 0x3FF0000000000000 = 4607182418800017408
    auto out = preprocess("__F2BITS__(1.0)\n");
    CHECK(contains(out, "4607182418800017408"), "__F2BITS__: 1.0 -> bits correctos");
}

static void test_bits2f_one() {
    auto out = preprocess("__BITS2F__(4607182418800017408)\n");
    CHECK(contains(out, "1"), "__BITS2F__: bits de 1.0 -> \"1\"");
}

/* =========================================================================
   Operaciones aritmeticas flotantes
   ========================================================================= */

static void test_fadd_1_plus_1() {
    // 1.0 + 1.0 = 2.0; bits de 2.0 = 0x4000000000000000 = 4611686018427387904
    auto out = preprocess(
        "__FADD__(4607182418800017408, 4607182418800017408)\n"
    );
    CHECK(contains(out, "4611686018427387904"), "__FADD__: 1.0+1.0 = 2.0 (bits)");
}

static void test_fsub_4_minus_2() {
    // 4.0 - 2.0 = 2.0; bits 4.0=0x4010000000000000=4616189618054758400
    auto out = preprocess(
        "__FSUB__(4616189618054758400, 4611686018427387904)\n"
    );
    CHECK(contains(out, "4611686018427387904"), "__FSUB__: 4.0-2.0 = 2.0 (bits)");
}

static void test_fmul_2_by_2() {
    // 2.0 * 2.0 = 4.0; bits 4.0=4616189618054758400
    auto out = preprocess(
        "__FMUL__(4611686018427387904, 4611686018427387904)\n"
    );
    CHECK(contains(out, "4616189618054758400"), "__FMUL__: 2.0*2.0 = 4.0 (bits)");
}

static void test_fdiv_4_by_2() {
    // 4.0 / 2.0 = 2.0
    auto out = preprocess(
        "__FDIV__(4616189618054758400, 4611686018427387904)\n"
    );
    CHECK(contains(out, "4611686018427387904"), "__FDIV__: 4.0/2.0 = 2.0 (bits)");
}

/* =========================================================================
   Funciones matematicas de flotantes
   ========================================================================= */

static void test_fabs_negative() {
    // |-1.0| = 1.0; bits de -1.0 = 0xBFF0000000000000 = -4616189618054758400
    auto out = preprocess(
        "__FABS__(-4616189618054758400)\n"
    );
    CHECK(contains(out, "4607182418800017408"), "__FABS__: |-1.0| = 1.0 (bits)");
}

static void test_fsqrt_4() {
    // sqrt(4.0) = 2.0; bits 4.0=4616189618054758400
    auto out = preprocess(
        "__FSQRT__(4616189618054758400)\n"
    );
    CHECK(contains(out, "4611686018427387904"), "__FSQRT__: sqrt(4.0) = 2.0 (bits)");
}

static void test_ffloor() {
    // floor(1.5) = 1.0; bits 1.5=0x3FF8000000000000=4609434218613702656
    auto out = preprocess(
        "__FFLOOR__(4609434218613702656)\n"
    );
    CHECK(contains(out, "4607182418800017408"), "__FFLOOR__: floor(1.5) = 1.0 (bits)");
}

static void test_fceil() {
    // ceil(1.5) = 2.0
    auto out = preprocess(
        "__FCEIL__(4609434218613702656)\n"
    );
    CHECK(contains(out, "4611686018427387904"), "__FCEIL__: ceil(1.5) = 2.0 (bits)");
}

static void test_ftoi() {
    // trunc(2.9) = 2; bits 2.9 = 4613712638259704627
    auto out = preprocess(
        "__FTOI__(4613712638259704627)\n"
    );
    CHECK(contains(out, "2"), "__FTOI__: trunc(2.9) = 2");
}

static void test_itof() {
    // (double)2 = 2.0; bits 2.0 = 4611686018427387904
    auto out = preprocess(
        "__ITOF__(2)\n"
    );
    CHECK(contains(out, "4611686018427387904"), "__ITOF__: (double)2 = 2.0 (bits)");
}

/* =========================================================================
   Comparaciones flotantes
   ========================================================================= */

static void test_feq_equal() {
    auto out = preprocess(
        "__FEQ__(4607182418800017408, 4607182418800017408)\n"
    );
    CHECK(contains(out, "1"), "__FEQ__: 1.0 == 1.0 -> 1");
}

static void test_feq_different() {
    auto out = preprocess(
        "__FEQ__(4607182418800017408, 4611686018427387904)\n"
    );
    CHECK(contains(out, "0"), "__FEQ__: 1.0 != 2.0 -> 0");
}

static void test_flt() {
    // 1.0 < 2.0
    auto out = preprocess(
        "__FLT__(4607182418800017408, 4611686018427387904)\n"
    );
    CHECK(contains(out, "1"), "__FLT__: 1.0 < 2.0 -> 1");
}

static void test_fgt() {
    // 2.0 > 1.0
    auto out = preprocess(
        "__FGT__(4611686018427387904, 4607182418800017408)\n"
    );
    CHECK(contains(out, "1"), "__FGT__: 2.0 > 1.0 -> 1");
}

static void test_fiszero() {
    auto out = preprocess("__FISZERO__(0)\n");
    CHECK(contains(out, "1"), "__FISZERO__: 0 es cero -> 1");
}

static void test_fpi() {
    // __FPI__() devuelve bits de pi = 4614256656552045848
    auto out = preprocess("__FPI__()\n");
    CHECK(contains(out, "4614256656552045848"), "__FPI__: bits de pi correctos");
}

/* =========================================================================
   Conversion numerica
   ========================================================================= */

static void test_dec2hex() {
    auto out = preprocess("__DEC2HEX__(255)\n");
    CHECK(contains(out, "ff"), "__DEC2HEX__: 255 -> \"ff\"");
}

static void test_dec2bin() {
    auto out = preprocess("__DEC2BIN__(10)\n");
    CHECK(contains(out, "1010"), "__DEC2BIN__: 10 -> \"1010\"");
}

static void test_dec2oct() {
    auto out = preprocess("__DEC2OCT__(8)\n");
    CHECK(contains(out, "10"), "__DEC2OCT__: 8 -> \"10\"");
}

static void test_hex2dec() {
    auto out = preprocess("__HEX2DEC__(ff)\n");
    CHECK(contains(out, "255"), "__HEX2DEC__: ff -> 255");
}

static void test_bin2dec() {
    auto out = preprocess("__BIN2DEC__(1010)\n");
    CHECK(contains(out, "10"), "__BIN2DEC__: 1010 -> 10");
}

static void test_oct2dec() {
    auto out = preprocess("__OCT2DEC__(10)\n");
    CHECK(contains(out, "8"), "__OCT2DEC__: 10 (octal) -> 8");
}

static void test_parse_int_hex() {
    auto out = preprocess("__PARSE_INT__(ff, 16)\n");
    CHECK(contains(out, "255"), "__PARSE_INT__: ff en base 16 -> 255");
}

/* =========================================================================
   Manipulacion de bits
   ========================================================================= */

static void test_lobyte() {
    auto out = preprocess("__LOBYTE__(0x1234)\n");
    CHECK(contains(out, "52"), "__LOBYTE__: 0x1234 -> 0x34 = 52");
}

static void test_hibyte() {
    auto out = preprocess("__HIBYTE__(0x1234)\n");
    CHECK(contains(out, "18"), "__HIBYTE__: 0x1234 -> 0x12 = 18");
}

static void test_loword() {
    auto out = preprocess("__LOWORD__(0x12345678)\n");
    CHECK(contains(out, "22136"), "__LOWORD__: 0x12345678 -> 0x5678 = 22136");
}

static void test_swap16() {
    auto out = preprocess("__SWAP16__(0x1234)\n");
    CHECK(contains(out, "13330"), "__SWAP16__: 0x1234 -> 0x3412 = 13330");
}

static void test_swap32() {
    auto out = preprocess("__SWAP32__(0x12345678)\n");
    CHECK(contains(out, "2018915346"), "__SWAP32__: 0x12345678 -> 0x78563412 = 2018915346");
}

static void test_bitset() {
    auto out = preprocess("__BITSET__(0, 3)\n");
    CHECK(contains(out, "8"), "__BITSET__: poner bit 3 en 0 -> 8");
}

static void test_bitclear() {
    auto out = preprocess("__BITCLEAR__(0xFF, 3)\n");
    CHECK(contains(out, "247"), "__BITCLEAR__: limpiar bit 3 de 0xFF -> 247");
}

static void test_bittest_true() {
    auto out = preprocess("__BITTEST__(8, 3)\n");
    CHECK(contains(out, "1"), "__BITTEST__: bit 3 de 8 -> 1");
}

static void test_bittest_false() {
    auto out = preprocess("__BITTEST__(8, 0)\n");
    CHECK(contains(out, "0"), "__BITTEST__: bit 0 de 8 -> 0");
}

static void test_bitcount() {
    auto out = preprocess("__BITCOUNT__(0xFF)\n");
    CHECK(contains(out, "8"), "__BITCOUNT__: 0xFF tiene 8 bits a 1");
}

static void test_signext() {
    // extension de signo de 4 bits de 0b1000 (8) -> -8 en 64 bits
    auto out = preprocess("__SIGNEXT__(8, 4)\n");
    CHECK(contains(out, "-8"), "__SIGNEXT__: 0b1000 con 4 bits -> -8");
}

/* =========================================================================
   main
   ========================================================================= */

int main() {
    std::cout << "=== Tests de flotantes y conversion numerica del preprocesador vpp ===\n";

    // F2BITS / BITS2F
    test_f2bits_zero();
    test_f2bits_one();
    test_bits2f_one();

    // aritmetica flotante
    test_fadd_1_plus_1();
    test_fsub_4_minus_2();
    test_fmul_2_by_2();
    test_fdiv_4_by_2();

    // funciones matematicas
    test_fabs_negative();
    test_fsqrt_4();
    test_ffloor();
    test_fceil();
    test_ftoi();
    test_itof();

    // comparaciones
    test_feq_equal();
    test_feq_different();
    test_flt();
    test_fgt();
    test_fiszero();
    test_fpi();

    // conversion numerica
    test_dec2hex();
    test_dec2bin();
    test_dec2oct();
    test_hex2dec();
    test_bin2dec();
    test_oct2dec();
    test_parse_int_hex();

    // manipulacion de bits
    test_lobyte();
    test_hibyte();
    test_loword();
    test_swap16();
    test_swap32();
    test_bitset();
    test_bitclear();
    test_bittest_true();
    test_bittest_false();
    test_bitcount();
    test_signext();

    std::cout << "\nResultados: "
              << tests_passed << " pasados, "
              << tests_failed << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
