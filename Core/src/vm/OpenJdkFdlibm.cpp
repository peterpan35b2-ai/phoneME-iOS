/*
 * Copyright (c) 1998, 2001, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation. Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * version 2 for more details.
 *
 * Derived from OpenJDK 8u fdlibm at commit
 * aa3f9dea12a0be998102f42a5ff31ff1dda33253:
 *   e_asin.c, e_acos.c, s_atan.c, e_exp.c
 *
 * The original algorithms and constants are retained. The legacy __HI/__LO
 * aliasing macros are expressed with std::bit_cast so this C++23 port stays
 * endian-independent and free from strict-aliasing undefined behavior.
 */

#include "OpenJdkFdlibm.hpp"

#include <bit>
#include <cmath>
#include <cstdint>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace phoneme::vm::openjdk_fdlibm {
namespace {

[[nodiscard]] std::int32_t high_word(double value) noexcept {
    return static_cast<std::int32_t>(
        std::bit_cast<std::uint64_t>(value) >> 32U);
}

[[nodiscard]] std::uint32_t low_word(double value) noexcept {
    return static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(value));
}

void set_high_word(double& value, std::int32_t word) noexcept {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    value = std::bit_cast<double>(
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(word)) << 32U) |
        (bits & 0xFFFFFFFFULL));
}

void set_low_word(double& value, std::uint32_t word) noexcept {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    value = std::bit_cast<double>(
        (bits & 0xFFFFFFFF00000000ULL) | static_cast<std::uint64_t>(word));
}

} // namespace

// OpenJDK 8u fdlibm e_asin.c.
double asin(double x) noexcept {
    constexpr double one = 1.00000000000000000000e+00;
    constexpr double huge = 1.000e+300;
    constexpr double pio2_hi = 1.57079632679489655800e+00;
    constexpr double pio2_lo = 6.12323399573676603587e-17;
    constexpr double pio4_hi = 7.85398163397448278999e-01;
    constexpr double pS0 = 1.66666666666666657415e-01;
    constexpr double pS1 = -3.25565818622400915405e-01;
    constexpr double pS2 = 2.01212532134862925881e-01;
    constexpr double pS3 = -4.00555345006794114027e-02;
    constexpr double pS4 = 7.91534994289814532176e-04;
    constexpr double pS5 = 3.47933107596021167570e-05;
    constexpr double qS1 = -2.40339491173441421878e+00;
    constexpr double qS2 = 2.02094576023350569471e+00;
    constexpr double qS3 = -6.88283971605453293030e-01;
    constexpr double qS4 = 7.70381505559019352791e-02;

    double t = 0.0;
    double w;
    double p;
    double q;
    double c;
    double r;
    double s;
    const std::int32_t hx = high_word(x);
    const std::int32_t ix = hx & 0x7fffffff;
    if (ix >= 0x3ff00000) {
        if (((ix - 0x3ff00000) | static_cast<std::int32_t>(low_word(x))) == 0) {
            return x * pio2_hi + x * pio2_lo;
        }
        return (x - x) / (x - x);
    }
    if (ix < 0x3fe00000) {
        if (ix < 0x3e400000) {
            if (huge + x > one) return x;
        } else {
            t = x * x;
        }
        p = t * (pS0 + t * (pS1 + t * (pS2 + t *
            (pS3 + t * (pS4 + t * pS5)))));
        q = one + t * (qS1 + t * (qS2 + t * (qS3 + t * qS4)));
        w = p / q;
        return x + x * w;
    }
    w = one - std::fabs(x);
    t = w * 0.5;
    p = t * (pS0 + t * (pS1 + t * (pS2 + t *
        (pS3 + t * (pS4 + t * pS5)))));
    q = one + t * (qS1 + t * (qS2 + t * (qS3 + t * qS4)));
    s = std::sqrt(t);
    if (ix >= 0x3FEF3333) {
        w = p / q;
        t = pio2_hi - (2.0 * (s + s * w) - pio2_lo);
    } else {
        w = s;
        set_low_word(w, 0U);
        c = (t - w * w) / (s + w);
        r = p / q;
        p = 2.0 * s * r - (pio2_lo - 2.0 * c);
        q = pio4_hi - 2.0 * w;
        t = pio4_hi - (p - q);
    }
    return hx > 0 ? t : -t;
}

