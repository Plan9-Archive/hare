/*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */
#define KiB		1024u			/* Kibi 0x0000000000000400 */
#define MiB		1048576u		/* Mebi 0x0000000000100000 */
#define GiB		1073741824u		/* Gibi 000000000040000000 */
#define TiB		1099511627776ull	/* Tebi 0x0000010000000000 */
#define PiB		1125899906842624ull	/* Pebi 0x0004000000000000 */
#define EiB		1152921504606846976ull	/* Exbi 0x1000000000000000 */

#define HOWMANY(x, y)	(((x)+((y)-1))/(y))
#define ROUNDUP(x, y)	(HOWMANY((x), (y))*(y))
#define ROUNDDN(x, y)	(((x)/(y))*(y))
#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))

/*
 * Sizes
 */
#define	BI2BY		8			/* bits per byte */
#define	BY2V		8			/* bytes per vlong */
#define	BY2SE		4			/* bytes per stack element */
#define	BY2PG		4096			/* bytes per page */
#define	PGSHIFT		12			/* log(BY2PG) */
#define	PGROUND(s)	ROUNDUP(s, BY2PG)
#define	STACKALIGN(sp)	((sp) & ~7)		/* bug: assure with alloc */

#define	ICACHESIZE	32768			/* 0, 4, 8, 16, or 32 KB */
#define	ICACHEWAYSIZE	(ICACHESIZE/64)		/* 64-way set associative */
#define	ICACHELINELOG	5			/* 8 words (4 bytes) per line */
#define	ICACHELINESZ	(1<<ICACHELINELOG)

#define	DCACHESIZE	32768			/* 0, 4, 8, 16, or 32 KB */
#define	DCACHEWAYSIZE	(DCACHESIZE/64)		/* 64-way set associative */
#define	DCACHELINELOG	5			/* 8 words (4 bytes) per line */
#define	DCACHELINESZ	(1<<DCACHELINELOG)

#define	BLOCKALIGN	64			/* for mal d'orteil */

#define	MAXMACH		4			/* max # cpus system can run */
#define	MACHSIZE	(4*1024)

/*
 * Time
 */
#define	HZ		100			/* clock frequency */
#define	MHz		1000000
#define	TK2SEC(t)	((t)/HZ)		/* ticks to seconds */

/*
 * Bit encodings for Machine State Register (MSR)
 */
#define	MSR_AP	0x02000000	/* auxiliary processor available */
#define	MSR_APE	0x00080000	/* APU exception enable */
#define	MSR_WE	0x00040000	/* wait state enable */
#define	MSR_CE	0x00020000	/* critical interrupt enable */
#define	MSR_EE	0x00008000	/* enable external/decrementer interrupts */
#define	MSR_PR	0x00004000	/* =1, user mode */
#define	MSR_FP	0x00002000	/* floating-point available */
#define	MSR_ME	0x00001000	/* enable machine check exceptions */
#define	MSR_FE0	0x00000800	/* floating-point exception mode 0 */
#define	MSR_DWE	0x00000400	/* debug wait enable */
#define	MSR_DE	0x00000200	/* debug interrupts enable */
#define	MSR_FE1	0x00000100	/* floating-point exception mode 1 */
#define	MSR_IS	0x00000020	/* instruction address space */
#define	MSR_DS	0x00000010	/* data address space */

/* state in user mode */
#define	UMSR	(MSR_PR|MSR_CE|MSR_EE|MSR_DE)

/*
 * Exception Syndrome Register (ESR)
 */
#define	ESR_MCI	0x80000000	/* instruction machine check */
#define	ESR_PIL	0x08000000	/* program interrupt: illegal instruction */
#define	ESR_PPR	0x04000000	/* program interrupt: privileged */
#define	ESR_PTR	0x02000000	/* program interrupt: trap with successful compare */
#define	ESR_PEU	0x01000000	/* program interrupt: unimplemented APU/FPU operation */
#define	ESR_DST	0x00800000	/* data storage interrupt: store fault */
#define	ESR_DIZ	0x00400000	/* data/instruction storage interrupt: zone fault */
#define	ESR_PFP	0x00080000	/* program interrupt: FPU interrupt occurred */
#define	ESR_PAP	0x00040000	/* program interrupt: APU interrupt occurred */
#define	ESR_U0F	0x00008000	/* data storage interrupt: u0 fault */

/*
 * Memory Management Unit Control Register (MMUCR)
 */
#define MMUCR_STS	0x00010000	/* search translation space */
#define MMUCR_IULXE	0x00040000	/* instruction cache unlock exception enable */
#define MMUCR_DULXE	0x00080000	/* data cache unlock exception enable */
#define MMUCR_U3L2SWOAE	0x00100000	/* U3 L3 store without allocate enable */
#define MMUCR_U2SWOAE	0x00200000	/* U2 store without allocate enable */
#define MMUCR_U1TE	0x00400000	/* U1 transient enable */
#define MMUCR_SWOA	0x01000000	/* store without allocate */
#define MMUCR_L2SWOA	0x02000000	/* L2 store without allocate */

/*
 * Interrupt vector offsets
 */
