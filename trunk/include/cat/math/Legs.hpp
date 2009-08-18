/*
    Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
    License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

// 07/12/09 working and fast!
// 06/27/09 began

#ifndef CAT_LEGS_HPP
#define CAT_LEGS_HPP

#include <cat/Platform.hpp>

namespace cat {


#if defined(CAT_WORD_64)

#define CAT_LEG_BITS 64
#define CAT_USE_LEGS_ASM64 /* use 64-bit assembly code inner loops */
#define CAT_USED_BITS(x) BitMath::BSR64(x) /* does not work if x = 0 */
    typedef u64 Leg;
    typedef s64 LegSigned;
#if !defined(CAT_COMPILER_MSVC)
    typedef u128 LegPair;
    typedef s128 LegPairSigned;
# define CAT_LEG_PAIRMUL(A, B) ((LegPair)A * B)
#else
# define CAT_NO_LEGPAIR
#endif

#elif defined(CAT_WORD_32)

#define CAT_LEG_BITS 32
#define CAT_USED_BITS(x) BitMath::BSR32(x) /* does not work if x = 0 */
    typedef u32 Leg;
    typedef s32 LegSigned;
    typedef u64 LegPair;
    typedef s64 LegPairSigned;

#if defined(CAT_COMPILER_MSVC)
# define CAT_LEG_PAIRMUL(A, B) __emulu(A, B) /* slightly faster in ICC */
#else
# define CAT_LEG_PAIRMUL(A, B) ((LegPair)A * B)
#endif

#endif // CAT_WORD_32

// Largest value that can be taken on by a Leg
const Leg CAT_LEG_LARGEST = ~(Leg)0;


#if defined(CAT_NO_LEGPAIR)


// p(hi:lo) = A * B
#define CAT_LEG_MUL(A, B, p_hi, p_lo)    \
{                                        \
    p_lo = _umul128(A, B, &p_hi);        \
}

// p(hi:lo) = A * B + C
#define CAT_LEG_MULADD(A, B, C, p_hi, p_lo) \
{                                           \
    u64 _C0 = C;                            \
    p_lo = _umul128(A, B, &p_hi);           \
    p_hi += ((p_lo += _C0) < _C0);          \
}

// p(hi:lo) = A * B + C + D
#define CAT_LEG_MULADD2(A, B, C, D, p_hi, p_lo) \
{                                               \
    u64 _C0 = C, _D0 = D;                       \
    p_lo = _umul128(A, B, &p_hi);               \
    p_hi += ((p_lo += _C0) < _C0);              \
    p_hi += ((p_lo += _D0) < _D0);              \
}

// p(C2:C1:C0) = A * B + (C1:C0)
#define CAT_LEG_COMBA2(A, B, C0, C1, C2)     \
{                                            \
    u64 _p_hi, _p_lo;                        \
    _p_lo = _umul128(A, B, &_p_hi);          \
    _p_hi += ((C0 += _p_lo) < _p_lo);        \
    C2 = ((C1 += _p_hi) < _p_hi);            \
}

// p(C2:C1:C0) = A * B + (C2:C1:C0)
#define CAT_LEG_COMBA3(A, B, C0, C1, C2)     \
{                                            \
    u64 _p_hi, _p_lo;                        \
    _p_lo = _umul128(A, B, &_p_hi);          \
    _p_hi += ((C0 += _p_lo) < _p_lo);        \
    C2 += ((C1 += _p_hi) < _p_hi);           \
}


#else // !defined(CAT_NO_LEGPAIR)


// p(hi:lo) = A * B
#define CAT_LEG_MUL(A, B, p_hi, p_lo)      \
{                                          \
    LegPair _mt = CAT_LEG_PAIRMUL(A, B);   \
    (p_lo) = (Leg)_mt;                     \
    (p_hi) = (Leg)(_mt >> CAT_LEG_BITS);   \
}

// p(hi:lo) = A * B + C
#define CAT_LEG_MULADD(A, B, C, p_hi, p_lo)           \
{                                                     \
    LegPair _mt = CAT_LEG_PAIRMUL(A, B) + (Leg)(C);   \
    (p_lo) = (Leg)_mt;                                \
    (p_hi) = (Leg)(_mt >> CAT_LEG_BITS);              \
}

// p(hi:lo) = A * B + C + D
#define CAT_LEG_MULADD2(A, B, C, D, p_hi, p_lo)                  \
{                                                                \
    LegPair _mt = CAT_LEG_PAIRMUL(A, B) + (Leg)(C) + (Leg)(D);   \
    (p_lo) = (Leg)_mt;                                           \
    (p_hi) = (Leg)(_mt >> CAT_LEG_BITS);                         \
}

// p(C2:C1:C0) = A * B + (C1:C0)
#define CAT_LEG_COMBA2(A, B, C0, C1, C2)        \
{                                               \
	LegPair _cp = CAT_LEG_PAIRMUL(A, B) + (C0); \
	(C0) = (Leg)_cp;                            \
	_cp = (_cp >> CAT_LEG_BITS) + (C1);         \
	(C1) = (Leg)_cp;                            \
	(C2) = (Leg)(_cp >> CAT_LEG_BITS);          \
}

// p(C2:C1:C0) = A * B + (C2:C1:C0)
#define CAT_LEG_COMBA3(A, B, C0, C1, C2)        \
{                                               \
	LegPair _cp = CAT_LEG_PAIRMUL(A, B) + (C0); \
	(C0) = (Leg)_cp;                            \
	_cp = (_cp >> CAT_LEG_BITS) + (C1);         \
	(C1) = (Leg)_cp;                            \
	(C2) += (Leg)(_cp >> CAT_LEG_BITS);         \
}

// Q(hi:lo) = A(hi:lo) / B
#define CAT_LEG_DIV(A_hi, A_lo, B, Q_hi, Q_lo)                    \
{                                                                 \
    LegPair _A = ((LegPair)(A_hi) << CAT_LEG_BITS) | (Leg)(A_lo); \
    LegPair _qt = (LegPair)(_A / (B));                            \
    (Q_hi) = (Leg)(_qt >> CAT_LEG_BITS);                          \
    (Q_lo) = (Leg)_qt;                                            \
}


#endif // CAT_NO_LEGPAIR


} // namespace cat

#endif // CAT_LEGS_HPP