// OpenJDK 8u fdlibm e_acos.c.
double acos(double x) noexcept {
    constexpr double one = 1.00000000000000000000e+00;
    constexpr double pi = 3.14159265358979311600e+00;
    constexpr double pio2_hi = 1.57079632679489655800e+00;
    constexpr double pio2_lo = 6.12323399573676603587e-17;
    constexpr double pS0 = 1.66666666666666657415e-01;
    constexpr double pS1 = -3.25565818622400915405e-01;
    constexpr double pS2 = 2.01212532134862925881e-01;
    constexpr double pS3 = -4.00555345006794114027e-02;
    constexpr double pS4 = 7.91534994289814532176e-04;
    constexpr double pS5 = 3.47933107596021167570e-05;
    constexpr double qS1 = -2.40339491173441421878e+00;
    constexpr double qS2 = 2.02094576023350569471e+00;
    constexpr double qS3 = -6.88283971605453293030e-01;
    constexpr double qS4 = 7.70381505559019352791e-02;

    double z;
    double p;
    double q;
    double r;
    double w;
    double s;
    double c;
    double df;
    const std::int32_t hx = high_word(x);
    const std::int32_t ix = hx & 0x7fffffff;
    if (ix >= 0x3ff00000) {
        if (((ix - 0x3ff00000) | static_cast<std::int32_t>(low_word(x))) == 0) {
            return hx > 0 ? 0.0 : pi + 2.0 * pio2_lo;
        }
        return (x - x) / (x - x);
    }
    if (ix < 0x3fe00000) {
        if (ix <= 0x3c600000) return pio2_hi + pio2_lo;
        z = x * x;
        p = z * (pS0 + z * (pS1 + z * (pS2 + z *
            (pS3 + z * (pS4 + z * pS5)))));
        q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
        r = p / q;
        return pio2_hi - (x - (pio2_lo - x * r));
    }
    if (hx < 0) {
        z = (one + x) * 0.5;
        p = z * (pS0 + z * (pS1 + z * (pS2 + z *
            (pS3 + z * (pS4 + z * pS5)))));
        q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
        s = std::sqrt(z);
        r = p / q;
        w = r * s - pio2_lo;
        return pi - 2.0 * (s + w);
    }
    z = (one - x) * 0.5;
    s = std::sqrt(z);
    df = s;
    set_low_word(df, 0U);
    c = (z - df * df) / (s + df);
    p = z * (pS0 + z * (pS1 + z * (pS2 + z *
        (pS3 + z * (pS4 + z * pS5)))));
    q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
    r = p / q;
    w = r * s + c;
    return 2.0 * (df + w);
}

