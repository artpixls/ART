// -*- C++ -*-

#pragma once

#include <inttypes.h>

namespace rtengine {

inline uint16_t DNG_FloatToHalf(float f)
{
    union {
        float f;
        uint32_t i;
    } tmp;

    tmp.f = f;
    int32_t sign     =  (tmp.i >> 16) & 0x00008000;
    int32_t exponent = ((tmp.i >> 23) & 0x000000ff) - (127 - 15);
    int32_t mantissa =   tmp.i        & 0x007fffff;
    if (exponent <= 0) {
        if (exponent < -10) {
            return (uint16_t)sign;
        }
        mantissa = (mantissa | 0x00800000) >> (1 - exponent);
        if (mantissa &  0x00001000)
            mantissa += 0x00002000;
        return (uint16_t)(sign | (mantissa >> 13));
    } else if (exponent == 0xff - (127 - 15)) {
        if (mantissa == 0) {
            return (uint16_t)(sign | 0x7c00);
        } else {
            return (uint16_t)(sign | 0x7c00 | (mantissa >> 13));
        }
    }
    if (mantissa & 0x00001000) {
        mantissa += 0x00002000;
        if (mantissa & 0x00800000) {
            mantissa =  0;          // overflow in significand,
            exponent += 1;          // adjust exponent
        }
    }
    if (exponent > 30) {
        return (uint16_t)(sign | 0x7c00); // infinity with the same sign as f.
    }
    return (uint16_t)(sign | (exponent << 10) | (mantissa >> 13));
}

// From DNG SDK dng_utils.h
inline uint32_t DNG_HalfToFloat_i(uint16_t halfValue)
{
    int32_t sign     = (halfValue >> 15) & 0x00000001;
    int32_t exponent = (halfValue >> 10) & 0x0000001f;
    int32_t mantissa =  halfValue        & 0x000003ff;
    if (exponent == 0) {
        if (mantissa == 0) {
            // Plus or minus zero
            return (uint32_t) (sign << 31);
        } else {
            // Denormalized number -- renormalize it
            while (!(mantissa & 0x00000400)) {
                mantissa <<= 1;
                exponent -=  1;
            }
            exponent += 1;
            mantissa &= ~0x00000400;
        }
    } else if (exponent == 31) {
        if (mantissa == 0) {
            // Positive or negative infinity, convert to maximum (16 bit) values.
            return (uint32_t)((sign << 31) | ((0x1eL + 127 - 15) << 23) |  (0x3ffL << 13));
        } else {
            // Nan -- Just set to zero.
            return 0;
        }
    }
    // Normalized number
    exponent += (127 - 15);
    mantissa <<= 13;
    // Assemble sign, exponent and mantissa.
    return (uint32_t) ((sign << 31) | (exponent << 23) | mantissa);
}


inline float DNG_HalfToFloat(uint16_t halfValue)
{
    union {
        float f;
        uint32_t i;
    } tmp;

    tmp.i = DNG_HalfToFloat_i(halfValue);
    return tmp.f;
}


inline uint32_t DNG_FP24ToFloat(const uint8_t * input)
{
    int32_t sign     = (input [0] >> 7) & 0x01;
    int32_t exponent = (input [0]     ) & 0x7F;
    int32_t mantissa = (((int32_t) input [1]) << 8) | input[2];
    if (exponent == 0) {
        if (mantissa == 0) {
            // Plus or minus zero
            return (uint32_t) (sign << 31);
        } else {
            // Denormalized number -- renormalize it
            while (!(mantissa & 0x00010000)) {
                mantissa <<= 1;
                exponent -=  1;
            }
            exponent += 1;
            mantissa &= ~0x00010000;
        }
    } else if (exponent == 127) {
        if (mantissa == 0) {
            // Positive or negative infinity, convert to maximum (24 bit) values.
            return (uint32_t) ((sign << 31) | ((0x7eL + 128 - 64) << 23) |  (0xffffL << 7));
        } else {
            // Nan -- Just set to zero.
            return 0;
        }
    }
    // Normalized number
    exponent += (128 - 64);
    mantissa <<= 7;
    // Assemble sign, exponent and mantissa.
    return (uint32_t) ((sign << 31) | (exponent << 23) | mantissa);
}

} // namespace rtengine
