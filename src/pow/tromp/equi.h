// Equihash solver
// Copyright (c) 2016-2016 John Tromp, The Zcash developers

#ifndef EQUI_H
#define EQUI_H

#include "sodium.h"
#include "compat/endian.h"

#include <stdint.h> // for types uint32_t,uint64_t

typedef uint32_t u32;
typedef unsigned char uchar;

// algorithm parameters, prefixed with W to reduce include file conflicts

#ifndef WN
#define WN	200
#endif

#ifndef WK
#define WK	9
#endif

#define NDIGITS		(WK+1)
#define DIGITBITS	(WN/(NDIGITS))

static const u32 PROOFSIZE = 1<<WK;
static const u32 BASE = 1<<DIGITBITS;
static const u32 NHASHES = 2*BASE;
static const u32 HASHESPERBLAKE = 512/WN;
static const u32 HASHOUT = HASHESPERBLAKE*WN/8;

typedef u32 proof[PROOFSIZE];

int compu32(const void *pa, const void *pb);

#endif // EQUI_H