// OpenJDK 8u fdlibm s_atan.c.
double atan(double x) noexcept {
    constexpr double atanhi[] = {
        4.63647609000806093515e-01,
        7.85398163397448278999e-01,
        9.82793723247329054082e-01,
        1.57079632679489655800e+00,
    };
    constexpr double atanlo[] = {
        2.26987774529616870924e-17,
        3.06161699786838301793e-17,
        1.39033110312309984516e-17,
        6.12323399573676603587e-17,
    };
    constexpr double aT[] = {
        3.33333333333329318027e-01,
       -1.99999999998764832476e-01,
        1.42857142725034663711e-01,
       -1.11111104054623557880e-01,
        9.09088713343650656196e-02,
       -7.69187620504482999495e-02,
        6.66107313738753120669e-02,
       -5.83357013379057348645e-02,
        4.97687799461593236017e-02,
       -3.65315727442169155270e-02,
        1.62858201153657823623e-02,
    };
    constexpr double one = 1.0;
    constexpr double huge = 1.0e300;

    double w;
    double s1;
    double s2;
    double z;
    const std::int32_t hx = high_word(x);
    const std::int32_t ix = hx & 0x7fffffff;
    int id;
    if (ix >= 0x44100000) {
        if (ix > 0x7ff00000 ||
            (ix == 0x7ff00000 && low_word(x) != 0U)) {
            return x + x;
        }
        return hx > 0 ? atanhi[3] + atanlo[3]
                      : -atanhi[3] - atanlo[3];
    }
    if (ix < 0x3fdc0000) {
        if (ix < 0x3e200000 && huge + x > one) return x;
        id = -1;
    } else {
        x = std::fabs(x);
        if (ix < 0x3ff30000) {
            if (ix < 0x3fe60000) {
                id = 0;
                x = (2.0 * x - one) / (2.0 + x);
            } else {
                id = 1;
                x = (x - one) / (x + one);
            }
        } else if (ix < 0x40038000) {
            id = 2;
            x = (x - 1.5) / (one + 1.5 * x);
        } else {
            id = 3;
            x = -1.0 / x;
        }
    }
    z = x * x;
    w = z * z;
    s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w *
        (aT[6] + w * (aT[8] + w * aT[10])))));
    s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w *
        (aT[7] + w * aT[9]))));
    if (id < 0) return x - x * (s1 + s2);
    z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
    return hx < 0 ? -z : z;
}

// OpenJDK 8u fdlibm e_exp.c.
double exp(double x) noexcept {
    constexpr double one = 1.0;
    constexpr double halF[] = {0.5, -0.5};
    constexpr double huge = 1.0e+300;
    constexpr double twom1000 = 9.33263618503218878990e-302;
    constexpr double o_threshold = 7.09782712893383973096e+02;
    constexpr double u_threshold = -7.45133219101941108420e+02;
    constexpr double ln2HI[] = {
        6.93147180369123816490e-01,
       -6.93147180369123816490e-01,
    };
    constexpr double ln2LO[] = {
        1.90821492927058770002e-10,
       -1.90821492927058770002e-10,
    };
    constexpr double invln2 = 1.44269504088896338700e+00;
    constexpr double P1 = 1.66666666666666019037e-01;
    constexpr double P2 = -2.77777777770155933842e-03;
    constexpr double P3 = 6.61375632143793436117e-05;
    constexpr double P4 = -1.65339022054652515390e-06;
    constexpr double P5 = 4.13813679705723846039e-08;

    double y;
    double hi = 0.0;
    double lo = 0.0;
    double c;
    double t;
    int k = 0;
    std::uint32_t hx = static_cast<std::uint32_t>(high_word(x));
    const int xsb = static_cast<int>((hx >> 31U) & 1U);
    hx &= 0x7fffffffU;
    if (hx >= 0x40862E42U) {
        if (hx >= 0x7ff00000U) {
            if (((hx & 0xfffffU) | low_word(x)) != 0U) return x + x;
            return xsb == 0 ? x : 0.0;
        }
        if (x > o_threshold) return huge * huge;
        if (x < u_threshold) return twom1000 * twom1000;
    }
    if (hx > 0x3fd62e42U) {
        if (hx < 0x3FF0A2B2U) {
            hi = x - ln2HI[xsb];
            lo = ln2LO[xsb];
            k = 1 - xsb - xsb;
        } else {
            k = static_cast<int>(invln2 * x + halF[xsb]);
            t = static_cast<double>(k);
            hi = x - t * ln2HI[0];
            lo = t * ln2LO[0];
        }
        x = hi - lo;
    } else if (hx < 0x3e300000U) {
        if (huge + x > one) return one + x;
    }
    t = x * x;
    c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    if (k == 0) return one - ((x * c) / (c - 2.0) - x);
    y = one - ((lo - (x * c) / (2.0 - c)) - hi);
    if (k >= -1021) {
        set_high_word(y, high_word(y) + k * 0x00100000);
        return y;
    }
    set_high_word(y, high_word(y) + (k + 1000) * 0x00100000);
    return y * twom1000;
}

} // namespace phoneme::vm::openjdk_fdlibm
