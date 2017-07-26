/* Copyright (c) 2007 Mega Man */

#include <asm/asm.h>
#include <asm/regdef.h>

/** Data Cache: Index Writeback Invalidate. */
#define DXWBIN 0x14

/** Instruction Cache: Index Invalidate. */
#define IXIN 0x07

/** Size of a cache line. */
#define CACHE_LINE_SIZE 64

/** Number of cache ways. */
#define NUMBER_OF_CACHE_WAYS 2

/** Number of data cache lines per cache way. */
#define NUMBER_OF_DCACHE_LINES 64

/** Number of instruction cache lines per cache way. */
#define NUMBER_OF_ICACHE_LINES 128

.globl flushDCacheAll
.type flushDCacheAll,@function;
.ent flushDCacheAll
.set noreorder
/* Cache instructions as last instuction of a page could lead to errors. */
.align 6
flushDCacheAll:
	li	t0, 0x00000000
	li	t1, NUMBER_OF_DCACHE_LINES
flushDCacheAll_loop:
	sync.l
	/* Flush cache line in Way 0. */
	cache DXWBIN, 0(t0)
	sync.l
	/* Flush cache line in Way 1. */
	cache DXWBIN, 1(t0)
	sync.l
	addi	t1, t1, -1
	addi	t0, t0, CACHE_LINE_SIZE
	bgtz	t1, flushDCacheAll_loop
	nop
	jr	ra
	nop
.end flushDCacheAll

.globl invalidateICacheAll
.type invalidateICacheAll,@function;
.ent invalidateICacheAll
.set noreorder
/* Align at cache line size to be sure, that only one cache line is loaded by code itself. */
.align 6
invalidateICacheAll:
	li	t0, 0x00000000
	li	t1, NUMBER_OF_ICACHE_LINES
invalidateICacheAll_loop:
	sync.p
	/* Flush cache line in Way 0. */
	cache IXIN, 0(t0)
	sync.p
	/* Flush cache line in Way 1. */
	cache IXIN, 1(t0)
	sync.p
	addi	t1, t1, -1
	addi	t0, t0, CACHE_LINE_SIZE
	bgtz	t1, invalidateICacheAll_loop
	nop
	jr	ra
	nop
.end invalidateICacheAll
