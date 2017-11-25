/////////////////////////////////////////////////////////////////////////
// $Id: gf2.cc 13322 2017-10-21 19:57:12Z sshwarts $
/////////////////////////////////////////////////////////////////////////
//
//   Copyright (c) 2017 Stanislav Shwartsman
//          Written by Stanislav Shwartsman [sshwarts at sourceforge net]
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA B 02110-1301 USA
//
/////////////////////////////////////////////////////////////////////////

#define NEED_CPU_REG_SHORTCUTS 1
#include "bochs.h"
#include "cpu.h"
#define LOG_THIS BX_CPU_THIS_PTR

#if BX_CPU_LEVEL >= 6

static const Bit8u GF256_Inv[256] = {
  0x00, 0x01, 0x8d, 0xf6, 0xcb, 0x52, 0x7b, 0xd1,
  0xe8, 0x4f, 0x29, 0xc0, 0xb0, 0xe1, 0xe5, 0xc7,
  0x74, 0xb4, 0xaa, 0x4b, 0x99, 0x2b, 0x60, 0x5f,
  0x58, 0x3f, 0xfd, 0xcc, 0xff, 0x40, 0xee, 0xb2,
  0x3a, 0x6e, 0x5a, 0xf1, 0x55, 0x4d, 0xa8, 0xc9,
  0xc1, 0x0a, 0x98, 0x15, 0x30, 0x44, 0xa2, 0xc2,
  0x2c, 0x45, 0x92, 0x6c, 0xf3, 0x39, 0x66, 0x42,
  0xf2, 0x35, 0x20, 0x6f, 0x77, 0xbb, 0x59, 0x19,
  0x1d, 0xfe, 0x37, 0x67, 0x2d, 0x31, 0xf5, 0x69,
  0xa7, 0x64, 0xab, 0x13, 0x54, 0x25, 0xe9, 0x09,
  0xed, 0x5c, 0x05, 0xca, 0x4c, 0x24, 0x87, 0xbf,
  0x18, 0x3e, 0x22, 0xf0, 0x51, 0xec, 0x61, 0x17,
  0x16, 0x5e, 0xaf, 0xd3, 0x49, 0xa6, 0x36, 0x43,
  0xf4, 0x47, 0x91, 0xdf, 0x33, 0x93, 0x21, 0x3b,
  0x79, 0xb7, 0x97, 0x85, 0x10, 0xb5, 0xba, 0x3c,
  0xb6, 0x70, 0xd0, 0x06, 0xa1, 0xfa, 0x81, 0x82,
  0x83, 0x7e, 0x7f, 0x80, 0x96, 0x73, 0xbe, 0x56,
  0x9b, 0x9e, 0x95, 0xd9, 0xf7, 0x02, 0xb9, 0xa4,
  0xde, 0x6a, 0x32, 0x6d, 0xd8, 0x8a, 0x84, 0x72,
  0x2a, 0x14, 0x9f, 0x88, 0xf9, 0xdc, 0x89, 0x9a,
  0xfb, 0x7c, 0x2e, 0xc3, 0x8f, 0xb8, 0x65, 0x48,
  0x26, 0xc8, 0x12, 0x4a, 0xce, 0xe7, 0xd2, 0x62,
  0x0c, 0xe0, 0x1f, 0xef, 0x11, 0x75, 0x78, 0x71,
  0xa5, 0x8e, 0x76, 0x3d, 0xbd, 0xbc, 0x86, 0x57,
  0x0b, 0x28, 0x2f, 0xa3, 0xda, 0xd4, 0xe4, 0x0f,
  0xa9, 0x27, 0x53, 0x04, 0x1b, 0xfc, 0xac, 0xe6,
  0x7a, 0x07, 0xae, 0x63, 0xc5, 0xdb, 0xe2, 0xea,
  0x94, 0x8b, 0xc4, 0xd5, 0x9d, 0xf8, 0x90, 0x6b,
  0xb1, 0x0d, 0xd6, 0xeb, 0xc6, 0x0e, 0xcf, 0xad,
  0x08, 0x4e, 0xd7, 0xe3, 0x5d, 0x50, 0x1e, 0xb3,
  0x5b, 0x23, 0x38, 0x34, 0x68, 0x46, 0x03, 0x8c,
  0xdd, 0x9c, 0x7d, 0xa0, 0xcd, 0x1a, 0x41, 0x1c
};

#include "scalar_arith.h"

