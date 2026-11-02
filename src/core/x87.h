// The x87 helpers our replacements of the game's float code share: the
// transcendental instructions (FSIN, FCOS, FPTAN, FPATAN) and the C
// runtime's __CIacos, each the game's own instruction or call, so the
// results match to the bit.
#pragma once

#include <opensc4/addresses.h>

#include <cstdint>

namespace opensc4::x87 {

// FPTAN of the value (the game's instruction; the 1.0 it pushes dropped).
inline double Tan(double value)
{
    double result;
    __asm {
        fld value
        fptan
        fstp st(0)
        fstp result
    }
    return result;
}

// The game's FLD1; FPATAN: the arctangent of the value.
inline double Atan(double value)
{
    double result;
    __asm {
        fld value
        fld1
        fpatan
        fstp result
    }
    return result;
}

// The game's FSIN.
inline double Sin(double value)
{
    double result;
    __asm {
        fld value
        fsin
        fstp result
    }
    return result;
}

// The game's FCOS.
inline double Cos(double value)
{
    double result;
    __asm {
        fld value
        fcos
        fstp result
    }
    return result;
}

// The C runtime's __CIacos (0x009F0090, its argument and answer in ST0),
// the answer stored as a float.
inline float CrtAcos(double value)
{
    float result;
    const std::uintptr_t function = addr::CRT_CIacos;
    __asm {
        fld value
        call function
        fstp result
    }
    return result;
}

} // namespace opensc4::x87
