/*	$OpenBSD: i386-bitops.h,v 1.1 1996/06/24 03:34:59 downsj Exp $	*/

/*
 * this is mixture of i386/bitops.h and asm/string.h
 * taken from the Linux source tree 
 *
 * XXX replace with Mach routines or reprogram in C
 */
#ifndef _SYS_GNU_EXT2FS_I386_BITOPS_H_
#define	_SYS_GNU_EXT2FS_I386_BITOPS_H_

/*
 * Copyright 1992, Linus Torvalds.
 */

/*
 * These have to be done with inline assembly: that way the bit-setting
 * is guaranteed to be atomic. All bit operations return 0 if the bit
 * was cleared before the operation and != 0 if it was not.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

/*
 * Some hacks to defeat gcc over-optimizations..
 */
struct __dummy { unsigned long a[100]; };
#define ADDR (*(struct __dummy *) addr)

static __inline__ int set_bit(int nr, void * addr)
{
	int oldbit;

	__asm__ __volatile__("btsl %2,%1\n\tsbbl %0,%0"
		:"=r" (oldbit),"=m" (ADDR)
		:"ir" (nr));
	return oldbit;
}

static __inline__ int clear_bit(int nr, void * addr)
{
	int oldbit;

	__asm__ __volatile__("btrl %2,%1\n\tsbbl %0,%0"
		:"=r" (oldbit),"=m" (ADDR)
		:"ir" (nr));
	return oldbit;
}

static __inline__ int change_bit(int nr, void * addr)
{
	int oldbit;

	__asm__ __volatile__("btcl %2,%1\n\tsbbl %0,%0"
		:"=r" (oldbit),"=m" (ADDR)
		:"ir" (nr));
	return oldbit;
}

/*
 * This routine doesn't need to be atomic, but it's faster to code it
 * this way.
 */
static __inline__ int test_bit(int nr, void * addr)
{
	int oldbit;

	__asm__ __volatile__("btl %2,%1\n\tsbbl %0,%0"
		:"=r" (oldbit)
		:"m" (ADDR),"ir" (nr));
	return oldbit;
}

/*
 * Find-bit routines..
 */
static inline int find_first_zero_bit(void * addr, unsigned size)
{
	int res;

	if (!size)
		return 0;
	__asm__("
		cld
		movl $-1,%%eax
		xorl %%edx,%%edx
		repe; scasl
		je 1f
		xorl -4(%%edi),%%eax
		subl $4,%%edi
		bsfl %%eax,%%edx
1:		subl %%ebx,%%edi
		shll $3,%%edi
		addl %%edi,%%edx"
		:"=d" (res)
		:"c" ((size + 31) >> 5), "D" (addr), "b" (addr)
		:"ax", "cx", "di");
	return res;
}

static inline int find_next_zero_bit (void * addr, int size, int offset)
{
	unsigned long * p = ((unsigned long *) addr) + (offset >> 5);
	int set = 0, bit = offset & 31, res;
	
	if (bit) {
		/*
		 * Look for zero in first byte
		 */
		__asm__("
			bsfl %1,%0
			jne 1f
			movl $32, %0
1:			"
			: "=r" (set)
			: "r" (~(*p >> bit)));
		if (set < (32 - bit))
			return set + offset;
		set = 32 - bit;
		p++;
	}
	/*
	 * No zero yet, search remaining full bytes for a zero
	 */
	res = find_first_zero_bit (p, size - 32 * (p - (unsigned long *) addr));
	return (offset + set + res);
}

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static inline unsigned long ffz(unsigned long word)
{
	__asm__("bsfl %1,%0"
		:"=r" (word)
		:"r" (~word));
	return word;
}

/* 
 * memscan() taken from linux asm/string.h
 */
/*
 * find the first occurrence of byte 'c', or 1 past the area if none
 */
static inline char * memscan(void * addr, unsigned char c, int size)
{
        if (!size)
                return addr;
        __asm__("cld
                repnz; scasb
                jnz 1f
                dec %%edi
1:              "
                : "=D" (addr), "=c" (size)
                : "0" (addr), "1" (size), "a" (c));
        return addr;
}

#endif /* !_SYS_GNU_EXT2FS_I386_BITOPS_H_ */