BX_CPP_INLINE Bit8u affine_byte(Bit64u src2qw, Bit8u src1byte, Bit8u imm8)
{
  Bit8u result = 0;
  for (int i=7; i >= 0; i--) {
    result |= parity_byte((src2qw & 0xff) & src1byte) << i;
    src2qw >>= 8;
  }
  return result ^ imm8;
}

BX_CPP_INLINE void xmm_gf2p8affineqb(BxPackedXmmRegister *dst, const BxPackedXmmRegister *src, Bit8u imm8)
{
  for (unsigned i=0; i < 16; i++) {
    dst->xmmubyte(i) = affine_byte(src->xmm64u(i/8), dst->xmmubyte(i), imm8);
  }
}

BX_CPP_INLINE Bit8u affine_inverse_byte(Bit64u src2qw, Bit8u src1byte, Bit8u imm8)
{
  return affine_byte(src2qw, GF256_Inv[src1byte], imm8);
}

BX_CPP_INLINE void xmm_gf2p8affineinvqb(BxPackedXmmRegister *dst, const BxPackedXmmRegister *src, Bit8u imm8)
{
  for (unsigned i=0; i < 16; i++) {
    dst->xmmubyte(i) = affine_inverse_byte(src->xmm64u(i/8), dst->xmmubyte(i), imm8);
  }
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::GF2P8AFFINEINVQB_VdqWdqIbR(bxInstruction_c *i)
{
  BxPackedXmmRegister dst = BX_READ_XMM_REG(i->dst()), src = BX_READ_XMM_REG(i->src());

  xmm_gf2p8affineinvqb(&dst, &src, i->Ib());

  BX_WRITE_XMM_REG(i->dst(), dst);

  BX_NEXT_INSTR(i);
}

#if BX_SUPPORT_AVX
BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::VGF2P8AFFINEINVQB_VdqHdqWdqIbR(bxInstruction_c *i)
{
  BxPackedAvxRegister dst = BX_READ_AVX_REG(i->src1()), src2 = BX_READ_AVX_REG(i->src2());
  unsigned len = i->getVL();

  for (unsigned n=0; n < len; n++) {
    xmm_gf2p8affineinvqb(&dst.vmm128(n), &src2.vmm128(n), i->Ib());
  }

#if BX_SUPPORT_EVEX
  if (i->opmask())
    avx512_write_regq_masked(i, &dst, len, BX_READ_8BIT_OPMASK(i->opmask()));
  else
#endif
    BX_WRITE_AVX_REGZ(i->dst(), dst, len);

  BX_NEXT_INSTR(i);
}
#endif

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::GF2P8AFFINEQB_VdqWdqIbR(bxInstruction_c *i)
{
  BxPackedXmmRegister dst = BX_READ_XMM_REG(i->dst()), src = BX_READ_XMM_REG(i->src());

  xmm_gf2p8affineqb(&dst, &src, i->Ib());

  BX_WRITE_XMM_REG(i->dst(), dst);

  BX_NEXT_INSTR(i);
}

#if BX_SUPPORT_AVX
BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::VGF2P8AFFINEQB_VdqHdqWdqIbR(bxInstruction_c *i)
{
  BxPackedAvxRegister dst = BX_READ_AVX_REG(i->src1()), src2 = BX_READ_AVX_REG(i->src2());
  unsigned len = i->getVL();

  for (unsigned n=0; n < len; n++) {
    xmm_gf2p8affineqb(&dst.vmm128(n), &src2.vmm128(n), i->Ib());
  }

#if BX_SUPPORT_EVEX
  if (i->opmask())
    avx512_write_regq_masked(i, &dst, len, BX_READ_8BIT_OPMASK(i->opmask()));
  else
#endif
    BX_WRITE_AVX_REGZ(i->dst(), dst, len);

  BX_NEXT_INSTR(i);
}
#endif

Bit8u GF256_Exp[256] = {
  0x01, 0x03, 0x05, 0x0f, 0x11, 0x33, 0x55, 0xff,
  0x1a, 0x2e, 0x72, 0x96, 0xa1, 0xf8, 0x13, 0x35,
  0x5f, 0xe1, 0x38, 0x48, 0xd8, 0x73, 0x95, 0xa4,
  0xf7, 0x02, 0x06, 0x0a, 0x1e, 0x22, 0x66, 0xaa,
  0xe5, 0x34, 0x5c, 0xe4, 0x37, 0x59, 0xeb, 0x26,
  0x6a, 0xbe, 0xd9, 0x70, 0x90, 0xab, 0xe6, 0x31,
  0x53, 0xf5, 0x04, 0x0c, 0x14, 0x3c, 0x44, 0xcc,
  0x4f, 0xd1, 0x68, 0xb8, 0xd3, 0x6e, 0xb2, 0xcd,
  0x4c, 0xd4, 0x67, 0xa9, 0xe0, 0x3b, 0x4d, 0xd7,
  0x62, 0xa6, 0xf1, 0x08, 0x18, 0x28, 0x78, 0x88,
  0x83, 0x9e, 0xb9, 0xd0, 0x6b, 0xbd, 0xdc, 0x7f,
  0x81, 0x98, 0xb3, 0xce, 0x49, 0xdb, 0x76, 0x9a, 
  0xb5, 0xc4, 0x57, 0xf9, 0x10, 0x30, 0x50, 0xf0,
  0x0b, 0x1d, 0x27, 0x69, 0xbb, 0xd6, 0x61, 0xa3,
  0xfe, 0x19, 0x2b, 0x7d, 0x87, 0x92, 0xad, 0xec,
  0x2f, 0x71, 0x93, 0xae, 0xe9, 0x20, 0x60, 0xa0,
  0xfb, 0x16, 0x3a, 0x4e, 0xd2, 0x6d, 0xb7, 0xc2,
  0x5d, 0xe7, 0x32, 0x56, 0xfa, 0x15, 0x3f, 0x41,
  0xc3, 0x5e, 0xe2, 0x3d, 0x47, 0xc9, 0x40, 0xc0,
  0x5b, 0xed, 0x2c, 0x74, 0x9c, 0xbf, 0xda, 0x75,
  0x9f, 0xba, 0xd5, 0x64, 0xac, 0xef, 0x2a, 0x7e,
  0x82, 0x9d, 0xbc, 0xdf, 0x7a, 0x8e, 0x89, 0x80,
  0x9b, 0xb6, 0xc1, 0x58, 0xe8, 0x23, 0x65, 0xaf,
  0xea, 0x25, 0x6f, 0xb1, 0xc8, 0x43, 0xc5, 0x54,
  0xfc, 0x1f, 0x21, 0x63, 0xa5, 0xf4, 0x07, 0x09,
  0x1b, 0x2d, 0x77, 0x99, 0xb0, 0xcb, 0x46, 0xca,
  0x45, 0xcf, 0x4a, 0xde, 0x79, 0x8b, 0x86, 0x91,
  0xa8, 0xe3, 0x3e, 0x42, 0xc6, 0x51, 0xf3, 0x0e,
  0x12, 0x36, 0x5a, 0xee, 0x29, 0x7b, 0x8d, 0x8c,
  0x8f, 0x8a, 0x85, 0x94, 0xa7, 0xf2, 0x0d, 0x17,
  0x39, 0x4b, 0xdd, 0x7c, 0x84, 0x97, 0xa2, 0xfd,
  0x1c, 0x24, 0x6c, 0xb4, 0xc7, 0x52, 0xf6, 0x01
};

Bit8u GF256_Log[256] = {
  0x00, 0x00, 0x19, 0x01, 0x32, 0x02, 0x1a, 0xc6,
  0x4b, 0xc7, 0x1b, 0x68, 0x33, 0xee, 0xdf, 0x03,
  0x64, 0x04, 0xe0, 0x0e, 0x34, 0x8d, 0x81, 0xef,
  0x4c, 0x71, 0x08, 0xc8, 0xf8, 0x69, 0x1c, 0xc1,
  0x7d, 0xc2, 0x1d, 0xb5, 0xf9, 0xb9, 0x27, 0x6a,
  0x4d, 0xe4, 0xa6, 0x72, 0x9a, 0xc9, 0x09, 0x78,
  0x65, 0x2f, 0x8a, 0x05, 0x21, 0x0f, 0xe1, 0x24,
  0x12, 0xf0, 0x82, 0x45, 0x35, 0x93, 0xda, 0x8e,
  0x96, 0x8f, 0xdb, 0xbd, 0x36, 0xd0, 0xce, 0x94,
  0x13, 0x5c, 0xd2, 0xf1, 0x40, 0x46, 0x83, 0x38,
  0x66, 0xdd, 0xfd, 0x30, 0xbf, 0x06, 0x8b, 0x62,
  0xb3, 0x25, 0xe2, 0x98, 0x22, 0x88, 0x91, 0x10,
  0x7e, 0x6e, 0x48, 0xc3, 0xa3, 0xb6, 0x1e, 0x42,
  0x3a, 0x6b, 0x28, 0x54, 0xfa, 0x85, 0x3d, 0xba,
  0x2b, 0x79, 0x0a, 0x15, 0x9b, 0x9f, 0x5e, 0xca,
  0x4e, 0xd4, 0xac, 0xe5, 0xf3, 0x73, 0xa7, 0x57,
  0xaf, 0x58, 0xa8, 0x50, 0xf4, 0xea, 0xd6, 0x74,
  0x4f, 0xae, 0xe9, 0xd5, 0xe7, 0xe6, 0xad, 0xe8,
  0x2c, 0xd7, 0x75, 0x7a, 0xeb, 0x16, 0x0b, 0xf5,
  0x59, 0xcb, 0x5f, 0xb0, 0x9c, 0xa9, 0x51, 0xa0,
  0x7f, 0x0c, 0xf6, 0x6f, 0x17, 0xc4, 0x49, 0xec,
  0xd8, 0x43, 0x1f, 0x2d, 0xa4, 0x76, 0x7b, 0xb7,
  0xcc, 0xbb, 0x3e, 0x5a, 0xfb, 0x60, 0xb1, 0x86,
  0x3b, 0x52, 0xa1, 0x6c, 0xaa, 0x55, 0x29, 0x9d,
  0x97, 0xb2, 0x87, 0x90, 0x61, 0xbe, 0xdc, 0xfc,
  0xbc, 0x95, 0xcf, 0xcd, 0x37, 0x3f, 0x5b, 0xd1,
  0x53, 0x39, 0x84, 0x3c, 0x41, 0xa2, 0x6d, 0x47,
  0x14, 0x2a, 0x9e, 0x5d, 0x56, 0xf2, 0xd3, 0xab,
  0x44, 0x11, 0x92, 0xd9, 0x23, 0x20, 0x2e, 0x89,
  0xb4, 0x7c, 0xb8, 0x26, 0x77, 0x99, 0xe3, 0xa5,
  0x67, 0x4a, 0xed, 0xde, 0xc5, 0x31, 0xfe, 0x18,
  0x0d, 0x63, 0x8c, 0x80, 0xc0, 0xf7, 0x70, 0x07
};

/*
// Reference implementation matching exactly Intel SDM
BX_CPP_INLINE Bit8u gf2p8mul(Bit8u a, Bit8u b)
{
    Bit16u temp = 0;

    // Polynomial multiplication
    for (unsigned bitcnt = 0; bitcnt < 8; bitcnt++) {
        if ((a >> bitcnt) & 0x1) {
            temp ^= (b << bitcnt);
        }
    }

    // Carry out polynomial reduction by the characteristic polynomial p
    for (unsigned bitcnt = 14; bitcnt > 7; bitcnt--) {
        if ((temp >> bitcnt) & 0x1) {
            temp ^= (0x011b << (bitcnt - 8));
        }
    }

    // Result should now fit within a byte after the modulo reduction
    return Bit8u(temp);
}
*/

// faster implementation found at 
// https://www.gamedev.net/forums/topic/546942-c-can-this-be-optimized/
BX_CPP_INLINE Bit8u gf2p8mul(Bit8u a, Bit8u b)
{
  if (a == 0 || b == 0) return 0;

  Bit16u tmp = GF256_Log[a] + GF256_Log[b];
  if (tmp > 255)
    tmp = tmp - 255;
  
  return GF256_Exp[tmp];
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::GF2P8MULB_VdqWdqR(bxInstruction_c *i)
{
  BxPackedXmmRegister dst = BX_READ_XMM_REG(i->dst()), src = BX_READ_XMM_REG(i->src());

  for (unsigned n=0; n < 16; n++)
    dst.xmmubyte(n) = gf2p8mul(dst.xmmubyte(n), src.xmmubyte(n));

  BX_WRITE_XMM_REG(i->dst(), dst);

  BX_NEXT_INSTR(i);
}

#if BX_SUPPORT_AVX
BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::VGF2P8MULB_VdqHdqWdqR(bxInstruction_c *i)
{
  BxPackedAvxRegister dst = BX_READ_AVX_REG(i->src1()), src = BX_READ_AVX_REG(i->src2());
  unsigned len = i->getVL();

  for (unsigned n=0; n < BYTE_ELEMENTS(len); n++) {
    dst.vmmubyte(n) = gf2p8mul(dst.vmmubyte(n), src.vmmubyte(n));
  }

#if BX_SUPPORT_EVEX
  if (i->opmask())
    avx512_write_regb_masked(i, &dst, len, BX_READ_OPMASK(i->opmask()));
  else
#endif
    BX_WRITE_AVX_REGZ(i->dst(), dst, len);

  BX_NEXT_INSTR(i);
}
#endif

#endif