#define	INT_CI		0x0100		/* Critical input interrupt */
#define	INT_MCHECK	0x0200		/* Machine check */
#define	INT_DSI		0x0300		/* Data storage interrupt */
#define	INT_ISI		0x0400		/* Instruction storage interrupt */
#define	INT_EI		0x0500		/* External interrupt */
#define	INT_ALIGN	0x0600		/* Alignment */
#define	INT_PROG	0x0700		/* Program */
#define	INT_FPU		0x0800		/* FPU unavailable */
#define	INT_DEC		0x0900		/* UNUSED on 405? */
#define	INT_SYSCALL	0x0C00		/* System call */
#define	INT_TRACE	0x0D00		/* UNUSED on 405? */
#define	INT_FPA		0x0E00		/* UNUSED on 405? */
#define	INT_APU		0x0F20		/* APU unavailable */
#define	INT_PIT		0x1000		/* PIT interrupt */
#define	INT_FIT		0x1010		/* FIT interrupt */
#define	INT_WDT		0x1020		/* Watchdog timer */
#define	INT_DMISS	0x1100		/* Data TLB miss */
#define	INT_IMISS	0x1200		/* Instruction TLB miss */
#define	INT_DEBUG	0x2000		/* Debug */

/*
 * Magic registers
 */
#define	MACH		30		/* R30 is m-> */
#define	USER		29		/* R29 is up-> */

/*
 * Virtual MMU
 */
#define	PTEMAPMEM	(1024*1024)
#define	PTEPERTAB	(PTEMAPMEM/BY2PG)
#define	SEGMAPSIZE	1984
#define	SSEGMAPSIZE	16
#define	PPN(x)		((x)&~(BY2PG-1))

#define	PTEVALID	(1<<0)
#define	PTEWRITE	(1<<1)
#define	PTERONLY	(0<<1)
#define	PTEUNCACHED	(1<<2)

/*
 * Physical MMU
 */
#define	NTLB		64		/* number of entries */
#define	NTLBPID		256		/* number of hardware pids (0 = global) */

/* TLBHI */
#define	TLBEPN(x)	((x) & ~0x3FF)
#define	TLB1K		(0x00<<4)
#define	TLB4K		(0x01<<4)
#define	TLB16K		(0x02<<4)
#define	TLB64K		(0x03<<4)
#define	TLB256K		(0x04<<4)
#define	TLB1MB		(0x05<<4)
#define	TLB4MB		(0x06<<4)	/* 4MiB not implemented */
#define	TLB16MB		(0x07<<4)
#define	TLB32MB		(0x08<<4)	/* 32MiB not implemented */
#define	TLB256MB	(0x09<<4)
#define	TLB1GB		(0x0a<<4)
#define	TLBVALID	(1<<9)
#define	TLBTS		(1<<8)		/* Translation address space */

/* TLBMID */
#define	TLBRPN(x)	((x) & ~0x3FF)
#define TLBERPN(pa)	(((pa)>>32) & 0xf)

/* TLBLO */
#define TLBWL1		(1<<20)		/* Write-through L1 */
#define TLBIL1I		(1<<19)		/* Inhibit L1 icache */
#define TLBIL1D		(1<<18)		/* Inhibit L1 dcache */
#define TLBIL2I		(1<<17)		/* Inhibit L2 icache */
#define TLBIL2D		(1<<16)		/* Inhibit L2 dcache */
#define	TLBU0		(1<<15)		/* user definable */
#define	TLBU1		(1<<14)		/* user definable */
#define	TLBU2		(1<<13)		/* user definable */
#define	TLBU3		(1<<12)		/* user definable */
#define	TLBW		(1<<11)		/* write-through? */
#define	TLBI		(1<<10)		/* cache inhibit */
#define	TLBM		(1<<9)		/* memory coherent */
#define	TLBG		(1<<8)		/* guarded */
#define	TLBLE		(1<<7)		/* little endian mode */
#define	TLBUX		(1<<5)		/* user execute enable */
#define	TLBUW		(1<<4)		/* user writable */
#define	TLBUR		(1<<3)		/* user readable */
#define	TLBSX		(1<<2)		/* supervisor execute enable */
#define	TLBSW		(1<<1)		/* supervisor writable */
#define	TLBSR		(1<<0)		/* supervisor readable */

/* shorthand */
#define	TLBWR		(TLBSW|TLBSR)
#define	TLBXWR		(TLBSX|TLBSW|TLBSR)
#define TLBCI		(TLBIL1I|TLBIL1D|TLBIL2I|TLBIL2D)

/*
 * software TLB (for quick reload by [id]tlbmiss)
 */
#define	STLBLOG		10
#define	STLBSIZE	(1<<STLBLOG)

/*
 * Address spaces
 */
#define	KSEG0		0xf0000000
#define	KSEG1		0xc0000000		/* KMap */
#define	KSEGM		0xf0000000		/* mask to check segment */
#define	KZERO		KSEG0			/* base of kernel address space */
#define	KTZERO		(KZERO)			/* first address in kernel text */

#define UTZERO		(0x100000)		/* first address in user text */
#define UTROUND(t)	ROUNDUP((t), 0x100000)
#define	USTKTOP		(KSEG1)			/* byte just beyond user stack */
#define	USTKSIZE	(4*1024*1024)		/* size of user stack */
#define TSTKTOP		(USTKTOP-USTKSIZE)	/* end of new stack in sysexec */

#define	KSTACK		(16*1024)		/* Size of kernel stack */

#define	UREGSIZE	((8+40)*4)

#include "physmem.h"

#define getpgcolor(a)	0			/* ../port/page.c */
